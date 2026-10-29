#include "cellsync.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <unordered_set>

#include "cell_logic.h"
#include "game.h"
#include "modes.h"
#include "net/interp.h"
#include "net/utf8.h"
#include "net/validate.h"
#include "sporekit_game.hpp"
#include "fixes.h"

namespace cellsync {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_ok;
static uintptr_t a_Wipe, a_Cine, a_Pair, a_GetData, a_DamageCell, a_KillCell, a_Break, a_Destroy, a_AddFood, a_Populate, a_Rollover, a_GetDamage,
    a_RefCreate, a_Serializer, a_S, a_Fnv, a_Effect, a_TargetParams, a_IsPrey, a_IsThreat, a_ScaleDiff, a_NoAttack, a_Alert, a_React, a_Rest,
    a_Soft, a_ChunkA, a_ChunkB, a_Bite, a_SpawnLoot, a_EatFood;

// ---- cCellObjectData fields (docs/re/cell.md) ----
constexpr uintptr_t kTarget = 0x08, kFlags = 0x48, kCounter = 0x4A, kPos = 0x4C, kSize = 0x58, kRot = 0x5C,
                    kPrevPos = 0x84, kVel = 0x90, kTargetSize = 0xB4, kModelKey = 0xFC, kRes = 0x108,
                    kInvuln = 0x111, kDead = 0x112, kDying = 0x113, kAiLock = 0x16C, kHp = 0x244,
                    kKeepAlive = 0x354, kLevel = 0x358, kQuery = 0x35C, kFlee = 0x220, kChase = 0x22C;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
static bool in_cell() { return g_ok && game::mode_id() == modes::kCell && modes::cell_game(); }

static uint8_t* G() { return modes::cell_game(); }
static uint32_t avatar_idx() { return G() ? F<uint32_t>(G(), 0x411C) : 0; }
static uint8_t* avatar() { return G() ? modes::cell_at(avatar_idx()) : nullptr; }
// the mate the player's mating call brought (0 = none); the editor opens when the avatar reaches it
static uint32_t mate_idx() { return G() ? F<uint32_t>(G(), 0x51D4) : 0; }
static void* fg_query() { return G() ? F<void*>(G(), 0x40FC) : nullptr; }
// cCellSerializableData: cCellGame+0x5190 (the global at 0x16B4DC0 holds the same pointer).
// Only in the Cell stage: cCellGame stays allocated after it (the Creature stage), its pool and
// what it points to do not (the player report of 2026-10-25, cell_at in the Creature stage).
static uint8_t* S()
{
    if (!in_cell())
        return nullptr;
    uint8_t* g = G();
    uint8_t* s = g ? F<uint8_t*>(g, 0x5190) : nullptr;
    return s ? s : (a_S ? *(uint8_t**)a_S : nullptr);
}

// every live foreground cell (gameplay layer), avatar excluded
template <typename Fn>
static void for_each_cell(Fn fn)
{
    uint8_t* g = G();
    if (!g)
        return;
    uint8_t* data = F<uint8_t*>(g, 0x1C);
    uint32_t used = F<uint32_t>(g, 0x1C + 0x10), size = F<uint32_t>(g, 0x1C + 0x14);
    if (!data || size != 0x398)
        return;
    void* fq = fg_query();
    uint32_t av = avatar_idx();
    for (uint32_t slot = 0; slot < used && slot < 4096; slot++) {
        uint8_t* c = data + (size_t)slot * size;
        uint32_t idx = F<uint32_t>(c, 0);
        if ((idx & 0xFFFF) != slot || idx == av || F<void*>(c, kQuery) != fq)
            continue;
        fn(c, idx);
    }
}

static net::Vec3 pos_of(uint8_t* c)
{
    auto* p = (float*)(c + kPos);
    return {p[0], p[1], p[2]};
}

static void set_pos(uint8_t* c, const net::Vec3& p)
{
    auto* d = (float*)(c + kPos);
    d[0] = p.x; d[1] = p.y; d[2] = p.z;
    auto* pp = (float*)(c + kPrevPos);
    pp[0] = p.x; pp[1] = p.y; pp[2] = p.z;
    F<uint16_t>(c, kFlags) |= 4;
    F<uint16_t>(c, kCounter)++;
}

// ---------------- host ----------------
static double g_last_send, g_last_food;
static uint16_t g_cycle;
static uint32_t g_own_bites, g_own_hits; // DamageCell by our avatar / that took hp
static uint32_t g_own_chunks; // chunks our avatar bit off the host's cells (EatChunk)
static uint32_t g_hits_in, g_eats_in, g_hits_aggro, g_bitten_in, g_puppet_hits_dropped, g_kills_in, g_eats_late;
static int32_t g_food_sent = -1, g_plant_sent = -1;

// ---------------- guest ----------------
struct Mirror {
    uint32_t local = 0;
    net::Vec3 pos, vel;
    net::Quat rot;
    float size = 1;
    int hp = 6;
    double seen = 0;
};
static std::unordered_map<uint32_t, Mirror> g_mirrors;   // host index -> mirror
static std::unordered_map<uint32_t, uint32_t> g_host_of; // local index -> host index
static std::unordered_set<uint32_t> g_cycle_seen;
static bool g_our_destroy, g_food_guard, g_replay;
// what the guest's avatar ate this frame (AddFoodPoints): sent with the eaten cell (MSG_CELL_EAT) so the
// host adds it only if the cell was still there (eaten once), else on its own at the end of the frame
static int g_pend_food;
static bool g_pend_plant;
static uint32_t g_kills_sent, g_death_replays, g_loot_skipped;
static uint32_t g_hurt_local, g_hurt_fwd, g_soft_moves, g_bites_taken, g_damage_taken;
// coop.pool mirrorfix 0|1 (tests): 0 = the guest of 0.7.2 (mirrors not turned, chunk and lethal bites not
// sent to the host), to show what the fixes change in the same session
static bool g_mirror_fixes = true;
static bool g_leave_fix = true; // coop.pool leavefix 0|1: reproduce voluntary leave before BUG-0028
COOP_FIX("pool.mirror", "BUG-0001", g_mirror_fixes, "cell: the guest turns the host cells and sends chunk and lethal bites (coop.pool mirrorfix)");
static bool g_fix_forget = true; // off: mirrors kept after the stage, destroyed in its freed pool (BUG-0006, tests)
COOP_FIX("pool.forget", "BUG-0006", g_fix_forget, "cell: mirrors and puppet indices are forgotten once the Cell stage is left");
COOP_FIX("pool.leave", "BUG-0028", g_leave_fix, "cell: a guest leaving the session drops its mirror cells (coop.pool leavefix)");

bool voluntary_leave_cleanup_enabled() { return g_leave_fix; }

// Fixes of bug wave 2 with their old behaviour kept for the tests (`coop.pool fix <name>|all 0|1`):
// a test that never failed proves nothing (CLAUDE.md 3).
static bool g_fix_purge = true;    // BUG-0032: mirrors removed only after a complete state round
static bool g_fix_hostof = true;   // BUG-0088: a recreated mirror drops its old local index
static bool g_fix_foodseq = true;  // BUG-0051: the guest keeps its meals the host has not counted yet
static bool g_fix_resend = true;   // BUG-0128: food / DNA totals sent again every 2 s and to newcomers
static bool g_fix_kostale = true;  // BUG-0061: a remote knockout ends when its owner leaves or stops refreshing it
static bool g_fix_koreset = true;  // BUG-0070: our knockout ends with the session or the stage
static bool g_fix_bodylevel = true; // BUG-0041: a level-up during the regrowth gives the level's own size
static bool g_fix_rescue = true;    // BUG-0131: a knocked-out partner rescues, revives nobody
static bool g_fix_guestbite = true; // BUG-0078: a guest's bite passes DamageCell's gate (ShouldNotAttack(victim, puppet), +0x17C, +0x111)
COOP_FIX("pool.purge", "BUG-0032", g_fix_purge, "cell: mirrors removed only after a complete state round");
COOP_FIX("pool.hostof", "BUG-0088", g_fix_hostof, "cell: a recreated mirror drops its old local index");
COOP_FIX("pool.foodseq", "BUG-0051", g_fix_foodseq, "cell: the guest keeps its meals the host has not counted yet");
COOP_FIX("pool.resend", "BUG-0128", g_fix_resend, "cell: food / DNA totals sent again every 2 s and to newcomers");
COOP_FIX("pool.kostale", "BUG-0061", g_fix_kostale, "cell: a remote knockout ends when its owner leaves or stops refreshing it");
COOP_FIX("pool.koreset", "BUG-0070", g_fix_koreset, "cell: our knockout ends with the session or the stage");
COOP_FIX("pool.bodylevel", "BUG-0041", g_fix_bodylevel, "cell: a level-up during the regrowth gives the level's own size");
COOP_FIX("pool.rescue", "BUG-0131", g_fix_rescue, "cell: a knocked-out partner rescues, revives nobody");
COOP_FIX("pool.guestbite", "BUG-0078", g_fix_guestbite, "cell: a guest's bite passes the game's ShouldNotAttack gate");
// wave 5 (review of 0.7.3.0)
static bool g_fix_ko_grace = true; // BUG-0238: a remote knockout lasts to its own deadline through late refreshes
COOP_FIX("pool.ko_grace", "BUG-0238", g_fix_ko_grace, "cell: a late knockout refresh does not end it, replay its effect; KO_UP when ours ends on a stage leave");
static double g_kodrop_until; // tests (`coop.pool kodrop <s>`): MSG_CELL_KO ignored until then
static uint32_t g_hits_spared;
bool totals_resend_enabled() { return g_fix_resend; }
static cell_logic::Rounds g_rounds;
static cell_logic::Meals g_meals;              // guest: meals sent, not yet in a host total
static uint32_t g_late_chunks, g_rounds_partial; // guest: state chunks of an older round, rounds with a lost chunk
static std::unordered_map<int, uint32_t> g_food_ack; // host: the last meal number of each guest it applied
struct FoodTx {
    int32_t food = -1, plant = -1;
    uint32_t ack = 0;
    double at = 0;
};
static std::unordered_map<int, FoodTx> g_food_tx; // host: what each guest got last
static uint32_t g_created, g_destroyed, g_hits_sent, g_eats_sent, g_purged, g_bitten_sent, g_soft_turns, g_chunks_sent, g_chunks_taken, g_breaks_sent;

// ---------------- both: symbiosis ----------------
// A lethal hit does not kill a player who has a partner in the pool: the cell is knocked out for
// kDownTime seconds. A partner who stays right next to it for kReviveHold seconds revives it
// (half health). Otherwise it respawns next to the partner as a small cell (half size), which
// grows back by eating or at the next level up. Alone in the pool the vanilla death happens
// (it rebuilds the whole level).
constexpr double kDownTime = 10.0, kReviveHold = 1.5;
constexpr float kSmall = 0.5f;
static double g_last_regen, g_invuln_until, g_last_tick;
static uint32_t g_revives, g_respawns;
static bool g_downed;
static double g_down_since, g_hold;
static net::Vec3 g_down_pos;
static float g_body = 1.f;    // body size factor after a respawn (1 = full size)
static float g_body_size = 0; // the target size we wrote, to notice the game resizing the avatar

static std::string nearest_partner(float max_dist, net::Vec3* where = nullptr, float* dist = nullptr, bool helper = false);


// "right next to": the cells (almost) touch
static float revive_dist(uint8_t* a)
{
    float size = F<float>(a, kSize);
    return std::max(3.f, size * 3.f);
}

// A cell with a soft body (the avatar) takes its position and velocity from its particles
// (modes::cell_move moves their positions and, for a stop, clears their velocities);
// +0x1C4 is the steering acceleration the cell step adds to the particle velocities.
static void stop_at(uint8_t* c, const net::Vec3& p)
{
    modes::cell_move(c, p, true);
    auto* bv = (float*)(c + 0x1C4);
    bv[0] = bv[1] = bv[2] = 0;
    auto* t = (float*)(c + kTarget);
    t[0] = p.x; t[1] = p.y; t[2] = p.z;
    auto* v = (float*)(c + kVel);
    v[0] = v[1] = v[2] = 0;
}

// scale the avatar body (target size; the game smooths +0x58 towards it), keeping the mass in step
static void set_body(uint8_t* a, float factor)
{
    float cur = F<float>(a, kTargetSize);
    if (cur <= 0)
        return;
    float full = g_body < 1.f ? cur / g_body : cur;
    float want = full * factor, k = want / cur;
    F<float>(a, kTargetSize) = want;
    F<float>(a, 0xB8) *= k * k; // mass ~ size^2 (as SetCellSize 0xE57190 computes it)
    F<float>(a, 0xBC) /= k * k; // inverse mass
    g_body = factor;
    g_body_size = want;
}

// ---- what everybody sees ----
enum : uint8_t { KO_UP = 0, KO_OUT = 1, KO_REVIVED = 2, KO_RESPAWNED = 3 };
struct Ko {
    uint8_t state = KO_UP;
    float left = 0, hold = 0;
    double since = 0, fx_at = 0;
    double refreshed = 0; // the last MSG_CELL_KO of its owner (KO_OUT comes every 0.25 s)
    bool expired = false; // ended here by ko_expire, not by its owner (BUG-0238: no effect replay)
    uint32_t fx = 0;      // death effects played on its puppet (tests)
};
static std::unordered_map<int, Ko> g_ko; // remote player -> knockout state

// the nearest partner; `helper`: one who can help: rescue (a lethal hit becomes a knockout), revive,
// the spot to respawn at. A knocked-out partner (held at hp 1, never flagged dead) cannot (BUG-0131: two cells
// knocked out side by side revived each other). Old (`fix rescue 0`): any partner.
static std::string nearest_partner(float max_dist, net::Vec3* where, float* dist, bool helper)
{
    uint8_t* a = avatar();
    if (!a || !C.partners)
        return {};
    std::unordered_set<std::string> out;
    if (helper && g_fix_rescue && C.sess)
        for (auto& [id, k] : g_ko)
            if (k.state == KO_OUT)
                out.insert(C.sess->name_of((uint8_t)id));
    net::Vec3 me = pos_of(a);
    std::string best;
    float bd = max_dist;
    for (auto& [n, p] : C.partners()) {
        if (out.count(n))
            continue;
        float d = net::dist(me, p);
        if (d <= bd) {
            bd = d;
            best = n;
            if (where)
                *where = p;
        }
    }
    if (dist)
        *dist = best.empty() ? -1.f : bd;
    return best;
}
static double g_ko_sent;
constexpr float kKoOpacity = 0.35f;
constexpr uintptr_t kOpacity = 0xA4;

// a one-shot game effect on a cell (names from the game: cell_death_oneshot, cell_hatch_ice, ...)
static void effect(uint8_t* c, const char* name)
{
    if (!c || !a_Fnv || !a_Effect || !F<uint32_t>(c, 0x248))
        return;
    uint32_t id = skg::cdecl_call<uint32_t>(a_Fnv, name, 0x811C9DC5u, 1);
    skg::cdecl_call<void*>(a_Effect, c, id);
}

static void send_ko(uint8_t state, float left, float hold)
{
    if (!C.sess || !C.sess->connected())
        return;
    net::Writer w;
    w.u8((uint8_t)C.sess->my_id());
    w.u8(state);
    w.u8((uint8_t)std::max(0.f, std::min(left * 10.f, 255.f)));
    w.u8((uint8_t)std::max(0.f, std::min(hold * 255.f, 255.f)));
    if (C.sess->role() == net::Role::Host)
        C.sess->broadcast_reliable(net::MSG_CELL_KO, w);
    else
        C.sess->send_reliable_to(0, net::MSG_CELL_KO, w);
    g_ko_sent = H->time_seconds();
}

static uint8_t* puppet_cell(int id)
{
    if (!C.puppets)
        return nullptr;
    for (auto& p : C.puppets())
        if (p.id == id)
            return (uint8_t*)p.cell;
    return nullptr;
}

static void on_ko(int id, uint8_t state, float left, float hold)
{
    Ko& k = g_ko[id];
    uint8_t prev = k.state;
    k.left = left;
    k.hold = hold;
    k.refreshed = H->time_seconds();
    bool expired = k.expired;
    k.expired = false;
    if (state == prev)
        return;
    k.state = state;
    k.since = k.refreshed;
    // outside the Cell stage only the state is kept (BUG-0061: the "up again" was dropped there);
    // the puppets of other stages are not cells
    uint8_t* c = in_cell() ? puppet_cell(id) : nullptr;
    if (state == KO_OUT && expired && g_fix_ko_grace && c)
        F<float>(c, kOpacity) = kKoOpacity; // the same knockout after a gap: no second death effect
    else if (state == KO_OUT) {
        effect(c, "cell_death_oneshot");
        k.fx += c != nullptr;
    }
    else if (state == KO_REVIVED)
        effect(c, "cell_hatch_ice");
    else if (state == KO_RESPAWNED)
        k.fx_at = k.since + 0.4; // after the puppet has moved next to its partner
    if (state != KO_OUT && c)
        F<float>(c, kOpacity) = 1.f;
}

// BUG-0061: a "knocked out" its owner no longer refreshes (it left, its "up again" was lost while we
// were outside the Cell stage) ends here: the partner was drawn translucent with a frozen "KO" label
static void ko_expire(double now)
{
    for (auto& [id, k] : g_ko) {
        if (k.state != KO_OUT || !cell_logic::ko_stale(now, k.refreshed, k.left, g_fix_kostale, g_fix_ko_grace))
            continue;
        k.state = KO_UP;
        k.since = now;
        k.expired = true;
        if (uint8_t* c = in_cell() ? puppet_cell(id) : nullptr)
            F<float>(c, kOpacity) = 1.f;
    }
}

static bool go_down(uint8_t* c)
{
    if (!c || c != avatar() || !C.sess || !C.sess->connected())
        return false;
    if (!g_downed) {
        if (nearest_partner(1e9f, nullptr, nullptr, true).empty())
            return false; // alone in the pool: the vanilla death
        g_downed = true;
        g_down_since = H->time_seconds();
        g_hold = 0;
        g_down_pos = pos_of(c);
        effect(c, "cell_death_oneshot");
        send_ko(KO_OUT, (float)kDownTime, 0);
        if (C.notice)
            C.notice(C.my_name() + " is knocked out: swim right next to them within 10 s");
    }
    F<int>(c, kHp) = 1;
    F<uint8_t>(c, kDead) = 0;
    F<uint8_t>(c, kDying) = 0;
    F<uint8_t>(c, kInvuln) = 1;
    g_invuln_until = 0;
    stop_at(c, g_down_pos);
    return true;
}

static void downed_tick(uint8_t* a, double now, double dt)
{
    if (F<uint8_t>(a, kDying) || F<uint8_t>(a, kDead)) {
        F<uint8_t>(a, kDead) = 0;
        F<uint8_t>(a, kDying) = 0;
    }
    F<int>(a, kHp) = 1;
    F<uint8_t>(a, kInvuln) = 1;
    F<float>(a, kOpacity) = kKoOpacity;
    stop_at(a, g_down_pos);
    float d = 0;
    std::string helper = nearest_partner(revive_dist(a), nullptr, &d, true);
    g_hold = helper.empty() ? 0 : g_hold + dt;
    if (now - g_ko_sent > 0.25)
        send_ko(KO_OUT, (float)std::max(0.0, kDownTime - (now - g_down_since)), (float)(g_hold / kReviveHold));
    if (!helper.empty() && g_hold >= kReviveHold) { // revived by the partner
        g_downed = false;
        F<int>(a, kHp) = 3;
        F<float>(a, kOpacity) = 1.f;
        g_invuln_until = now + 2.0;
        g_revives++;
        effect(a, "cell_hatch_ice");
        send_ko(KO_REVIVED, 0, 1);
        if (C.notice)
            C.notice(helper + " revived " + C.my_name());
        return;
    }
    if (now - g_down_since < kDownTime)
        return;
    // nobody came: respawn next to the partner, small again
    g_downed = false;
    net::Vec3 at = g_down_pos;
    std::string partner = nearest_partner(1e9f, &at, nullptr, true);
    stop_at(a, partner.empty() ? at : net::Vec3{at.x + 2.f, at.y + 1.f, 0.f});
    F<uint8_t>(a, 0x04) = 1; // idle
    F<int>(a, kHp) = 6;
    g_invuln_until = now + 3.0;
    set_body(a, kSmall);
    F<float>(a, kOpacity) = 1.f;
    g_respawns++;
    effect(a, "cell_egg_L0_autohatch");
    send_ko(KO_RESPAWNED, 0, 0);
    if (C.notice)
        C.notice(C.my_name() + " respawned small" + (partner.empty() ? "" : " next to " + partner));
}

// ai.type of a cell (docs/re/cell.md 1: the first int of the AI block chosen by difficulty), -1 unknown
static int ai_type(uint8_t* c)
{
    void* ref = F<void*>(c, kRes);
    if (!ref || !a_GetData)
        return -1;
    void* holder = nullptr;
    uint8_t* res = skg::cdecl_call<uint8_t*>(a_GetData, ref, &holder);
    if (holder)
        --F<int>(holder, 0xC);
    if (!res)
        return -1;
    int d = S() ? F<int>(S(), 0x7C) : 1;
    uint8_t* ai = res + 0xE0;
    if (d == 0 && F<int>(res, 0x248) != -1)
        ai = res + 0x248;
    else if (d == 2 && F<int>(res, 0x194) != -1)
        ai = res + 0x194;
    return F<int>(ai, 0);
}

// ---------------- guest -> host: what our avatar did to a cell of the host's pool ----------------
// MSG_CELL_EAT: u32 host index, u8 kind, u8 arg, i32 food, u8 plant (older guests send the index only)
enum : uint8_t { ACT_GONE = 0, ACT_KILL = 1, ACT_BREAK = 2 };
static void send_act(uint32_t hidx, uint8_t kind, uint8_t arg, int food, bool plant)
{
    net::Writer w;
    w.u32(hidx);
    w.u8(kind);
    w.u8(arg);
    w.u32((uint32_t)food);
    w.u8(plant ? 1 : 0);
    w.u32(g_meals.sent(food)); // the meal number the host echoes in its total (BUG-0051)
    C.sess->send_reliable_to(0, net::MSG_CELL_EAT, w);
}

static void flush_food()
{
    if (!g_pend_food)
        return;
    if (is_guest()) {
        net::Writer w;
        w.u32((uint32_t)g_pend_food);
        w.u8(g_pend_plant ? 1 : 0);
        w.u32(g_meals.sent(g_pend_food));
        C.sess->send_reliable_to(0, net::MSG_CELL_FOOD, w);
    }
    g_pend_food = 0;
}

// the host index of a mirror (guest), 0 when the cell is not one
static uint32_t mirror_key(uint8_t* c)
{
    if (!c || !is_guest())
        return 0;
    auto it = g_host_of.find(F<uint32_t>(c, 0));
    return it != g_host_of.end() ? it->second : 0;
}

// ---------------- hooks ----------------
using DamageCell_t = bool(SK_CDECL*)(uint8_t*, uint8_t*, const float*, int, int, float);
static DamageCell_t o_DamageCell;
static void hurt_avatar(int dmg, int type);

// guest: our cell bit the host's cell <hidx> (its mirror here): the host applies it (MSG_CELL_HIT)
static void send_hit(uint32_t hidx, int dmg, int type)
{
    net::Writer w;
    w.u32(hidx);
    w.u8((uint8_t)std::max(0, std::min(dmg, 255)));
    w.u8((uint8_t)type);
    C.sess->send_reliable_to(0, net::MSG_CELL_HIT, w);
    g_hits_sent++;
}

static bool SK_CDECL h_DamageCell(uint8_t* victim, uint8_t* attacker, const float* dir, int type, int x, float dt)
{
    if (!in_cell())
        return o_DamageCell(victim, attacker, dir, type, x, dt);
    // Damage to a player's cell is decided on that player's machine: there the host's NPC is a
    // mirror that swims into the real cell, and the guest's own collision step bites it (hurt_local,
    // then its DamageCell / KillCell hooks: knockout, revive). The host's picture of the guest
    // (a puppet, no collision pairs, invulnerable) takes nothing, or the guest would be bitten twice
    // (docs/re/cell.md 9). The guest tells the host which NPC bit it (MSG_CELL_BITTEN) for the
    // NPC's own after-bite behaviour.
    if (is_host() && victim && C.puppet_owner && C.puppet_owner(victim) > 0) {
        g_puppet_hits_dropped++;
        return false;
    }
    if (is_guest() && victim) {
        auto it = g_host_of.find(F<uint32_t>(victim, 0));
        if (it != g_host_of.end()) { // a mirror: the host applies the bite
            if (attacker == avatar())
                send_hit(it->second, a_GetDamage ? skg::cdecl_call<int>(a_GetDamage, attacker, victim) : 1, type);
            return false;
        }
        if (victim == avatar() && attacker && g_host_of.count(F<uint32_t>(attacker, 0)))
            g_hurt_local++; // a mirror bit us here: the only way a host NPC hurts us (see above)
    }
    if (victim && victim == avatar()) { // damage our own cell takes (diagnostics: is the guest as exposed?)
        int before = F<int>(victim, kHp);
        bool r = o_DamageCell(victim, attacker, dir, type, x, dt);
        int lost = before - F<int>(victim, kHp);
        if (lost > 0 && !g_downed) {
            g_bites_taken++;
            g_damage_taken += lost;
        }
        if (lost > 0 && is_guest() && attacker) {
            // a mirror bit us: on the host that NPC only touched our puppet (no bite there), so it
            // would chase us on and on. The host runs its after-bite rest (0xE57930 -> 0xE578B0).
            auto it = g_host_of.find(F<uint32_t>(attacker, 0));
            if (it != g_host_of.end()) {
                net::Writer w;
                w.u32(it->second);
                C.sess->send_reliable_to(0, net::MSG_CELL_BITTEN, w);
                g_bitten_sent++;
            }
        }
        return r;
    }
    if (victim && attacker && attacker == avatar()) { // our own bites on the host's cells (diagnostics)
        int before = F<int>(victim, kHp);
        bool r = o_DamageCell(victim, attacker, dir, type, x, dt);
        g_own_bites++;
        if (F<int>(victim, kHp) < before || F<uint8_t>(victim, kDying))
            g_own_hits++;
        return r;
    }
    return o_DamageCell(victim, attacker, dir, type, x, dt);
}

// A carnivore's mouth does not bite through DamageCell: the event dispatcher 0xE7E130 runs
// EatChunkA 0xE7DD90 (event 0x13) / EatChunkB 0xE7DBD0 (0x14), cdecl void(u32 eaterIdx,
// u32 victimIdx, u32 ev+0x30, u32 ev+0x3C, float dt): the eater gets the food (OnEat), the victim
// loses GetDamageAmount(eater, victim) hp and KillIfDead may kill it (docs/re/cell.md 10). On the
// guest the victim is a mirror: its hp came back with the next state of the host, the host never
// heard of the bite (the guest could not hurt the host's creatures with a mouth), and a lethal
// chunk killed the mirror only here. The guest now sends the bite to the host as MSG_CELL_HIT
// (type 6, as the host's own chunk bites) and keeps the mirror alive until the host decides.
using Chunk_t = void(SK_CDECL*)(uint32_t, uint32_t, uint32_t, uint32_t, float);
static Chunk_t o_ChunkA, o_ChunkB;

static void chunk(Chunk_t orig, uint32_t eater, uint32_t victim, uint32_t a, uint32_t b, float dt)
{
    uint8_t* e = in_cell() ? modes::cell_at(eater) : nullptr;
    uint8_t* v = e ? modes::cell_at(victim) : nullptr;
    if (!v) {
        orig(eater, victim, a, b, dt);
        return;
    }
    if (e == avatar() && !is_guest())
        g_own_chunks++;
    auto it = is_guest() ? g_host_of.find(victim) : g_host_of.end();
    if (g_mirror_fixes && it != g_host_of.end() && e == avatar()) {
        int dmg = a_GetDamage ? skg::cdecl_call<int>(a_GetDamage, e, v) : 1;
        send_hit(it->second, dmg, 6);
        g_chunks_sent++;
        // the bite shows here (food, health ring, push), but the mirror does not die here
        int hp = F<int>(v, kHp);
        if (hp - dmg < 1)
            F<int>(v, kHp) = dmg + 1;
        orig(eater, victim, a, b, dt);
        F<int>(v, kHp) = std::max(1, hp - dmg);
        return;
    }
    int before = victim == avatar_idx() ? F<int>(v, kHp) : 0;
    orig(eater, victim, a, b, dt);
    if (victim == avatar_idx() && F<int>(v, kHp) < before && !g_downed) {
        g_bites_taken++;
        g_damage_taken += before - F<int>(v, kHp);
        if (is_guest()) { // a mirror bit a chunk off us: the host's NPC rests after it (as for a bite)
            auto at = g_host_of.find(eater);
            if (at != g_host_of.end()) {
                g_chunks_taken++;
                net::Writer w;
                w.u32(at->second);
                C.sess->send_reliable_to(0, net::MSG_CELL_BITTEN, w);
                g_bitten_sent++;
            }
        }
    }
}
static void SK_CDECL h_ChunkA(uint32_t e, uint32_t v, uint32_t a, uint32_t b, float dt) { chunk(o_ChunkA, e, v, a, b, dt); }
static void SK_CDECL h_ChunkB(uint32_t e, uint32_t v, uint32_t a, uint32_t b, float dt) { chunk(o_ChunkB, e, v, a, b, dt); }

using KillCell_t = bool(SK_CDECL*)(uint8_t*, uint32_t, void*, bool, int, float);
static KillCell_t o_KillCell;
static std::unordered_map<uintptr_t, uint32_t> g_destroy_callers; // diagnostics: who destroyed a mirror
static uint32_t g_mirror_kills, g_npc_deaths; // npc_deaths: NPCs of this pool killed / broken by the game
static bool SK_CDECL h_KillCell(uint8_t* c, uint32_t killer, void* res, bool by_player, int type, float dt)
{
    if (uint32_t hidx = !g_replay && g_mirror_fixes && in_cell() ? mirror_key(c) : 0) {
        // Our avatar killed a cell of the host's pool (a lethal bite, the chunks a mouth tears off
        // 0xE7DD90 / 0xE7DBD0 -> KillIfDead, ...). Killed here, its death and its meat (KillCell's loot)
        // stayed on this machine: the loot was purged as cells the host does not have, the host's
        // creature lived on. The host kills it; its death and loot come back with the state.
        g_mirror_kills++;
        g_destroy_callers[((uintptr_t)__builtin_return_address(0) - H->rebase(0)) | 0x20000000u]++;
        if (!F<uint8_t>(c, kDying) && !F<uint8_t>(c, kDead)) {
            send_act(hidx, ACT_KILL, (uint8_t)type, 0, false);
            g_kills_sent++;
        }
        return false;
    }
    if (in_cell() && go_down(c))
        return false;
    bool npc = in_cell() && c && c != avatar() && !modes::cell_is_puppet(c) && !F<uint8_t>(c, kDying) && !F<uint8_t>(c, kDead);
    bool r = o_KillCell(c, killer, res, by_player, type, dt);
    if (npc && F<uint8_t>(c, kDying))
        g_npc_deaths++;
    return r;
}

using Break_t = void(SK_CDECL*)(uint8_t*, uint8_t*, float);
static Break_t o_Break;
static void SK_CDECL h_Break(uint8_t* victim, uint8_t* attacker, float f)
{
    if (uint32_t hidx = g_mirror_fixes && in_cell() ? mirror_key(victim) : 0) {
        // Bite 0xE7B0A0 breaks the victim instead of damaging it when the bite is lethal: effect, then
        // (event 5 0xE7ADD0) its loot and DestroyCell(idx, 1). On a mirror all of it happened here only
        // (the meat stayed on the guest and was purged as stray, the host never saw the kill): the
        // host breaks its own cell (ACT_BREAK), with the same death and loot, mirrored back.
        g_mirror_kills++;
        g_destroy_callers[((uintptr_t)__builtin_return_address(0) - H->rebase(0)) | 0x10000000u]++;
        int st = F<int>(victim, 0x18C);
        if (attacker && attacker == avatar() && st != 7 && st != 8 && st != 9 && !F<uint8_t>(victim, kDead)) {
            effect(victim, "cell_death_break");
            send_act(hidx, ACT_BREAK, 0, 0, false);
            g_kills_sent++;
            g_breaks_sent++;
        }
        return;
    }
    if (in_cell() && go_down(victim))
        return;
    if (in_cell() && victim && victim != avatar() && !modes::cell_is_puppet(victim))
        g_npc_deaths++;
    o_Break(victim, attacker, f);
}

using Destroy_t = void(SK_CDECL*)(uint32_t, bool, float, int);
static Destroy_t o_Destroy;
// Guest: the cells our avatar touched lately (HandleCollisionPair with the avatar), local index -> time.
// Only these can have been eaten or swallowed by us.
static std::unordered_map<uint32_t, double> g_touched;
static uint32_t g_kept, g_puppet_saves, g_puppet_new, g_own_eats, g_own_food;
static std::unordered_map<int, void*> g_puppet_seen;
static void SK_CDECL h_Destroy(uint32_t idx, bool loot, float f, int x)
{
    if (in_cell()) {
        // A partner's cell is ours to remove (modes::cell_despawn forgets it before it destroys it).
        // The game removed it on a level change (RemoveCellsOfOldScale), when an NPC broke or swallowed
        // it, ...: the partner blinked out and in again with an "appeared in the world" message each
        // time (13 times in 10 minutes). Its owner decides what happens to it.
        uint8_t* c = modes::cell_at(idx);
        if (c && modes::cell_is_puppet(c)) {
            uintptr_t ra = (uintptr_t)__builtin_return_address(0) - H->rebase(0);
            g_destroy_callers[ra | 0x80000000u]++;
            g_puppet_saves++;
            F<uint8_t>(c, kDead) = 0;
            F<uint8_t>(c, kDying) = 0;
            return;
        }
    }
    if (is_guest() && !g_our_destroy && in_cell()) {
        auto it = g_host_of.find(idx);
        if (it != g_host_of.end()) {
            uintptr_t ra = (uintptr_t)__builtin_return_address(0);
            auto t = g_touched.find(idx);
            bool touched = t != g_touched.end() && H->time_seconds() - t->second <= 1.0;
            g_destroy_callers[touched ? ((ra - H->rebase(0)) | 0x40000000u) : ra]++;
            if (!touched) {
                // Not our doing: the game removes it for its own reasons (a poison cloud outside our
                // active area 0xE78C00, the cells of the old scale on a level change 0xE7CD10, the area
                // cleared around a hatching cell 0xE78B20, size 0). It is the host's cell: the host
                // decides, the mirror stays until the host's state drops it. Reported, these made
                // the host destroy cells nobody had eaten (docs/stability/cell.md).
                g_kept++;
                return;
            }
            // we ate it (or swallowed it): the host removes it the same way (with its loot, if the
            // game asked for loot) and adds what we got for it, once (not if someone ate it first)
            send_act(it->second, ACT_GONE, loot ? 1 : 0, g_pend_food, g_pend_plant);
            g_pend_food = 0;
            g_eats_sent++;
            g_mirrors.erase(it->second);
            g_host_of.erase(it);
            g_touched.erase(t);
            loot = false;
        }
    }
    o_Destroy(idx, loot, f, x);
}

using AddFood_t = void(SK_CDECL*)(int, bool);
static AddFood_t o_AddFood;
static void SK_CDECL h_AddFood(int amount, bool plant)
{
    uint8_t* a0 = in_cell() ? avatar() : nullptr;
    float before = a0 ? F<float>(a0, kTargetSize) : 0.f;
    o_AddFood(amount, plant);
    if (!g_food_guard && amount > 0 && in_cell()) { // our own avatar ate (AvatarEat 0xE7D070), not the network
        g_own_eats++;
        g_own_food += amount;
    }
    uint8_t* a = in_cell() ? avatar() : nullptr;
    if (a && a == a0 && g_body < 1.f) {
        // we ate: the small body grows back (50 points = full size). A meal over a level line made
        // the game resize the avatar for the new level inside AddFoodPoints (SetCellSize): that size
        // is the full one, not the small one (BUG-0041: it was divided by g_body again, up to 2x).
        float after = F<float>(a, kTargetSize);
        cell_logic::Body b = cell_logic::regrow(before, after, g_body, amount, g_fix_bodylevel);
        if (b.target != after && after > 0) {
            float k = b.target / after;
            F<float>(a, kTargetSize) = b.target;
            F<float>(a, 0xB8) *= k * k; // mass ~ size^2, as set_body
            F<float>(a, 0xBC) /= k * k;
        }
        g_body = b.factor;
        g_body_size = b.target;
    }
    if (is_guest() && !g_food_guard && amount != 0 && in_cell()) {
        // AvatarEat runs before the food is removed (EatFood 0xE7DF40: OnEat, then DestroyCell):
        // h_Destroy takes it with the eaten cell, the tick sends what is left
        if (g_pend_food && g_pend_plant != plant)
            flush_food();
        g_pend_food += amount;
        g_pend_plant = plant;
    }
}

// Guest: interactions between two cells of the host's pool (mirror eats mirror food, mirror
// swallows mirror, the host's cell bumps into a mirror) are the host's business: it simulates them
// and the result arrives with the next state. Run here they made the guest report "eaten" to the
// host for cells nobody on the guest had touched, and the host destroyed them (docs/stability/cell.md).
using Pair_t = void(SK_CDECL*)(uint8_t*, uint8_t*, float);
static Pair_t o_Pair;
static uint32_t g_pairs_skipped;
static const char* kind_of(uint8_t* c);
// what our cell does to creatures (tests: how often a player gets to bite): collision pairs of our
// cell with a creature, interaction events of kind 10 our cell started (Bite 0xE7B0A0)
static uint32_t g_creature_pairs, g_bite_events;
using Bite_t = bool(SK_CDECL*)(uint32_t, uint32_t, void*, void*, int, float);
static Bite_t o_Bite;
static bool SK_CDECL h_Bite(uint32_t att, uint32_t vic, void* p, void* d, int x, float dt)
{
    if (in_cell() && att == avatar_idx())
        g_bite_events++;
    return o_Bite(att, vic, p, d, x, dt);
}
static void SK_CDECL h_Pair(uint8_t* a, uint8_t* b, float dt)
{
    if (in_cell() && a && b && (a == avatar() || b == avatar()) && !strcmp(kind_of(a == avatar() ? b : a), "creature"))
        g_creature_pairs++;
    if (in_cell() && (modes::cell_is_puppet(a) || modes::cell_is_puppet(b))) {
        // a partner's cell is a picture of a cell simulated on its owner's machine: here nothing eats
        // it, swallows it or is eaten by it (the owner's own game does that, with our cells mirrored)
        g_pairs_skipped++;
        return;
    }
    if (is_guest() && in_cell()) {
        uint8_t* av = avatar();
        if (a != av && b != av) {
            g_pairs_skipped++;
            return;
        }
        uint8_t* other = a == av ? b : a;
        if (other) {
            double now = H->time_seconds();
            g_touched[F<uint32_t>(other, 0)] = now;
            if (g_touched.size() > 256) // forget old touches
                for (auto it = g_touched.begin(); it != g_touched.end();)
                    it = now - it->second > 1.0 ? g_touched.erase(it) : std::next(it);
        }
    }
    o_Pair(a, b, dt);
}

// A level change removes every cell of the old scale (RemoveCellsOfOldScale 0xE7CD10). On the
// guest these are mirrors: the host removes its own cells at its own level change (they made the
// guest send ~600 "eaten" reports per level change before h_Destroy kept untouched mirrors).
using Wipe_t = void(SK_CDECL*)();
static Wipe_t o_Wipe;
static uint32_t g_wipes;
static void SK_CDECL h_Wipe()
{
    if (is_guest() && in_cell())
        g_wipes++; // h_Destroy keeps the mirrors (not touched by us): the host's state replaces them
    o_Wipe();
}

using Populate_t = int(SK_CDECL*)(float, int, int, void*, void*, void*, int);
static Populate_t o_Populate;
static int SK_CDECL h_Populate(float prob, int layer, int level, void* query, void* bbox, void* marker, int x)
{
    // guests do not spawn their own creatures/food on the gameplay layer: the host's are mirrored
    if (is_guest() && in_cell() && query == fg_query())
        return 0;
    return o_Populate(prob, layer, level, query, bbox, marker, x);
}

// ShouldNotAttack 0xE57480 (cdecl bool(attacker, victim)): the prey / threat tests of the NPC AI
// (IsPrey 0xE68910, IsThreat 0xE68870) and DamageCell ask it. Its "wontAttackPlayer(WhenSmall)"
// flags of a species (resource +0x300/+0x301) apply only when the victim is the avatar
// (G+0x411C). A partner's cell is a player too: for it the game answers as for the avatar, so an
// NPC that spares the host spares the guest as well (and the other way round).
using NoAttack_t = bool(SK_CDECL*)(uint8_t*, uint8_t*);
static NoAttack_t o_NoAttack;
static bool SK_CDECL h_NoAttack(uint8_t* a, uint8_t* b)
{
    uint8_t* g = G();
    if (!g || !b || !in_cell() || !modes::cell_is_puppet(b))
        return o_NoAttack(a, b);
    uint32_t& av = F<uint32_t>(g, 0x411C);
    uint32_t keep = av;
    size_t mark = game::swap_open(game::swap_undo_u32, &av, keep, "cell avatar index"); // BUG-0110
    av = F<uint32_t>(b, 0); // single-threaded cell step: restored right after
    bool r = o_NoAttack(a, b);
    av = keep;
    game::swap_close(mark);
    return r;
}

// SpawnLoot 0xE771D0: the cells a death leaves behind (meat chunks, part tokens) come from here
// (KillCell, a break 0xE7ADD0, DestroyCell(idx, 1)). Counted per machine: loot_calls / loot_cells.
using SpawnLoot_t = void(SK_CDECL*)(void*, void*, void*, void*, float, int, void*, float, uint32_t*, int*);
static SpawnLoot_t o_SpawnLoot;
static uint32_t g_loot_calls, g_loot_cells;
static std::deque<std::pair<uint32_t, double>> g_loot_recent; // host: cells the last loot drops created
static uint32_t pool_live()
{
    uint8_t* g = G();
    return g ? F<uint32_t>(g, 0x1C + 0xC) : 0;
}
static void SK_CDECL h_SpawnLoot(void* res, void* key, void* query, void* pos, float size, int level, void* table, float dt,
                                 uint32_t* out, int* nout)
{
    bool fg = in_cell() && query && query == fg_query();
    if (fg && is_guest()) {
        // The gameplay cells of the pool are the host's (no PopulateMarker here either): loot the guest's
        // game drops (a mirror broken or killed by the game here, DestroyCell(idx, 1)) would exist only on
        // this machine and be purged as stray at the next state. The host drops its own, mirrored here.
        g_loot_skipped++;
        return;
    }
    uint32_t before = fg ? pool_live() : 0;
    // the callers pass no list of the new cells (KillCell, E7ADD0, DestroyCell): ours records them
    // (E771D0 appends every cell it creates when the list is given, recursion included)
    uint32_t buf[256];
    int n = 0;
    bool mine = fg && !out && !nout;
    o_SpawnLoot(res, key, query, pos, size, level, table, dt, mine ? buf : out, mine ? &n : nout);
    if (fg) {
        g_loot_calls++;
        uint32_t after = pool_live();
        if (after > before)
            g_loot_cells += after - before;
        double now = H->time_seconds();
        for (int i = 0; i < n && i < 256; i++)
            g_loot_recent.push_back({buf[i], now});
        while (g_loot_recent.size() > 64)
            g_loot_recent.pop_front();
    }
}

// SoftBodyStep 0xE5A430 (cdecl void(cell*, resData*, float dt), docs/re/cell.md 10): a soft body
// takes its rotation +0x5C from its particles (shape matching) every cell step, after the AI. A
// mirror's particles are only moved, never turned, by guest_tick: their cloud kept whatever
// orientation the guest's collisions and the shape matching gave it, so the rotation the guest wrote
// was replaced before every drawing and collision step and the host's NPC spun on the guest (and its
// mouth and body faced elsewhere than on the host). For a mirror the
// particle cloud is turned about its centre onto the host's facing right after the fit, and the
// host's matrix is written: what is drawn and what bites or is bitten here faces as on the host.
using Soft_t = void(SK_CDECL*)(uint8_t*, void*, float);
static Soft_t o_Soft;
static void SK_CDECL h_Soft(uint8_t* c, void* res, float dt)
{
    o_Soft(c, res, dt);
    if (!c || !g_mirror_fixes || !is_guest() || !in_cell() || g_mirrors.empty())
        return;
    auto hit = g_host_of.find(F<uint32_t>(c, 0));
    if (hit == g_host_of.end())
        return;
    auto mit = g_mirrors.find(hit->second);
    int n = F<int>(c, 0x250);
    uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
    if (mit == g_mirrors.end() || !parts || !F<uint32_t>(c, 0x248) || n < 2 || n > 64)
        return;
    float want[9];
    modes::quat_to_mat(mit->second.rot, want);
    auto* m = (float*)(c + kRot);
    // row-vector convention (goal = rest * M + centre): the image of the x axis is row 0
    float d = std::atan2(want[1], want[0]) - std::atan2(m[1], m[0]);
    float cs = std::cos(d), sn = std::sin(d);
    float cx = 0, cy = 0;
    int used = 0;
    for (int i = 0; i < n; i++) {
        uint32_t k = F<uint32_t>(c, 0x254 + 4 * i);
        if (k >= 1500)
            continue;
        auto* q = (float*)(parts + 4 + 12 * (size_t)k);
        cx += q[0];
        cy += q[1];
        used++;
    }
    if (!used)
        return;
    cx /= used;
    cy /= used;
    auto turn = [&](float* v, float ox, float oy) {
        float x = v[0] - ox, y = v[1] - oy;
        v[0] = ox + x * cs - y * sn;
        v[1] = oy + x * sn + y * cs;
    };
    for (int i = 0; i < n; i++) {
        uint32_t k = F<uint32_t>(c, 0x254 + 4 * i);
        if (k >= 1500)
            continue;
        turn((float*)(parts + 4 + 12 * (size_t)k), cx, cy);       // position, about the centre
        turn((float*)(parts + 0x4654 + 12 * (size_t)k), 0, 0);    // velocity
        turn((float*)(parts + 0x11944 + 12 * (size_t)k), 0, 0);   // goal offset of the shape matching
    }
    memcpy(m, want, sizeof want);
    memcpy(c + 0x24, want, sizeof want); // the shape matrix the fit wrote with it (blend 0: the same)
    F<uint16_t>(c, kFlags) |= 2;
    g_soft_turns++;
}

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    a_GetData = H->sym("Simulator::Cell::GetData");
    a_Pair = H->sym("Simulator::Cell::HandleCollisionPair");
    a_Cine = H->sym("Simulator::cCinematicManager::Get");
    a_Wipe = H->sym("Simulator::Cell::RemoveCellsOfOldScale");
    a_DamageCell = H->sym("Simulator::Cell::DamageCell");
    a_Fnv = H->sym("FNV_hash");
    a_Effect = H->sym("Simulator::Cell::cCellGFX::InstanceEffectOnCell");
    a_KillCell = H->sym("Simulator::Cell::KillCell");
    a_Break = H->sym("Simulator::Cell::BreakOrSwallow");
    a_Destroy = H->sym("Simulator::Cell::DestroyCell");
    a_AddFood = H->sym("Simulator::Cell::AddFoodPoints");
    a_Populate = H->sym("Simulator::Cell::PopulateMarker");
    a_Rollover = H->sym("Simulator::Cell::ShowHealthRollover");
    a_GetDamage = H->sym("Simulator::Cell::GetDamageAmount");
    a_TargetParams = H->sym("Simulator::Cell::GetTargetSearchParams");
    a_IsPrey = H->sym("Simulator::Cell::IsPrey");
    a_IsThreat = H->sym("Simulator::Cell::IsThreat");
    a_ScaleDiff = H->sym("Simulator::Cell::GetScaleDifference");
    a_NoAttack = H->sym("Simulator::Cell::ShouldNotAttack");
    a_Alert = H->sym("Simulator::Cell::AlertSpeciesOfAttack");
    a_React = H->sym("Simulator::Cell::ReactToAttacker");
    a_Rest = H->sym("Simulator::Cell::RestAfterBite");
    a_Soft = H->sym("Simulator::Cell::SoftBodyStep");
    a_ChunkA = H->sym("Simulator::Cell::EatChunkA");
    a_ChunkB = H->sym("Simulator::Cell::EatChunkB");
    a_Bite = H->sym("Simulator::Cell::Bite");
    a_SpawnLoot = H->sym("Simulator::Cell::SpawnLoot");
    a_EatFood = H->sym("Simulator::Cell::EatFood");
    a_RefCreate = H->sym("Simulator::Cell::cCellDataReference_::Create");
    a_Serializer = H->sym("Simulator::Cell::cCellSerializer::_ptr");
    a_S = H->sym("Simulator::Cell::cCellSerializableData::_ptr");
    g_ok = a_DamageCell && a_KillCell && a_Break && a_Destroy && a_AddFood && a_Populate && a_RefCreate &&
           a_Serializer && a_S;
    if (!g_ok) {
        H->log(M, SK_WARN, "cellsync: symbols missing, cell co-op disabled");
        return false;
    }
    H->hook_create(M, (void*)a_DamageCell, (void*)h_DamageCell, (void**)&o_DamageCell);
    H->hook_create(M, (void*)a_KillCell, (void*)h_KillCell, (void**)&o_KillCell);
    H->hook_create(M, (void*)a_Break, (void*)h_Break, (void**)&o_Break);
    H->hook_create(M, (void*)a_Destroy, (void*)h_Destroy, (void**)&o_Destroy);
    H->hook_create(M, (void*)a_AddFood, (void*)h_AddFood, (void**)&o_AddFood);
    H->hook_create(M, (void*)a_Populate, (void*)h_Populate, (void**)&o_Populate);
    if (a_Pair)
        H->hook_create(M, (void*)a_Pair, (void*)h_Pair, (void**)&o_Pair);
    if (a_Wipe)
        H->hook_create(M, (void*)a_Wipe, (void*)h_Wipe, (void**)&o_Wipe);
    if (a_NoAttack)
        H->hook_create(M, (void*)a_NoAttack, (void*)h_NoAttack, (void**)&o_NoAttack);
    if (a_SpawnLoot)
        H->hook_create(M, (void*)a_SpawnLoot, (void*)h_SpawnLoot, (void**)&o_SpawnLoot);
    if (a_Soft)
        H->hook_create(M, (void*)a_Soft, (void*)h_Soft, (void**)&o_Soft);
    if (a_Bite)
        H->hook_create(M, (void*)a_Bite, (void*)h_Bite, (void**)&o_Bite);
    if (a_ChunkA && a_ChunkB) {
        H->hook_create(M, (void*)a_ChunkA, (void*)h_ChunkA, (void**)&o_ChunkA);
        H->hook_create(M, (void*)a_ChunkB, (void*)h_ChunkB, (void**)&o_ChunkB);
    }
    return true;
}

// ---------------- host ----------------
static void host_tick(double now)
{
    uint8_t* s = S();
    if (s && now - g_last_food > 0.5) {
        g_last_food = now;
        int32_t food = F<int32_t>(s, 0x1C), plant = F<int32_t>(s, 0x20);
        bool changed = food != g_food_sent || plant != g_plant_sent;
        // Each guest gets the total with the last of its meals in it (BUG-0051), when it changed, and
        // again every 2 s: a guest drops it while it loads or is in an editor, and a newcomer had none
        // until the total changed (BUG-0128). Old (`fix resend 0`): on a change of the total only.
        for (auto& pl : C.sess->players()) {
            if (pl.id == 0)
                continue;
            FoodTx& tx = g_food_tx[pl.id];
            uint32_t ack = g_food_ack.count(pl.id) ? g_food_ack[pl.id] : 0;
            bool due = g_fix_resend ? food != tx.food || plant != tx.plant || ack != tx.ack || now - tx.at > 2.0
                                    : changed || ack != tx.ack;
            if (!due)
                continue;
            tx = {food, plant, ack, now};
            net::Writer w;
            w.u32((uint32_t)food);
            w.u32((uint32_t)plant);
            w.u32(ack); // older guests read the two totals only
            C.sess->send_reliable_to(pl.id, net::MSG_CELL_FOOD, w);
        }
        g_food_sent = food;
        g_plant_sent = plant;
    }
    if (now - g_last_send < 0.1)
        return;
    g_last_send = now;
    uint8_t* av = avatar();
    if (!av)
        return;
    net::Vec3 me = pos_of(av);
    struct E {
        uint8_t* c;
        uint32_t idx;
    };
    std::vector<E> cells;
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        if (modes::cell_is_puppet(c) || !F<void*>(c, kRes) || net::dist(pos_of(c), me) > 120.f)
            return;
        cells.push_back({c, idx});
    });
    g_cycle++;
    size_t per = 14, chunks = std::max<size_t>(1, (cells.size() + per - 1) / per);
    for (size_t k = 0; k < chunks; k++) {
        net::Writer w;
        w.u16(g_cycle);
        w.u8((uint8_t)k);
        w.u8((uint8_t)chunks);
        size_t from = k * per, to = std::min(cells.size(), from + per);
        w.u8((uint8_t)(to - from));
        for (size_t i = from; i < to; i++) {
            uint8_t* c = cells[i].c;
            void* ref = F<void*>(c, kRes);
            uint32_t inst = ref ? *(uint32_t*)ref : 0;
            auto* key = (uint32_t*)(c + kModelKey);
            net::Vec3 p = pos_of(c);
            auto* v = (float*)(c + kVel);
            net::Quat q;
            modes::mat_to_quat((float*)(c + kRot), q);
            w.u32(cells[i].idx);
            w.u32(inst);
            w.u32(key[0]); w.u32(key[1]); w.u32(key[2]);
            w.u8((uint8_t)F<int>(c, kLevel));
            w.f32(F<float>(c, kSize));
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.f32(v[0]); w.f32(v[1]);
            w.f32(q.x); w.f32(q.y); w.f32(q.z); w.f32(q.w);
            w.u8(cell_logic::hp_byte(F<int>(c, kHp))); // a negative hp is 0, not 255 (BUG-0160)
            w.u8((uint8_t)((F<uint8_t>(c, kDying) ? 1 : 0) | (F<uint8_t>(c, kDead) ? 2 : 0)));
        }
        for (auto& pl : C.sess->players())
            if (pl.id != 0)
                C.sess->send_unreliable_to(pl.id, net::MSG_CELL_STATE, w);
    }
}

// ---------------- guest ----------------
static void* ref_for(uint32_t inst)
{
    return inst ? skg::cdecl_call<void*>(a_RefCreate, inst, (void*)a_Serializer) : nullptr;
}

static void destroy_local(uint32_t idx)
{
    g_our_destroy = true;
    modes::cell_destroy(idx);
    g_our_destroy = false;
}

static void guest_state(net::Reader& r)
{
    uint16_t cycle = r.u16();
    uint8_t chunk = r.u8(), chunks = r.u8(), n = r.u8();
    if (!r.ok())
        return;
    switch (g_rounds.on_chunk(cycle, chunk, g_fix_purge)) {
    case cell_logic::Rounds::OLD_CHUNK: // overtaken by a newer round: it would start that one again
        g_late_chunks++;
        return;
    case cell_logic::Rounds::NEW_ROUND:
        g_cycle_seen.clear();
        break;
    default:
        break;
    }
    double now = H->time_seconds();
    for (int i = 0; i < n && r.ok(); i++) {
        uint32_t hidx = r.u32(), inst = r.u32(), key[3];
        key[0] = r.u32(); key[1] = r.u32(); key[2] = r.u32();
        int level = r.u8();
        float size = r.f32();
        net::Vec3 p, v;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        v.x = r.f32(); v.y = r.f32(); v.z = 0;
        net::Quat q;
        q.x = r.f32(); q.y = r.f32(); q.z = r.f32(); q.w = r.f32();
        int hp = r.u8();
        uint8_t flags = r.u8();
        if (!r.ok())
            return;
        g_cycle_seen.insert(hidx);
        auto& m = g_mirrors[hidx];
        m.pos = p; m.vel = v; m.rot = q; m.size = size; m.hp = hp; m.seen = now;
        uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr;
        if (!c) { // dead ones too: the host shows a creature it killed until it is gone
            uint32_t idx = modes::cell_create(p, ref_for(inst), level, key, &q);
            c = modes::cell_at(idx);
            if (c) {
                if (m.local && g_fix_hostof) // the old local cell is gone (BUG-0088)
                    g_host_of.erase(m.local);
                m.local = idx;
                g_host_of[idx] = hidx;
                g_created++;
            }
        }
        if (c) {
            F<uint8_t>(c, kAiLock) = 1;    // passive: no behaviour, the host decides
            F<uint8_t>(c, kKeepAlive) = 1; // we own the lifetime, not the tile streaming
            F<float>(c, kSize) = size;
            F<float>(c, kTargetSize) = size;
            F<int>(c, kHp) = hp;
            if ((flags & 3) && !F<uint8_t>(c, kDying) && !F<uint8_t>(c, kDead)) {
                // the host's creature died: the same death here (effect, dying / dead animation,
                // locks, event 6 -> dead). Its loot is the host's (SpawnLoot is off on the guest).
                g_replay = true;
                o_KillCell(c, 0, nullptr, false, 6, 0.f);
                g_replay = false;
                g_death_replays++;
            }
        }
    }
    if (chunk + 1 < chunks)
        return;
    // The host no longer has these: remove the mirrors. Only after a round of which every chunk
    // arrived (the state is unreliable): a lost or late chunk deleted its 14 cells here, and they
    // came back 0.1 s later as new cells (BUG-0032). Mirrors not listed for a second go anyway.
    bool complete = g_rounds.complete(chunks, g_fix_purge);
    if (!complete)
        g_rounds_partial++;
    for (auto it = g_mirrors.begin(); it != g_mirrors.end();) {
        if (complete ? !g_cycle_seen.count(it->first) : now - it->second.seen > 1.0) {
            if (it->second.local) {
                g_host_of.erase(it->second.local);
                destroy_local(it->second.local);
                g_destroyed++;
            }
            it = g_mirrors.erase(it);
        } else
            ++it;
    }
    // stray local gameplay cells (spawned before the sync started, loot, ...) do not exist on the host.
    // Not the guest's own mate (G+0x51D4, spawned by its mating call 0xE760F0): it was removed here
    // within a second, so a guest could never reach it and open its own editor.
    std::vector<uint32_t> stray;
    uint32_t mate = mate_idx();
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        if (!g_host_of.count(idx) && !modes::cell_is_puppet(c) && idx != mate)
            stray.push_back(idx);
    });
    for (uint32_t idx : stray) {
        destroy_local(idx);
        g_purged++;
    }
}

static void guest_tick(double now)
{
    flush_food();
    for (auto& [hidx, m] : g_mirrors) {
        uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr;
        if (!c)
            continue;
        double age = std::min(now - m.seen, 0.5);
        net::Vec3 want{m.pos.x + m.vel.x * (float)age, m.pos.y + m.vel.y * (float)age, m.pos.z};
        net::Vec3 cur = pos_of(c);
        int n = F<int>(c, 0x250);
        if (F<uint32_t>(c, 0x248) && n > 1) {
            // a soft body (docs/re/cell.md 6): +0x4C and +0x90 are the mean of its particles, writes
            // to them alone are undone, and a centre written away from its particles blew the body
            // up (mirrors flew off to 1e10 m on the guest). Move the particles with it and keep them
            // still: the pose is the host's, extrapolated along its velocity.
            modes::cell_move(c, want, true);
            auto* acc = (float*)(c + 0x1C4);
            acc[0] = acc[1] = acc[2] = 0;
            g_soft_moves++;
        } else {
            if (net::dist(cur, want) > 3.f)
                set_pos(c, want);
            auto* v = (float*)(c + kVel);
            v[0] = m.vel.x + (want.x - cur.x) * 2.f;
            v[1] = m.vel.y + (want.y - cur.y) * 2.f;
        }
        modes::quat_to_mat(m.rot, (float*)(c + kRot));
        F<uint16_t>(c, kFlags) |= 2;
        F<uint8_t>(c, kAiLock) = 1;
    }
}

// ---------------- both ----------------
static void symbiosis_tick(double now)
{
    uint8_t* a = avatar();
    double dt = g_last_tick > 0 ? std::min(now - g_last_tick, 0.5) : 0;
    g_last_tick = now;
    if (!a)
        return;
    if (g_body < 1.f && std::fabs(F<float>(a, kTargetSize) - g_body_size) > g_body_size * 0.01f)
        g_body = 1.f; // the game resized the avatar (level change): full size again
    if (C.puppets) // a partner's cell replaced by a new one (the partner blinked out and in)
        for (auto& p : C.puppets()) {
            void*& last = g_puppet_seen[p.id];
            if (last && last != p.cell)
                g_puppet_new++;
            last = p.cell;
        }
    ko_expire(now);
    for (auto& [id, k] : g_ko) {
        uint8_t* c = k.state == KO_OUT || k.fx_at > 0 ? puppet_cell(id) : nullptr;
        if (k.state == KO_OUT && c)
            F<float>(c, kOpacity) = kKoOpacity;
        if (k.fx_at > 0 && now >= k.fx_at) {
            k.fx_at = 0;
            effect(c, "cell_egg_L0_autohatch");
        }
    }
    if (g_downed) {
        downed_tick(a, now, dt);
        return;
    }
    if (g_invuln_until > 0 && now > g_invuln_until) {
        F<uint8_t>(a, kInvuln) = 0;
        g_invuln_until = 0;
    }
    if (now - g_last_regen < 3.0)
        return;
    g_last_regen = now;
    int hp = F<int>(a, kHp);
    if (hp > 0 && hp < 6 && !nearest_partner(12.f).empty()) {
        F<int>(a, kHp) = hp + 1;
        if (a_Rollover && F<uint32_t>(a, 0x248))
            skg::cdecl_call<void>(a_Rollover, a, hp);
    }
}

// the host forwards a bite of its NPC on our puppet: apply it to our own cell
static void hurt_avatar(int dmg, int type)
{
    uint8_t* a = avatar();
    if (!a || dmg <= 0 || F<uint8_t>(a, kInvuln) || F<uint8_t>(a, kDying))
        return;
    int old = F<int>(a, kHp), hp = old - dmg;
    g_bites_taken++;
    g_damage_taken += std::min(old, dmg);
    if (hp > 0) {
        F<int>(a, kHp) = hp;
        if (a_Rollover && F<uint32_t>(a, 0x248))
            skg::cdecl_call<void>(a_Rollover, a, old);
        return;
    }
    F<int>(a, kHp) = 0;
    h_KillCell(a, avatar_idx(), F<void*>(a, kRes), false, type, 0.f); // the pack may save us
}

// The mirrors live in the cell pool: once the stage is left (the editor's Creature switch, a
// save loaded) their indices mean nothing and the pool may be freed. A player report (0.7.2.1,
// 2026-10-25): the guest faulted in cell_at on session end in the Creature stage, destroying
// mirrors the Cell stage had left behind.
static void forget_mirrors()
{
    g_mirrors.clear();
    g_host_of.clear();
    g_touched.clear();
    g_puppet_seen.clear();
    g_rounds.reset();
}

// BUG-0070: the knockout ends with the session and with the stage. Before, a session ending within
// the 2-3 s after a revive or respawn left the cell invulnerable for good, one ending during a
// knockout left it translucent at 1 hp, and a knockout across a stage change moved the next Cell
// game's avatar to the old place and respawned it small.
static void reset_knockout()
{
    if (!g_fix_koreset)
        return;
    // BUG-0238: the partners keep our knockout up to its deadline now; tell them it ended (KO_UP is
    // state 0, which every version handles). On a session end the session is closed: nothing goes out.
    if (g_downed && g_fix_ko_grace)
        send_ko(KO_UP, 0, 0);
    if (uint8_t* a = in_cell() ? avatar() : nullptr) {
        if (g_downed || g_invuln_until > 0)
            F<uint8_t>(a, kInvuln) = 0;
        if (g_downed) {
            F<float>(a, kOpacity) = 1.f;
            F<int>(a, kHp) = std::max(F<int>(a, kHp), 1);
        }
    }
    g_downed = false;
    g_hold = 0;
    g_down_since = 0;
    g_invuln_until = 0;
    g_ko.clear();
}

void on_player_leave(int id)
{
    g_food_ack.erase(id);
    g_food_tx.erase(id);
    if (!g_fix_kostale)
        return;
    // its knockout ends with it: a newcomer gets the same id (BUG-0061)
    if (auto it = g_ko.find(id); it != g_ko.end()) {
        if (uint8_t* c = it->second.state == KO_OUT && in_cell() ? puppet_cell(id) : nullptr)
            F<float>(c, kOpacity) = 1.f;
        g_ko.erase(it);
    }
    g_puppet_seen.erase(id);
}

void tick(double now)
{
    static bool was_in_cell;
    bool cell = in_cell();
    if (!cell && (was_in_cell || !g_mirrors.empty()) && g_fix_forget) {
        if (was_in_cell)
            reset_knockout(); // no avatar any more: only our state is reset
        forget_mirrors();
        g_loot_recent.clear();
        modes::cell_forget(); // the partners' puppets were in the same pool
    }
    was_in_cell = cell;
    if (!in_cell() || !C.sess || !C.sess->connected() || !avatar())
        return;
    if (is_host())
        host_tick(now);
    else if (is_guest())
        guest_tick(now);
    symbiosis_tick(now);
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    // a knockout state is kept (and relayed) in any stage: an "up again" dropped while we were in an
    // editor or loading left the partner knocked out here (BUG-0061)
    bool cell = in_cell() && avatar();
    if (!cell && !(type == net::MSG_CELL_KO && g_fix_kostale))
        return;
    if (type == net::MSG_CELL_HIT && is_guest()) {
        uint32_t idx = r.u32();
        int dmg = r.u8(), t = r.u8();
        if (r.ok() && idx == 0xFFFFFFFFu) {
            g_hurt_fwd++;
            hurt_avatar(dmg, t);
        }
        return;
    }
    if (type == net::MSG_CELL_KO) {
        if (g_kodrop_until > 0 && H->time_seconds() < g_kodrop_until)
            return; // tests: a refresh that comes late (coop.pool kodrop)
        uint8_t id = r.u8(), state = r.u8(), left = r.u8(), hold = r.u8();
        if (!r.ok() || (is_host() && id != from))
            return; // a guest tells only about its own cell
        if (is_host() && from != 0) { // relay to the other guests
            net::Writer w;
            w.u8(id); w.u8(state); w.u8(left); w.u8(hold);
            C.sess->broadcast_reliable(net::MSG_CELL_KO, w, from);
        }
        if (id != C.sess->my_id())
            on_ko(id, state, left / 10.f, hold / 255.f);
        return;
    }
    if (!cell)
        return;
    if (type == net::MSG_CELL_STATE && is_guest()) {
        guest_state(r);
    } else if (type == net::MSG_CELL_HIT && is_host()) {
        uint32_t idx = r.u32();
        int dmg = r.u8(), t = r.u8();
        uint8_t* c = modes::cell_at(idx);
        // a bite lands on an NPC of our pool: never on a player's cell (ours or a puppet)
        if (!r.ok() || !c || F<uint8_t>(c, kDying) || c == avatar() || modes::cell_is_puppet(c))
            return;
        g_hits_in++;
        uint8_t* pc = puppet_cell(from);
        // BUG-0078: judged as DamageCell judges the host's own bite (0xE7A7C0): nothing happens when
        // the victim is not damageable (dead, hp 0), ShouldNotAttack(victim, attacker) is true (same
        // species, friend group, the species' "won't attack the player" flags: through the hook, so
        // the guest's puppet counts as the player) or the victim has +0x17C / +0x111 set. A mouth's
        // chunk (type 6) is EatChunk's, which asks none of these. Old (`fix guestbite 0`): the
        // arguments swapped, no gate.
        if (g_fix_guestbite && t != 6 &&
            (F<uint8_t>(c, kDead) == 1 || F<int>(c, kHp) == 0 || F<uint8_t>(c, 0x17C) || F<uint8_t>(c, kInvuln) ||
             (pc && o_NoAttack && h_NoAttack(c, pc)))) {
            g_hits_spared++;
            return;
        }
        // the NPC and its kind turn on the guest who bit it, as DamageCell does for the host's bites
        // (species alert 0xE71CE0, then the victim's own reaction: only AI type 0x1006 reacts,
        // through 0xE57710 -> 0xE57610), so biting is as risky for the guest as for the host
        if (pc && a_Alert && o_NoAttack && (g_fix_guestbite ? !h_NoAttack(c, pc) : !o_NoAttack(pc, c))) {
            uint32_t pidx = F<uint32_t>(pc, 0);
            skg::cdecl_call<void>(a_Alert, c, pidx);
            if (a_React && ai_type(c) == 0x1006)
                skg::cdecl_call<void>(a_React, c, pidx);
            g_hits_aggro++;
        }
        int hp = F<int>(c, kHp) - dmg;
        if (hp > 0) {
            F<int>(c, kHp) = hp;
        } else {
            F<int>(c, kHp) = 0;
            h_KillCell(c, avatar_idx(), F<void*>(avatar(), kRes), true, t, 0.f); // counts as a player kill
        }
    } else if (type == net::MSG_CELL_BITTEN && is_host()) {
        // our NPC bit a guest on the guest's machine: what DamageCell does for the attacker after a
        // bite on our own cell (0xE57930: AI type 0x1006 rests, its chase ends) - otherwise it
        // hunted the guest's puppet without a pause (docs/re/cell.md 9)
        uint8_t* c = modes::cell_at(r.u32());
        if (!r.ok() || !c || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying))
            return;
        g_bitten_in++;
        if (a_Rest && ai_type(c) == 0x1006) {
            void* holder = nullptr;
            uint8_t* res = skg::cdecl_call<uint8_t*>(a_GetData, F<void*>(c, kRes), &holder);
            if (holder)
                --F<int>(holder, 0xC);
            if (res)
                skg::cdecl_call<void>(a_Rest, c, res);
        }
    } else if (type == net::MSG_CELL_EAT && is_host()) {
        uint32_t idx = r.u32();
        if (!r.ok())
            return;
        uint8_t kind = r.u8(), arg = r.u8();
        int food = (int)r.u32();
        bool plant = r.u8() != 0;
        if (!r.ok()) // an older guest: the index only
            kind = ACT_GONE, arg = 0, food = 0, plant = false;
        uint32_t meal = r.u32();
        if (r.ok()) // applied (or refused, eaten first) now: its total goes back with this number
            g_food_ack[from] = meal;
        if (food <= -1000 || food >= 1000)
            food = 0;
        uint8_t* c = modes::cell_at(idx);
        if (c && (modes::cell_is_puppet(c) || c == avatar()))
            c = nullptr;
        uint8_t* pc = puppet_cell(from);
        if (kind == ACT_GONE) {
            if (c) {
                g_eats_in++;
                o_Destroy(idx, arg & 1, 0.f, 0); // with the loot the game asked for on the guest
                if (food)
                    o_AddFood(food, plant);
            } else if (food) {
                // somebody ate it first: no points for it. The guest already counted them: our total
                // goes out again and takes them back (MSG_CELL_FOOD -> its AddFoodPoints(diff))
                g_eats_late++;
                g_food_sent = -1;
            }
        } else if (c && !F<uint8_t>(c, kDying) && !F<uint8_t>(c, kDead)) {
            // the guest's bite killed it: the kill happens here, with the death and the loot
            g_kills_in++;
            if (kind == ACT_BREAK) { // the lethal bite: also a hit that arrived (hits_in)
                g_hits_in++;
                F<int>(c, kHp) = 0;
                h_Break(c, pc ? pc : avatar(), 1.f / 30.f);
            } else {
                F<int>(c, kHp) = 0;
                h_KillCell(c, pc ? F<uint32_t>(pc, 0) : avatar_idx(), pc ? F<void*>(pc, kRes) : nullptr, true, arg, 0.f);
            }
        }
    } else if (type == net::MSG_CELL_FOOD) {
        if (is_host()) { // a guest ate something: add it to the shared progress
            int amount = (int)r.u32();
            bool plant = r.u8() != 0;
            if (r.ok() && amount > -1000 && amount < 1000)
                o_AddFood(amount, plant);
            uint32_t meal = r.u32();
            if (r.ok())
                g_food_ack[from] = meal;
        } else if (is_guest()) {
            int food = (int)r.u32(), plant = (int)r.u32();
            uint8_t* s = S();
            if (!r.ok() || !s)
                return;
            uint32_t ack = r.u32(); // a host before 0.8 does not send it
            int diff = g_meals.diff(food, r.ok(), ack, g_pend_food, F<int32_t>(s, 0x1C), g_fix_foodseq);
            if (diff != 0) {
                g_food_guard = true;
                o_AddFood(diff, false); // through the game: level ups happen as usual
                g_food_guard = false;
            }
            F<int32_t>(s, 0x20) = plant;
        }
    }
}

void on_session_end()
{
    g_kodrop_until = 0;
    if (g_fix_koreset)
        reset_knockout();
    g_ko.clear();
    if (g_downed) {
        g_downed = false;
        if (uint8_t* a = in_cell() ? avatar() : nullptr)
            F<uint8_t>(a, kInvuln) = 0;
    }
    g_meals.reset();
    g_food_ack.clear();
    g_food_tx.clear();
    if (in_cell() || !g_fix_forget) // outside the Cell stage the indices point into a pool that is gone
        for (auto& [h, m] : g_mirrors)
            if (m.local)
                destroy_local(m.local);
    forget_mirrors();
    g_food_sent = g_plant_sent = -1;
    g_pend_food = 0;
}

// ---- overlay: knockout countdown, revive progress, arrows to knocked out partners ----
static std::string bar(float f)
{
    int n = (int)(std::max(0.f, std::min(f, 1.f)) * 8.f + 0.5f);
    std::string s;
    for (int i = 0; i < 8; i++)
        s += i < n ? "\u25A0" : "\u25A1"; // filled / empty square
    return s;
}

static int text_w(const std::string& s) { return utf8::overlay_width(s); } // cells, not bytes (BUG-0159)

static void draw_ko(uint8_t* c, const std::string& name, float left, float hold, double now, int sw, int sh)
{
    if (!c)
        return;
    net::Vec3 p = pos_of(c);
    float size = std::max(F<float>(c, kSize), 0.3f);
    float w[3] = {p.x, p.y + size * 2.2f + 1.f, p.z}, sc[2];
    bool blink = std::fmod(now, 0.8) < 0.5;
    char t[64];
    snprintf(t, sizeof t, "KO %d", (int)std::ceil(std::max(0.f, left)));
    uint32_t red = blink ? 0xFFFF4040 : 0xFFB02020;
    bool on = H->world_to_screen(w, sc) && sc[0] >= 0 && sc[1] >= 0 && sc[0] < sw && sc[1] < sh;
    if (on) {
        H->overlay_text((int)sc[0] - text_w(t) / 2, (int)sc[1] - 34, red, t);
        if (hold > 0.01f) {
            std::string b = bar(hold);
            H->overlay_text((int)sc[0] - 40, (int)sc[1] + 18, 0xFF60FF60, b.c_str());
        }
        return;
    }
    if (name.empty() || sw <= 0)
        return;
    // off screen: an arrow on the screen edge towards it
    float cx = sw / 2.f, cy = sh / 2.f;
    if (!H->world_to_screen(w, sc)) // behind: never happens in the flat cell world, keep a fallback
        sc[0] = cx, sc[1] = sh + 100.f;
    float dx = sc[0] - cx, dy = sc[1] - cy;
    float fx, fy;
    if (!cell_logic::edge_point(cx, cy, dx, dy, 70, 50, 1, 1, fx, fy)) // a NaN position (BUG-0160)
        return;
    int x = (int)fx, y = (int)fy;
    const char* arrow = std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? "\u25BA" : "\u25C4") : (dy > 0 ? "\u25BC" : "\u25B2");
    std::string s = std::string(arrow) + " " + name + " " + t;
    H->overlay_text(x - text_w(s) / 2, y - 10, red, s.c_str());
}

void draw_overlay()
{
    if (!in_cell() || !C.sess || !C.sess->connected())
        return;
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    double now = H->time_seconds();
    if (g_downed)
        draw_ko(avatar(), "", (float)std::max(0.0, kDownTime - (now - g_down_since)), (float)(g_hold / kReviveHold), now,
                sw, sh);
    if (!C.puppets)
        return;
    for (auto& p : C.puppets()) {
        auto it = g_ko.find(p.id);
        if (it == g_ko.end() || it->second.state != KO_OUT)
            continue;
        draw_ko((uint8_t*)p.cell, p.name, it->second.left, it->second.hold, now, sw, sh);
    }
}

// ---- debug (tests) ----
// what a player sees: the kind of a cell from its resource (docs/re/cell.md 1): cellType +0xB4,
// ai.type (first int of the AI block by difficulty)
static const char* kind_of(uint8_t* c)
{
    void* ref = F<void*>(c, kRes);
    if (!ref || !a_GetData)
        return "?";
    void* holder = nullptr;
    uint8_t* res = skg::cdecl_call<uint8_t*>(a_GetData, ref, &holder);
    if (holder)
        --F<int>(holder, 0xC);
    if (!res)
        return "?";
    int type = F<int>(res, 0xB4);
    int d = S() ? F<int>(S(), 0x7C) : 1;
    uint8_t* ai = res + 0xE0;
    if (d == 0 && F<int>(res, 0x248) != -1)
        ai = res + 0x248;
    else if (d == 2 && F<int>(res, 0x194) != -1)
        ai = res + 0x194;
    switch (type) {
    case 1: return "rock";
    case 3: return "plant";
    case 4: return "bubble";
    case 5: return "poison";
    case 6: return "part";
    case 7: return "nanite";
    case 2: return F<int>(ai, 0) != 0 ? "creature" : "meat";
    default: return "other";
    }
}

// on-screen gameplay cells: key kind size_ratio hp distance screen_x screen_y (the nearest first)
static std::string see(int max)
{
    uint8_t* av = avatar();
    net::Vec3 me = pos_of(av);
    float my = std::max(F<float>(av, kSize), 0.01f);
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    struct Seen {
        uint32_t key;
        const char* kind;
        float ratio, d;
        int hp, x, y;
    };
    std::vector<Seen> v;
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        if (modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying))
            return;
        uint32_t key = idx;
        if (is_guest()) {
            auto it = g_host_of.find(idx);
            if (it == g_host_of.end())
                return;
            key = it->second;
        }
        net::Vec3 p = pos_of(c);
        float w[3] = {p.x, p.y, p.z}, sc[2];
        if (!H->world_to_screen(w, sc) || sc[0] < 0 || sc[1] < 0 || sc[0] >= sw || sc[1] >= sh)
            return;
        v.push_back({key, kind_of(c), F<float>(c, kSize) / my, net::dist(p, me), F<int>(c, kHp), (int)sc[0], (int)sc[1]});
    });
    std::sort(v.begin(), v.end(), [](const Seen& a, const Seen& b) { return a.d < b.d; });
    std::string out;
    char b[128];
    float w[3] = {me.x, me.y, me.z}, sc[2] = {-1, -1};
    H->world_to_screen(w, sc);
    snprintf(b, sizeof b, "me %d %d hp=%d screen=%dx%d\n", (int)sc[0], (int)sc[1], F<int>(av, kHp), sw, sh);
    out += b;
    for (size_t i = 0; i < v.size() && (int)i < max; i++) {
        snprintf(b, sizeof b, "see c%u %s %.2f %d %.1f %d %d\n", v[i].key, v[i].kind, v[i].ratio, v[i].hp, v[i].d, v[i].x,
                 v[i].y);
        out += b;
    }
    return out;
}

// the NPCs within r of a player: what the game's own target tests say about each player
// (docs/re/cell.md 9): prey (chase) / threat (flee) and the current chase target
static std::string prey_report(float r, bool rows)
{
    uint8_t* av = avatar();
    if (!a_TargetParams || !a_IsPrey || !a_IsThreat || !a_GetData)
        return "symbols missing";
    struct Pl {
        std::string name;
        uint8_t* c;
        // NPCs within r / in their prey range and IsPrey / in their threat range and IsThreat / chasing it
        int around = 0, prey = 0, threat = 0, chasing = 0;
    };
    std::vector<Pl> players{{"me", av}};
    if (C.puppets)
        for (auto& p : C.puppets())
            if (p.cell)
                players.push_back({p.name, (uint8_t*)p.cell});
    std::string out;
    char b[256];
    for (auto& pl : players) {
        uint8_t* pc = pl.c;
        // scale difference me -> player (2 = same level)
        int sd = a_ScaleDiff ? skg::cdecl_call<int>(a_ScaleDiff, av, pc) : -1;
        snprintf(b, sizeof b, "player %s idx=%u level=%d scale_diff=%d lock189=%d dead=%d invuln=%d size=%.2f pos=(%.1f %.1f)\n",
                 pl.name.c_str(), F<uint32_t>(pc, 0), F<int>(pc, kLevel), sd, F<uint8_t>(pc, 0x189), F<uint8_t>(pc, kDead),
                 F<uint8_t>(pc, kInvuln), F<float>(pc, kSize), pos_of(pc).x, pos_of(pc).y);
        if (rows)
            out += b;
    }
    int n = 0, compared = 0, differ = 0; // NPCs whose answers for a partner differ from those for us
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        if (modes::cell_is_puppet(c) || strcmp(kind_of(c), "creature") != 0)
            return;
        float best = 1e9f;
        for (auto& pl : players)
            best = std::min(best, net::dist(pos_of(c), pos_of(pl.c)));
        if (best > r)
            return;
        void* holder = nullptr;
        uint8_t* res = skg::cdecl_call<uint8_t*>(a_GetData, F<void*>(c, kRes), &holder);
        if (holder)
            --F<int>(holder, 0xC);
        if (!res)
            return;
        alignas(4) uint8_t params[32] = {};
        skg::cdecl_call<void>(a_TargetParams, c, res, params);
        snprintf(b, sizeof b, "npc %u level=%d chase=%u flee=%u ranges=%.1f/%.1f", idx, F<int>(c, kLevel),
                 F<uint32_t>(c, kChase), F<uint32_t>(c, kFlee), F<float>(params, 0), F<float>(params, 4));
        std::string row = b;
        int mine = -1;
        for (auto& pl : players) {
            bool prey = skg::cdecl_call<bool>(a_IsPrey, c, pl.c, params);
            bool threat = skg::cdecl_call<bool>(a_IsThreat, c, pl.c, params);
            int ans = prey * 2 + threat;
            if (mine < 0)
                mine = ans;
            else {
                compared++;
                differ += ans != mine;
            }
            float d = net::dist(pos_of(c), pos_of(pl.c));
            pl.around += d <= r;
            pl.prey += prey && d <= F<float>(params, 0);
            pl.threat += threat && d <= F<float>(params, 4);
            pl.chasing += F<uint32_t>(c, kChase) == F<uint32_t>(pl.c, 0);
            snprintf(b, sizeof b, " | %s d=%.1f prey=%d threat=%d", pl.name.c_str(), d, (int)prey, (int)threat);
            row += b;
        }
        if (rows && n++ < 40)
            out += row + "\n";
    });
    snprintf(b, sizeof b, "prey_rules compared=%d differ=%d\n", compared, differ);
    out += b;
    for (size_t i = 0; i < players.size(); i++) {
        snprintf(b, sizeof b, "prey_sum %s around=%d prey=%d threat=%d chasing=%d\n", i ? "partner" : "me", players[i].around,
                 players[i].prey, players[i].threat, players[i].chasing);
        out += b;
    }
    return out;
}

std::string debug(const std::vector<std::string>& a)
{
    if (!a.empty() && a[0] == "fix") { // any stage (resend also covers the DNA total) // fix [<name>|all 0|1]: the wave 2 fixes, off = the old behaviour
        struct Sw {
            const char* name;
            bool* on;
        } sw[] = {{"purge", &g_fix_purge},     {"hostof", &g_fix_hostof},   {"foodseq", &g_fix_foodseq},
                  {"resend", &g_fix_resend},   {"kostale", &g_fix_kostale}, {"koreset", &g_fix_koreset},
                  {"bodylevel", &g_fix_bodylevel}, {"cellat", &modes::cell_at_fix},
                  {"guestbite", &g_fix_guestbite}, {"rescue", &g_fix_rescue}};
        if (a.size() >= 3) {
            if (a[2] != "0" && a[2] != "1")
                return "usage: coop.pool fix [<name>|all 0|1]";
            bool found = false;
            for (auto& x : sw)
                if (a[1] == "all" || a[1] == x.name)
                    *x.on = a[2] == "1", found = true;
            if (!found)
                return "no such fix: " + a[1];
        }
        std::string o = "fixes:";
        for (auto& x : sw)
            o += std::string(" ") + x.name + "=" + (*x.on ? "1" : "0");
        return o;
    }
    uint8_t* av = in_cell() ? avatar() : nullptr; // G() outlives the stage, its pool does not
    if (!av)
        return "not in the cell stage";
    if (a.size() >= 2 && a[0] == "zap") { // guest: the game removes <n> mirrors' cells (not our removal path)
        int n = atoi(a[1].c_str()), done = 0;
        for (auto& [h, m] : g_mirrors)
            if (done < n && m.local && modes::cell_at(m.local)) {
                destroy_local(m.local); // the mirror and g_host_of keep it: the next state recreates it
                done++;
            }
        return "zapped " + std::to_string(done);
    }
    if (!a.empty() && a[0] == "mirrormap") { // guest: host index -> local pool index of every live mirror
        std::string o;
        for (auto& [h, m] : g_mirrors)
            if (m.local && modes::cell_at(m.local))
                o += "map " + std::to_string(h) + " " + std::to_string(m.local) + "\n";
        return o.empty() ? "no live mirrors\n" : o;
    }
    if (!a.empty() && a[0] == "small") { // our cell as after a small respawn (body 0.5)
        set_body(av, kSmall);
        return "small";
    }
    if (a.size() >= 2 && a[0] == "foodlocal") { // our total only, as a total the host sent and we missed
        g_food_guard = true;
        o_AddFood(atoi(a[1].c_str()), false);
        g_food_guard = false;
        return "local food changed";
    }
    if (a.size() >= 2 && a[0] == "leavefix") {
        if (a[1] != "0" && a[1] != "1")
            return "usage: coop.pool leavefix 0|1";
        g_leave_fix = a[1] == "1";
        return g_leave_fix ? "leave fix on" : "leave fix off";
    }
    if (!a.empty() && a[0] == "own") {
        char b[100];
        snprintf(b, sizeof b, "avatar=%u mate=%u mate_alive=%d", avatar_idx(), mate_idx(),
                 modes::cell_at(mate_idx()) ? 1 : 0);
        return b;
    }
    if (!a.empty() && a[0] == "why") { // which game code destroyed mirrors on this guest
        std::string o;
        char b[64];
        for (auto& [ra, n] : g_destroy_callers) {
            if (ra & 0x80000000u)
                snprintf(b, sizeof b, "partner cell destroyed from 0x%08X x%u\n", (unsigned)(ra & 0x7FFFFFFF), n);
            else if (ra & 0x20000000u)
                snprintf(b, sizeof b, "mirror killed (KillCell) from 0x%08X x%u\n", (unsigned)(ra & 0x0FFFFFFF), n);
            else if (ra & 0x10000000u)
                snprintf(b, sizeof b, "mirror broken (BreakOrSwallow) from 0x%08X x%u\n", (unsigned)(ra & 0x0FFFFFFF), n);
            else if (ra & 0x40000000u)
                snprintf(b, sizeof b, "reported (touched) from 0x%08X x%u\n", (unsigned)(ra & 0x3FFFFFFF), n);
            else
                snprintf(b, sizeof b, "destroy from 0x%08X x%u\n", (unsigned)(ra - H->rebase(0)), n);
            o += b;
        }
        return o.empty() ? "none" : o;
    }
    if (!a.empty() && a[0] == "mirrors") { // live local pool indices, for checking cleanup after leave
        std::vector<uint32_t> indices;
        for (auto& [host, mirror] : g_mirrors)
            if (mirror.local && modes::cell_at(mirror.local))
                indices.push_back(mirror.local);
        std::sort(indices.begin(), indices.end());
        std::string out;
        for (uint32_t idx : indices)
            out += "mirror_local " + std::to_string(idx) + "\n";
        return out.empty() ? "no live mirrors\n" : out;
    }
    if (a.size() >= 2 && a[0] == "cell") { // a cell by its sync key (host index): local state
        uint32_t key = (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10);
        uint8_t* c = nullptr;
        if (is_guest()) {
            auto it = g_mirrors.find(key);
            c = it != g_mirrors.end() && it->second.local ? modes::cell_at(it->second.local) : nullptr;
        } else
            c = modes::cell_at(key);
        if (!c)
            return "no such cell";
        char b[256];
        auto* v = (float*)(c + kVel);
        snprintf(b, sizeof b, "cell %s %s pos=(%.2f %.2f) vel=(%.2f %.2f) size=%.2f gfx=%u particles=%d hp=%d level=%d "
                 "ailock=%d keepalive=%d\n", a[1].c_str(), kind_of(c), pos_of(c).x, pos_of(c).y, v[0], v[1],
                 F<float>(c, kSize), F<uint32_t>(c, 0x248), F<int>(c, 0x250), F<int>(c, kHp), F<int>(c, kLevel),
                 F<uint8_t>(c, kAiLock), F<uint8_t>(c, kKeepAlive));
        return b;
    }
    if (!a.empty() && a[0] == "see")
        return see(a.size() > 1 ? atoi(a[1].c_str()) : 60);
    if (!a.empty() && a[0] == "prey")
        return prey_report(a.size() > 1 ? (float)atof(a[1].c_str()) : 40.f, a.size() < 3 || a[2] != "sum");
    if (!a.empty() && a[0] == "bite") { // our avatar bites the nearest mirror (through DamageCell)
        uint8_t* best = nullptr;
        float bd = 1e9f;
        for (auto& [h, m] : g_mirrors) {
            uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr;
            if (!c)
                continue;
            float d = net::dist(pos_of(c), pos_of(av));
            if (d < bd) {
                bd = d;
                best = c;
            }
        }
        if (!best)
            return "no mirror";
        float dir[3] = {1, 0, 0};
        h_DamageCell(best, av, dir, 4, 0, 0.f);
        return "bit mirror " + std::to_string(F<uint32_t>(best, 0)) + " at " + std::to_string(bd);
    }
    if (!a.empty() && a[0] == "meat") { // every meat cell (on screen or not): key pos level size, sync key on a guest ('l' = local only)
        std::string o;
        char b[128];
        int n = 0;
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            if (modes::cell_is_puppet(c) || strcmp(kind_of(c), "meat") != 0)
                return;
            auto it = g_host_of.find(idx);
            bool mirror = it != g_host_of.end();
            snprintf(b, sizeof b, "meat %s%u pos=(%.1f %.1f) level=%d size=%.2f dist=%.1f dead=%d\n", is_guest() && !mirror ? "l" : "c",
                     is_guest() && mirror ? it->second : idx, pos_of(c).x, pos_of(c).y, F<int>(c, kLevel), F<float>(c, kSize),
                     net::dist(pos_of(c), pos_of(av)), F<uint8_t>(c, kDead));
            if (n++ < 80)
                o += b;
        });
        return o + "meat_count=" + std::to_string(n) + "\n";
    }
    if (!a.empty() && a[0] == "loot") { // host: the cells the last loot drops created (alive ones): key kind age
        std::string o;
        char b[128];
        double now = H->time_seconds();
        for (auto& [idx, t] : g_loot_recent)
            if (uint8_t* c = modes::cell_at(idx)) {
                snprintf(b, sizeof b, "loot c%u %s age=%.1f pos=(%.1f %.1f)\n", idx, kind_of(c), now - t, pos_of(c).x, pos_of(c).y);
                o += b;
            }
        return o.empty() ? "no loot" : o;
    }
    if (a.size() >= 2 && a[0] == "swimto") { // swim to a cell by its sync key (like a click on it)
        uint32_t key = (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10);
        uint8_t* c = nullptr;
        if (is_guest()) {
            auto it = g_mirrors.find(key);
            c = it != g_mirrors.end() && it->second.local ? modes::cell_at(it->second.local) : nullptr;
        } else
            c = modes::cell_at(key);
        if (!c)
            return "no such cell";
        net::Vec3 p = pos_of(c);
        if (a.size() >= 3 && a[2] == "tp") { // right next to it
            net::Vec3 me = pos_of(av);
            float dx = me.x - p.x, dy = me.y - p.y, l = std::max(0.01f, std::sqrt(dx * dx + dy * dy));
            float r = F<float>(av, kSize) * 0.8f + F<float>(c, kSize);
            stop_at(av, {p.x + dx / l * r, p.y + dy / l * r, 0.f});
        }
        auto* t = (float*)(av + kTarget);
        t[0] = p.x; t[1] = p.y; t[2] = 0;
        F<uint8_t>(av, 0x04) = 0;
        return "swimming to " + a[1];
    }
    if (a.size() >= 2 && a[0] == "eat") { // our avatar eats a cell by its sync key: the game's eat event 0x12 (EatFood)
        uint32_t key = (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10);
        uint8_t* c = nullptr;
        if (is_guest()) {
            auto it = g_mirrors.find(key);
            c = it != g_mirrors.end() && it->second.local ? modes::cell_at(it->second.local) : nullptr;
        } else
            c = modes::cell_at(key);
        if (!c || !a_EatFood)
            return "no such cell";
        g_touched[F<uint32_t>(c, 0)] = H->time_seconds(); // as the mouth contact would (HandleCollisionPair)
        uint32_t before = g_own_food;
        skg::cdecl_call<void>(a_EatFood, avatar_idx(), F<uint32_t>(c, 0), F<void*>(c, kRes), 1.f / 30.f);
        return "ate " + a[1] + " points=" + std::to_string(g_own_food - before);
    }
    if (!a.empty() && a[0] == "kill") {
        // one bite of our avatar (DamageCell, hooked: a guest's goes to the host) on the NPC creature
        // with sync key a[1], or on the nearest one. A bitten cell is not hurt again for a moment
        // (it took 2 calls a second apart): call it until it says "dying".
        std::vector<std::pair<float, uint8_t*>> v;
        // "kill <key> hit": DamageCell only (spikes, poison, electric: a lethal hit goes to KillCell)
        uint32_t want = a.size() > 1 && a[1] != "-" ? (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10) : 0;
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            if (modes::cell_is_puppet(c) || strcmp(kind_of(c), "creature") != 0)
                return;
            auto it = g_host_of.find(idx);
            if (is_guest() && it == g_host_of.end())
                return;
            uint32_t key = is_guest() ? it->second : idx;
            if (want ? key != want : (F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying)))
                return;
            v.push_back({net::dist(pos_of(c), pos_of(av)), c});
        });
        if (v.empty())
            return want ? "gone" : "no creature";
        std::sort(v.begin(), v.end(), [](auto& x, auto& y) { return x.first < y.first; });
        uint8_t* c = v[0].second;
        uint32_t key = is_guest() ? g_host_of[F<uint32_t>(c, 0)] : F<uint32_t>(c, 0);
        net::Vec3 cp = pos_of(c), ap = pos_of(av);
        float dx = cp.x - ap.x, dy = cp.y - ap.y, l = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
        float dir[3] = {dx / l, dy / l, 0}, at[3] = {cp.x, cp.y, cp.z};
        if (!F<uint8_t>(c, kDead) && !F<uint8_t>(c, kDying)) {
            if (a_Bite && !(a.size() > 2 && a[2] == "hit")) // the game's bite (event 10): DamageCell, or BreakOrSwallow when lethal
                skg::cdecl_call<bool>(a_Bite, avatar_idx(), F<uint32_t>(c, 0), at, dir, 0, 1.f / 30.f);
            else
                h_DamageCell(c, av, dir, 4, 0, 0.f);
        }
        char b[160];
        snprintf(b, sizeof b, "bit c%u at %.1f level=%d hp=%d pos=(%.1f %.1f)%s", key, v[0].first, F<int>(c, kLevel), F<int>(c, kHp),
                 pos_of(c).x, pos_of(c).y, F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying) ? " dying" : "");
        return b;
    }
    if (!a.empty() && a[0] == "realbite") { // realbite [max distance | c<key>]: our cell bites the nearest
        // creature (or that one) through the game's Bite 0xE7B0A0, what a bite event runs: DamageCell,
        // or BreakOrSwallow when the bite is lethal
        uint8_t* best = nullptr;
        float bd = a.size() > 1 && a[1][0] != 'c' ? (float)atof(a[1].c_str()) : 30.f;
        if (a.size() > 1 && a[1][0] == 'c') {
            uint32_t key = (uint32_t)strtoul(a[1].c_str() + 1, nullptr, 10);
            auto mi = g_mirrors.find(key);
            best = is_guest() ? (mi != g_mirrors.end() && mi->second.local ? modes::cell_at(mi->second.local) : nullptr)
                              : modes::cell_at(key);
            bd = best ? net::dist(pos_of(best), pos_of(av)) : 0.f;
        } else {
            for_each_cell([&](uint8_t* c, uint32_t idx) {
                if (modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying) || strcmp(kind_of(c), "creature"))
                    return;
                if (is_guest() && !g_host_of.count(idx))
                    return;
                float d = net::dist(pos_of(c), pos_of(av));
                if (d < bd) {
                    bd = d;
                    best = c;
                }
            });
        }
        if (!best || !o_Bite)
            return "no creature";
        net::Vec3 p = pos_of(best), me = pos_of(av);
        float at[3] = {p.x, p.y, p.z}, dir[3] = {p.x - me.x, p.y - me.y, 0};
        float n = std::max(0.01f, std::sqrt(dir[0] * dir[0] + dir[1] * dir[1]));
        dir[0] /= n;
        dir[1] /= n;
        uint32_t idx = F<uint32_t>(best, 0);
        auto it = g_host_of.find(idx);
        char b[160];
        snprintf(b, sizeof b, "bit c%u hp=%d at %.1f: %d", it != g_host_of.end() ? it->second : idx, F<int>(best, kHp), bd,
                 (int)h_Bite(avatar_idx(), idx, at, dir, 0, 0.033f));
        return b;
    }
    if (a.size() >= 2 && a[0] == "mirrorfix") { // 0: the guest as in 0.7.2 (see g_mirror_fixes)
        g_mirror_fixes = atoi(a[1].c_str()) != 0;
        return g_mirror_fixes ? "mirror fixes on" : "mirror fixes off (as in 0.7.2)";
    }
    if (a.size() >= 2 && a[0] == "food") { // the game's AddFoodPoints (hooked: shared)
        h_AddFood(atoi(a[1].c_str()), false);
        return "food added";
    }
    if (!a.empty() && a[0] == "mate") { // tests: the mating-call button (S+0x69), the mate, where it is
        uint8_t* s = S();
        if (s && a.size() >= 2 && a[1] == "button")
            F<uint8_t>(s, 0x69) = 1; // as if a part had been collected (0xE5D7B0 sets it)
        uint8_t* m = modes::cell_at(mate_idx());
        float sc[2] = {-1, -1};
        if (m) {
            net::Vec3 p = pos_of(m);
            float w[3] = {p.x, p.y, p.z};
            H->world_to_screen(w, sc);
        }
        char b[160];
        snprintf(b, sizeof b, "mate_button=%d mate=%08X alive=%d dist=%.1f at %d %d", s ? F<uint8_t>(s, 0x69) : -1,
                 mate_idx(), m ? 1 : 0, m ? net::dist(pos_of(m), pos_of(av)) : -1.f, (int)sc[0], (int)sc[1]);
        return b;
    }
    if (a.size() >= 4 && a[0] == "npcflag") { // host, tests: npcflag c<key> spare|invuln 0|1, hp <n> (BUG-0078)
        // spare: the NPC's species "won't attack the player" (resource data +0x300, all of its kind);
        // invuln: the NPC's own +0x111. Both make DamageCell skip a bite of the player on it.
        uint8_t* c = modes::cell_at((uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10));
        if (!c || !is_host() || modes::cell_is_puppet(c) || c == av)
            return "no such NPC (host only)";
        uint8_t v = a[3] == "1";
        if (a[2] == "hp") { // hp <n>: room for a few bites
            F<int>(c, kHp) = atoi(a[3].c_str());
            v = 1;
        } else if (a[2] == "invuln") {
            F<uint8_t>(c, kInvuln) = v;
        } else if (a[2] == "spare" && a_GetData && F<void*>(c, kRes)) {
            void* holder = nullptr;
            uint8_t* res = skg::cdecl_call<uint8_t*>(a_GetData, F<void*>(c, kRes), &holder);
            if (holder)
                --F<int>(holder, 0xC);
            if (!res)
                return "no resource data";
            F<uint8_t>(res, 0x300) = v;
        } else
            return "usage: coop.pool npcflag c<key> spare|invuln 0|1 | hp <n>";
        char b[96];
        snprintf(b, sizeof b, "npcflag %s %s=%d hp=%d", a[1].c_str(), a[2].c_str(), v, F<int>(c, kHp));
        return b;
    }
    if (a.size() >= 2 && a[0] == "kodrop") { // tests: ignore MSG_CELL_KO for <s> seconds (a late refresh, BUG-0238)
        double s = std::clamp(atof(a[1].c_str()), 0.0, 30.0);
        g_kodrop_until = s > 0 ? H->time_seconds() + s : 0;
        return "kodrop " + std::to_string(s).substr(0, 4);
    }
    if (a.size() >= 2 && a[0] == "invuln") { // tests: the avatar's invulnerability flag +0x111
        F<uint8_t>(av, kInvuln) = (uint8_t)atoi(a[1].c_str());
        return "invuln set";
    }
    if (a.size() >= 2 && a[0] == "hp") {
        F<int>(av, kHp) = atoi(a[1].c_str());
        return "hp set";
    }
    if (a.size() >= 3 && a[0] == "goto") { // swim to <dx dy> from where we are (like a mouse click)
        net::Vec3 p = pos_of(av);
        auto* t = (float*)(av + kTarget);
        t[0] = p.x + (float)atof(a[1].c_str());
        t[1] = p.y + (float)atof(a[2].c_str());
        t[2] = p.z;
        F<uint8_t>(av, 0x04) = 0;
        return "swimming";
    }
    if (a.size() >= 3 && a[0] == "tp") { // move our cell to <dx dy> from the nearest partner
        net::Vec3 at;
        if (nearest_partner(1e9f, &at).empty())
            return "no partner";
        stop_at(av, {at.x + (float)atof(a[1].c_str()), at.y + (float)atof(a[2].c_str()), 0.f});
        return "moved";
    }
    if (a.size() >= 2 && a[0] == "selfhit") { // tests: our own cell takes a bite of <dmg> (a lethal one: KillCell)
        hurt_avatar(atoi(a[1].c_str()), 4);
        uint8_t* c = avatar();
        char b[96];
        snprintf(b, sizeof b, "selfhit hp=%d dying=%d dead=%d downed=%d", c ? F<int>(c, kHp) : -1, c ? F<uint8_t>(c, kDying) : -1,
                 c ? F<uint8_t>(c, kDead) : -1, (int)g_downed);
        return b;
    }
    if (a.size() >= 3 && a[0] == "hurt") { // host: send a bite to player <id>
        net::Writer w;
        w.u32(0xFFFFFFFFu);
        w.u8((uint8_t)atoi(a[2].c_str()));
        w.u8(4);
        C.sess->send_reliable_to((uint8_t)atoi(a[1].c_str()), net::MSG_CELL_HIT, w);
        return "bite sent";
    }
    char b[400];
    int local = 0, chase_me = 0, chase_partner = 0;
    std::unordered_set<uint32_t> partner_idx;
    if (C.puppets)
        for (auto& p : C.puppets())
            if (p.cell)
                partner_idx.insert(F<uint32_t>(p.cell, 0));
    for_each_cell([&](uint8_t* c, uint32_t) {
        local++;
        uint32_t t = F<uint32_t>(c, 0x22C); // chase target (docs/re/cell.md 1)
        if (t == avatar_idx())
            chase_me++;
        else if (partner_idx.count(t))
            chase_partner++;
    });
    float pd = 0;
    nearest_partner(1e9f, nullptr, &pd);
    std::string ko;
    if (C.puppets) // what we draw of each partner (a knocked out one: translucent)
        for (auto& p : C.puppets())
            if (p.cell)
                ko += "puppet " + std::to_string(p.id) + " op=" + std::to_string(F<float>(p.cell, kOpacity)).substr(0, 4) + "\n";
    for (auto& [id, k] : g_ko) {
        uint8_t* pc = puppet_cell(id);
        char kb[96];
        snprintf(kb, sizeof kb, "ko[%d]=%d left=%.1f hold=%.2f op=%.2f fx=%u\n", id, k.state, k.left, k.hold,
                 pc ? F<float>(pc, kOpacity) : -1.f, k.fx);
        ko += kb;
    }
    snprintf(b, sizeof b,
             "avatar hp=%d invuln=%d own_eats=%u own_points=%u bites_taken=%u damage_taken=%u pos=(%.1f %.1f) size=%.2f target=%.2f body=%.2f downed=%d partner_dist=%.1f "
             "revive_dist=%.1f local_gameplay_cells=%d\n",
             F<int>(av, kHp), F<uint8_t>(av, kInvuln), g_own_eats, g_own_food, g_bites_taken, g_damage_taken, pos_of(av).x, pos_of(av).y, F<float>(av, kSize), F<float>(av, kTargetSize), g_body,
             g_downed ? 1 : 0, pd, revive_dist(av), local);
    // cine=1: a caption waits for the player's OK (cinematic manager +0x2C, docs/re/space.md 7.2);
    // parts: the unlocked part flags S+0x34 (13 bytes)
    void* cm = a_Cine ? skg::cdecl_call<void*>(a_Cine) : nullptr;
    std::string parts;
    if (uint8_t* s = S())
        for (int i = 0; i < 13; i++)
            parts += s[0x34 + i] ? '1' : '0';
    char ob[160];
    snprintf(ob, sizeof ob, "avatar_opacity=%.2f cine=%d parts=%s chasing_me=%d chasing_partner=%d\n",
             F<float>(av, kOpacity), cm && F<uint32_t>(cm, 0x2C) ? 1 : 0, parts.c_str(), chase_me, chase_partner);
    return b + std::string(ob) + ko + status();
}

std::string status()
{
    char d[96];
    snprintf(d, sizeof d, "cell_contacts: creature_pairs=%u bite_events=%u\n", g_creature_pairs, g_bite_events);
    char b[1100];
    uint8_t* s = S();
    int food = s ? F<int32_t>(s, 0x1C) : -1;
    if (C.sess && C.sess->role() == net::Role::Host)
        snprintf(b, sizeof b, "cell(host): food=%d own_bites=%u own_hits=%u own_chunks=%u hits_in=%u hits_aggro=%u hits_spared=%u bitten_in=%u puppet_hits_dropped=%u eats_in=%u revives=%u respawns=%u pairs_skipped=%u "
                 "puppet_saves=%u puppet_new=%u npc_deaths=%u loot_calls=%u loot_cells=%u kills_in=%u eats_late=%u\n", food, g_own_bites, g_own_hits, g_own_chunks, g_hits_in, g_hits_aggro, g_hits_spared, g_bitten_in,
                 g_puppet_hits_dropped, g_eats_in, g_revives, g_respawns, g_pairs_skipped, g_puppet_saves, g_puppet_new, g_npc_deaths, g_loot_calls, g_loot_cells, g_kills_in, g_eats_late);
    else
        snprintf(b, sizeof b,
                 "cell(guest): food=%d mirrors=%u created=%u destroyed=%u purged=%u hits_sent=%u eats_sent=%u "
                 "revives=%u respawns=%u pairs_skipped=%u hurt_local=%u hurt_fwd=%u bitten_sent=%u soft_moves=%u soft_turns=%u chunks_sent=%u chunks_taken=%u breaks_sent=%u wipes=%u kept=%u puppet_saves=%u puppet_new=%u "
                 "npc_deaths=%u loot_calls=%u loot_cells=%u mirror_kills=%u kills_sent=%u death_replays=%u loot_skipped=%u "
                 "host_of=%u late_chunks=%u rounds_partial=%u meals_unacked=%u\n",
                 food, (unsigned)g_mirrors.size(), g_created, g_destroyed, g_purged, g_hits_sent, g_eats_sent,
                 g_revives, g_respawns, g_pairs_skipped, g_hurt_local, g_hurt_fwd, g_bitten_sent, g_soft_moves, g_soft_turns, g_chunks_sent, g_chunks_taken, g_breaks_sent, g_wipes, g_kept, g_puppet_saves, g_puppet_new,
                 g_npc_deaths, g_loot_calls, g_loot_cells, g_mirror_kills, g_kills_sent, g_death_replays, g_loot_skipped,
                 (unsigned)g_host_of.size(), g_late_chunks, g_rounds_partial, (unsigned)g_meals.unacked.size());
    return b + std::string(d);
}

std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    if (!in_cell())
        return v;
    auto entry = [&](uint8_t* c, uint32_t key) {
        net::Quat q;
        modes::mat_to_quat((float*)(c + kRot), q);
        v.push_back({"c" + std::to_string(key), pos_of(c), q});
    };
    if (is_host()) {
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            if (!modes::cell_is_puppet(c))
                entry(c, idx);
        });
    } else {
        for (auto& [h, m] : g_mirrors)
            if (uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr)
                entry(c, h);
    }
    return v;
}

void join_partner()
{
    uint8_t* a = in_cell() ? avatar() : nullptr;
    net::Vec3 at;
    if (a && !nearest_partner(1e9f, &at).empty())
        stop_at(a, {at.x + 2.5f, at.y + 1.f, 0.f});
}

} // namespace cellsync
