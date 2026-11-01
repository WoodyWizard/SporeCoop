#include "cellsync.h"
#include "coop_ui.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "cell_logic.h"
#include "view_centres.h"
#include "game.h"
#include "modes.h"
#include "net/interp.h"
#include "net/utf8.h"
#include "net/validate.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "trace.h"

void coop_module_of(uintptr_t a, char* out, size_t cap); // debug.cpp (RE, BUG-0355)

namespace cellsync {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_ok;
static uintptr_t a_Wipe, a_Cine, a_Pair, a_GetData, a_DamageCell, a_KillCell, a_Break, a_Destroy, a_AddFood, a_Populate, a_Rollover, a_GetDamage,
    a_RefCreate, a_Serializer, a_S, a_Fnv, a_Effect, a_TargetParams, a_IsPrey, a_IsThreat, a_ScaleDiff, a_NoAttack, a_Alert, a_React, a_Rest,
    a_Soft, a_ChunkA, a_ChunkB, a_Bite, a_SpawnLoot, a_EatFood, a_ModelData, a_CellUI, a_DnaFor;

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
COOP_FIX("pool.meals_trim", "BUG-0219", cell_logic::g_meals_trim, "cell: a host without meal acks (0.7.2) does not grow the guest's meal list");
COOP_FIX("pool.resend", "BUG-0128", g_fix_resend, "cell: food / DNA totals sent again every 2 s and to newcomers");
COOP_FIX("pool.kostale", "BUG-0061", g_fix_kostale, "cell: a remote knockout ends when its owner leaves or stops refreshing it");
COOP_FIX("pool.koreset", "BUG-0070", g_fix_koreset, "cell: our knockout ends with the session or the stage");
COOP_FIX("pool.bodylevel", "BUG-0041", g_fix_bodylevel, "cell: a level-up during the regrowth gives the level's own size");
COOP_FIX("pool.rescue", "BUG-0131", g_fix_rescue, "cell: a knocked-out partner rescues, revives nobody");
COOP_FIX("pool.guestbite", "BUG-0078", g_fix_guestbite, "cell: a guest's bite passes the game's ShouldNotAttack gate");
// wave 5 (review of 0.7.3.0)
static bool g_fix_ko_grace = true; // BUG-0238: a remote knockout lasts to its own deadline through late refreshes
COOP_FIX("pool.ko_grace", "BUG-0238", g_fix_ko_grace, "cell: a late knockout refresh does not end it, replay its effect; KO_UP when ours ends on a stage leave");
static bool g_fix_ko_no_eat = true; // BUG-0364: a knocked-out cell eats nothing (food, chunks) until it is up again
COOP_FIX("cell.ko_no_eat", "BUG-0364", g_fix_ko_no_eat, "cell: a knocked-out cell does not eat what touches it (EatFood, chunk bites), so it does not respawn bigger");
static uint32_t g_ko_eats_blocked; // BUG-0364: meals and chunk bites of our knocked-out cell left out
static double g_kodrop_until; // tests (`coop.pool kodrop <s>`): MSG_CELL_KO ignored until then
static bool g_fix_food_ack_away = true; // BUG-0239: the host acknowledges a guest's meal it cannot count
COOP_FIX("pool.food_ack_away", "BUG-0239", g_fix_food_ack_away, "cell: a guest's meal reaching a host in an editor (no avatar) is acknowledged, not left on the way");
static bool g_fix_guestbite_type = true; // BUG-0240: a guest's bite also needs IsDamageable's cell type and AI type
COOP_FIX("pool.guestbite_type", "BUG-0240", g_fix_guestbite_type, "cell: a guest's bite spares cells IsDamageable refuses (cell type not 2, AI type 0)");
static bool g_test_away; // tests (`coop.pool away 1|0`, host): the Cell messages are handled as in an editor
static uint32_t g_hits_spared;
// BUG-0205 / BUG-0151 (GitHub #130): while the host is away (main menu, an editor or a load, the
// state coop.cpp calls "host away") its pool is not simulated and no state comes. The guest was told
// "you play on alone", but its mirrors stood AI-locked, it spawned nothing (h_Populate) and the food
// it ate went to a host that dropped it. Now the guest plays its own pool meanwhile: the mirrors
// become cells of its own game (AI lock and keep-alive cleared, no longer mirrors), its game spawns,
// drops loot and runs every collision pair, and what it eats is kept. When the host is back, the
// first state round of the host's pool purges the guest's own cells as strays (as at a join) and
// mirrors the host's again; the kept food goes to the host as meals (BUG-0051 numbering).
// Off: the frozen pool of 0.7.3.1 (and a host editor or load does not count as away in Cell).
static bool g_fix_away = true;
COOP_FIX("pool.away", "BUG-0205", g_fix_away, "cell: a guest plays its own pool while the host is away and takes the host's pool again when it is back");
static bool g_solo;        // guest: playing its own pool now (the host away)
static int g_solo_food[2]; // guest: meat / plant points eaten meanwhile (the guest's game has them)
static bool g_solo_owed;   // the host is back: g_solo_food goes out before its first state or total is applied
static uint32_t g_solo_times, g_solo_released, g_solo_spawns, g_solo_sent, g_solo_dropped;
bool away_enabled() { return g_fix_away; }
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

static uint8_t g_ko_logged;
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
    if (state != g_ko_logged) // the knockout state is refreshed every second: changes only
        trace::line("ko", "own cell state %u (0 up, 1 out, 2 revived, 3 respawned) left=%.1f hold=%.2f", state, left, hold);
    g_ko_logged = state;
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
    trace::line("ko", "player %d cell state %u -> %u (0 up, 1 out, 2 revived, 3 respawned)%s", id, prev, state,
                in_cell() && puppet_cell(id) ? "" : " (no puppet here)");
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

// the resource's cell type (+0xB4: 2 solid, 3 plant, 5 poison, ...) and ai.type (docs/re/cell.md 1:
// the first int of the AI block chosen by difficulty, as IsDamageable 0xE52940 picks it); false when
// the resource data cannot be read. One reader for kind_of, ai_type and the guest-bite gate.
static bool res_types(uint8_t* c, int* cell_type, int* ai_type)
{
    void* ref = c ? F<void*>(c, kRes) : nullptr;
    if (!ref || !a_GetData)
        return false;
    void* holder = nullptr;
    uint8_t* res = skg::cdecl_call<uint8_t*>(a_GetData, ref, &holder);
    if (holder)
        --F<int>(holder, 0xC);
    if (!res)
        return false;
    int d = S() ? F<int>(S(), 0x7C) : 1;
    uint8_t* ai = res + 0xE0;
    if (d == 0 && F<int>(res, 0x248) != -1)
        ai = res + 0x248;
    else if (d == 2 && F<int>(res, 0x194) != -1)
        ai = res + 0x194;
    *cell_type = F<int>(res, 0xB4);
    *ai_type = F<int>(ai, 0);
    return true;
}

// ai.type of a cell, -1 unknown
static int ai_type(uint8_t* c)
{
    int ct, ai;
    return res_types(c, &ct, &ai) ? ai : -1;
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
    uint32_t meal = g_meals.sent(food);
    w.u32(meal); // the meal number the host echoes in its total (BUG-0051)
    C.sess->send_reliable_to(0, net::MSG_CELL_EAT, w);
    static const char* kinds[] = {"gone", "kill", "break"};
    trace::ev("eat", kind == ACT_GONE ? "sent gone" : "sent kill", "sent %s c%u food=%d %s meal=%u", kinds[kind % 3], hidx,
              food, plant ? "plant" : "meat", meal);
}

static void flush_food()
{
    if (!g_pend_food)
        return;
    if (is_guest()) {
        net::Writer w;
        w.u32((uint32_t)g_pend_food);
        w.u8(g_pend_plant ? 1 : 0);
        uint32_t meal = g_meals.sent(g_pend_food);
        w.u32(meal);
        C.sess->send_reliable_to(0, net::MSG_CELL_FOOD, w);
        trace::ev("eat", "sent food", "sent food +%d %s meal=%u (no host cell with it)", g_pend_food,
                  g_pend_plant ? "plant" : "meat", meal);
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
using NoAttack_t = bool(SK_CDECL*)(uint8_t*, uint8_t*);
static NoAttack_t& o_NoAttack_ptr();
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
    trace::ev("bite", "sent", "sent c%u dmg=%d type=%d", hidx, dmg, type);
}

// BUG-0350: the guest's bite on a mirror skips the game's DamageCell (the host applies it), and with
// it the health ring DamageCell shows when the avatar bites (ShowHealthRollover 0xE62340, before the
// hp change, docs/re/cell.md#cell-guest-bite-ring): the guest never saw the hp of what it bit. The
// ring is shown here under DamageCell's own conditions (damage > 0, the victim damageable and not
// spared); the hp change and the hurt reaction stay the host's (they come with the state).
// `coop.fix cell.bite_ring off`: no ring (0.7.3.1).
static bool g_bite_ring_fix = true;
COOP_FIX("cell.bite_ring", "BUG-0350", g_bite_ring_fix, "cell: a guest's bite on a host creature shows its health ring");
static uint32_t g_bite_rings; // tests
// tests: whether a cell is inside the window (world_to_screen)
static int on_screen_dbg(uint8_t* c)
{
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    float w[3] = {F<float>(c, kPos), F<float>(c, kPos + 4), F<float>(c, kPos + 8)}, sc[2];
    return H->world_to_screen(w, sc) && sc[0] >= 0 && sc[1] >= 0 && sc[0] < sw && sc[1] < sh;
}

// tests: the game's health ring of cell <idx>: the timer of its entry in the cell UI's rollover pool
// (*cCellUI::_ptr + 0x9C, entries: +4 the cell's pool index, +0x24 a timer the game sets to 0.5),
// -1 when the cell has none (docs/re/cell.md#cell-guest-bite-ring)
static float rollover_of(uint32_t idx)
{
    uint8_t* ui = a_CellUI ? *(uint8_t**)a_CellUI : nullptr;
    if (!ui || !idx)
        return -1.f;
    uint8_t* pool = ui + 0x9C;
    uint8_t* data = F<uint8_t*>(pool, 0);
    uint32_t high = F<uint32_t>(pool, 0x10), size = F<uint32_t>(pool, 0x14);
    if (!data || size < 0x28 || high > 4096)
        return -1.f;
    for (uint32_t i = 0; i < high; i++) {
        uint8_t* e = data + (size_t)i * size;
        if ((F<uint32_t>(e, 0) & 0xFFFF) == i && F<uint32_t>(e, 4) == idx)
            return F<float>(e, 0x24);
    }
    return -1.f;
}

static const char* g_bite_ring_skip = "-"; // tests: why the last guest bite showed no ring
static void bite_ring(uint8_t* victim, uint8_t* attacker, int dmg)
{
    const char* why = !g_bite_ring_fix ? "off" : !a_Rollover ? "nosym" : dmg <= 0 ? "dmg" : !F<uint32_t>(victim, 0x248) ? "gfx"
                      : F<int>(victim, kHp) <= 0 ? "hp" : F<uint8_t>(victim, kDead) || F<uint8_t>(victim, kDying) ? "dead"
                      : F<uint8_t>(victim, 0x17C) ? "lock" : F<uint8_t>(victim, kInvuln) ? "invuln"
                      : o_NoAttack_ptr() && o_NoAttack_ptr()(victim, attacker) ? "spared" : nullptr;
    g_bite_ring_skip = why ? why : "-";
    if (why)
        return;
    skg::cdecl_call<void>(a_Rollover, victim, F<int>(victim, kHp));
    g_bite_rings++;
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
            if (attacker == avatar()) {
                int dmg = a_GetDamage ? skg::cdecl_call<int>(a_GetDamage, attacker, victim) : 1;
                send_hit(it->second, dmg, type);
                bite_ring(victim, attacker, dmg);
            }
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
    if (g_fix_ko_no_eat && g_downed && e == avatar()) { // BUG-0364: a knocked-out mouth does not bite
        g_ko_eats_blocked++;
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
static uint32_t g_dbg_destroys, g_dbg_expect, g_dbg_freed, g_dbg_nogfx, g_dbg_alive; // RE (BUG-0355)
static std::map<uintptr_t, uint32_t> g_dbg_destroy_by; // RE (BUG-0355): DestroyCell calls by return address
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
    // RE (BUG-0355): does the game's DestroyCell give the cell's soft-body particles back?
    uint8_t* dc = modes::cell_at(idx);
    uint8_t* dparts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
    int dn = dc ? F<int>(dc, 0x250) : 0, before = dparts ? F<int>(dparts, 0x1BD54) : 0;
    bool dgfx = dc && F<uint32_t>(dc, 0x248);
    if (dc)
        g_dbg_destroy_by[(uintptr_t)__builtin_return_address(0)]++;
    o_Destroy(idx, loot, f, x);
    if (dc && dparts && dn > 1) {
        g_dbg_destroys++;
        g_dbg_expect += (uint32_t)dn;
        g_dbg_freed += (uint32_t)std::max(0, before - F<int>(dparts, 0x1BD54));
        g_dbg_nogfx += !dgfx;
        g_dbg_alive += modes::cell_at(idx) != nullptr;
    }
}

// BUG-0364: the knockout holds our cell in place at hp 1 (downed_tick), but the game's contact
// events still ran: food floating into it was eaten (event 0x12, EatFood 0xE7DF40 -> OnEat ->
// AvatarEat -> AddFoodPoints), so the cell came back from its respawn bigger than "small again".
// A knocked-out cell eats nothing: the food stays where it is for whoever comes.
using EatFood_t = void(SK_CDECL*)(uint32_t, uint32_t, void*, float);
static EatFood_t o_EatFood;
static void SK_CDECL h_EatFood(uint32_t eater, uint32_t food, void* ref, float dt)
{
    if (g_fix_ko_no_eat && g_downed && in_cell() && eater == avatar_idx() && avatar()) {
        g_ko_eats_blocked++;
        return;
    }
    o_EatFood(eater, food, ref, dt);
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
        uint8_t* s0 = S();
        trace::ev("eat", "own", "own cell ate +%d %s (total %d)", amount, plant ? "plant" : "meat",
                  s0 ? F<int32_t>(s0, 0x1C) : -1);
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
    if (g_solo && is_guest() && !g_food_guard && amount != 0 && in_cell()) {
        g_solo_food[plant ? 1 : 0] += amount; // BUG-0205: kept until the host is back
        return;
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
static const char* kind_of(uint8_t* c);
// BUG-0307 diagnostics: where in the frame our own cell's state first stops being finite. The probes
// run at the start of the cell step (WorldScaleStep), around the own cell's collision pairs, after its
// soft-body step and after our guest tick; the first probe that sees a non-finite field logs the
// field mask, the probe and the last probe that saw it finite, with what was near.
enum : unsigned { NF_POS = 1, NF_PREV = 2, NF_VEL = 4, NF_ACC = 8, NF_ROT = 16, NF_SHAPE = 32, NF_SIZE = 64,
                  NF_PPOS = 128, NF_PVEL = 256, NF_PGOAL = 512, NF_MASS = 1024 };
static bool fin(const float* v, int n)
{
    for (int i = 0; i < n; i++)
        if (!std::isfinite(v[i]))
            return false;
    return true;
}
static unsigned cell_nonfinite(uint8_t* c)
{
    unsigned m = 0;
    if (!c)
        return 0;
    m |= fin((float*)(c + 0x4C), 3) ? 0 : NF_POS;
    m |= fin((float*)(c + 0x84), 3) ? 0 : NF_PREV;
    m |= fin((float*)(c + 0x90), 3) ? 0 : NF_VEL;
    m |= fin((float*)(c + 0x1C4), 3) ? 0 : NF_ACC;
    m |= fin((float*)(c + 0x5C), 9) ? 0 : NF_ROT;
    m |= fin((float*)(c + 0x24), 9) ? 0 : NF_SHAPE;
    m |= fin((float*)(c + 0x58), 1) && fin((float*)(c + 0xB4), 1) ? 0 : NF_SIZE;
    m |= fin((float*)(c + 0xB8), 2) ? 0 : NF_MASS; // mass, inverse mass
    int n = F<int>(c, 0x250);
    uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
    if (F<uint32_t>(c, 0x248) && parts && n > 1 && n <= 64)
        for (int i = 0; i < n; i++) {
            uint32_t k = F<uint32_t>(c, 0x254 + 4 * i);
            if (k >= 1500)
                continue;
            m |= fin((float*)(parts + 4 + 12 * (size_t)k), 3) ? 0 : NF_PPOS;
            m |= fin((float*)(parts + 0x4654 + 12 * (size_t)k), 3) ? 0 : NF_PVEL;
            m |= fin((float*)(parts + 0x11944 + 12 * (size_t)k), 3) ? 0 : NF_PGOAL;
        }
    return m;
}
static const char* g_own_ok_at = "-";
static unsigned g_own_nf;          // the mask at the last probe
static uint32_t g_own_nf_events;   // finite -> not finite transitions seen
static uint32_t g_shared_parts;    // own particle indices also held by another cell (last scan)
static double g_shared_scan_at;
static float g_last_dt;
static void own_probe(const char* where, uint8_t* other = nullptr)
{
    uint8_t* av = in_cell() ? avatar() : nullptr;
    if (!av)
        return;
    unsigned m = cell_nonfinite(av);
    if (!m) {
        g_own_nf = 0;
        g_own_ok_at = where;
        return;
    }
    if (g_own_nf) {
        g_own_nf = m;
        return;
    }
    g_own_nf = m;
    if (++g_own_nf_events > 20)
        return;
    // what was near: cells within 3 sizes, non-finite cells there, own particles shared with others
    auto* p = (float*)(av + 0x84); // the previous position (the current one may be NaN)
    float size = F<float>(av, kSize), r = std::isfinite(size) ? std::max(3.f * size, 2.f) : 2.f;
    int near_n = 0, near_bad = 0, zero_size = 0, shared = 0;
    std::unordered_set<uint32_t> own;
    int n = F<int>(av, 0x250);
    if (F<uint32_t>(av, 0x248) && n > 1 && n <= 64)
        for (int i = 0; i < n; i++)
            own.insert(F<uint32_t>(av, 0x254 + 4 * i));
    for_each_cell([&](uint8_t* c, uint32_t) {
        auto* q = (float*)(c + 0x4C);
        int cn = F<int>(c, 0x250);
        if (!own.empty() && F<uint32_t>(c, 0x248) && cn > 1 && cn <= 64)
            for (int i = 0; i < cn; i++)
                shared += own.count(F<uint32_t>(c, 0x254 + 4 * i)) ? 1 : 0;
        if (!std::isfinite(q[0]) || !std::isfinite(q[1])) {
            near_bad++;
            return;
        }
        if (fin(p, 2) && std::hypot(q[0] - p[0], q[1] - p[1]) < r) {
            near_n++;
            zero_size += F<float>(c, kSize) <= 1e-4f;
        }
    });
    char ob[160] = "";
    if (other)
        snprintf(ob, sizeof ob, " other=c%u %s pos=(%.2f %.2f) size=%.3f nf=0x%X dist=%.4f", F<uint32_t>(other, 0), kind_of(other),
                 F<float>(other, 0x4C), F<float>(other, 0x50), F<float>(other, kSize), cell_nonfinite(other),
                 std::hypot(F<float>(other, 0x4C) - p[0], F<float>(other, 0x50) - p[1]));
    H->log(M, SK_WARN,
           "coop: cell: own cell not finite (BUG-0307) at %s, finite at %s: mask=0x%X prev=(%.2f %.2f) size=%.3f dt=%.4f "
           "particles=%d near=%d (size 0: %d) non-finite cells=%d shared particles=%d mirrors=%u%s",
           where, g_own_ok_at, m, p[0], p[1], size, g_last_dt, n, near_n, zero_size, near_bad, shared,
           (unsigned)g_mirrors.size(), ob);
}
// own particle indices that another live cell holds too (once a second on the guest; logged when > 0)
static void shared_parts_scan(double now)
{
    if (now - g_shared_scan_at < 1.0)
        return;
    g_shared_scan_at = now;
    uint8_t* av = avatar();
    int n = av ? F<int>(av, 0x250) : 0;
    if (!av || !F<uint32_t>(av, 0x248) || n < 2 || n > 64)
        return;
    std::unordered_set<uint32_t> own;
    for (int i = 0; i < n; i++)
        own.insert(F<uint32_t>(av, 0x254 + 4 * i));
    uint32_t shared = 0, holder = 0;
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        int cn = F<int>(c, 0x250);
        if (!F<uint32_t>(c, 0x248) || cn < 2 || cn > 64)
            return;
        for (int i = 0; i < cn; i++)
            if (own.count(F<uint32_t>(c, 0x254 + 4 * i))) {
                shared++;
                holder = idx;
            }
    });
    if (shared && !g_shared_parts)
        H->log(M, SK_WARN, "coop: cell: %u of our own %d soft-body particles are held by another cell too (c%u) (BUG-0307)", shared, n,
               holder);
    g_shared_parts = shared;
}

using Pair_t = void(SK_CDECL*)(uint8_t*, uint8_t*, float);
static Pair_t o_Pair;
static uint32_t g_pairs_skipped;
static const char* kind_of(uint8_t* c);
// what our cell does to creatures (tests: how often a player gets to bite): collision pairs of our
// cell with a creature, interaction events of kind 10 our cell started (Bite 0xE7B0A0)
static uint32_t g_creature_pairs, g_bite_events;
static uint32_t g_own_pairs; // tests (BUG-0342): collision pairs of our own cell with any cell
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
    if (is_guest() && in_cell() && !g_solo) { // BUG-0205: our own pool while the host is away: every pair
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
    uint8_t* av = avatar();
    bool mine = av && (a == av || b == av);
    if (mine)
        g_own_pairs++;
    if (mine)
        own_probe("before a collision", a == av ? b : a);
    o_Pair(a, b, dt);
    if (mine)
        own_probe("collision", a == av ? b : a);
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
    if (is_guest() && in_cell() && query == fg_query()) {
        if (!g_solo)
            return 0;
        g_solo_spawns++; // BUG-0205: the host is away, our game fills its own pool
    }
    return o_Populate(prob, layer, level, query, bbox, marker, x);
}

// ---- BUG-0174: the host keeps the world populated around every player, not only around itself ----
// Spore populates and keeps only the tiles around the local view (PopulateTiles 0xE7D370 with the
// list cCellGame+0x4128 and the view boxes 0x16B3C88, docs/re/cell.md#cell-partner-tiles). In co-op
// the host's game is the one world and a guest's own population is off (h_Populate), so a guest more
// than one screen from the host swam in an empty pool. Now the host runs one more PopulateTiles pass
// per partner, with its own tile list and the view boxes moved onto the partner's cell; a tile two
// players share is populated once (h_PopTile), and a tile / cell inside any player's box is kept
// (h_TileActive). The players are the host's view centres (view_centres.h: the partners noted by coop.cpp,
// the same registry as BUG-0280's Creature / Tribe hook), at most views::kMaxCentres.
// Old (`coop.fix cell.partner_tiles off`): only the host's own view.
static bool g_fix_partner_tiles = true;
COOP_FIX("cell.partner_tiles", "BUG-0174", g_fix_partner_tiles, "cell: the host populates and keeps the tiles around every partner, and sends each guest the cells around it");
// BUG-0292: a cell whose pose is not finite (NaN, inf) is neither sent nor mirrored: a NaN mirror on
// a guest pushed its own cell to NaN through the soft-body contact, its camera and view boxes went
// NaN and PopulateTiles filled its stack with garbage tiles (fault, then a crash, at PopulateTile+0x8A).
// Old (`coop.fix pool.finite_state off`): sent and mirrored as they come.
static bool g_fix_finite = true;
COOP_FIX("pool.finite_state", "BUG-0292", g_fix_finite, "cell: a cell with a NaN / infinite pose is neither sent by the host nor mirrored by a guest");
static uint32_t g_nonfinite_sent_skipped, g_nonfinite_dropped;
// BUG-0299: a guest's own cell still went NaN with no NaN mirror (4 players, lab 20261030-053709): its
// view boxes followed, PopulateTiles filled the tile pool with garbage (a fault) and the next level-up
// crashed in PopulateTile+0x8A. Now no PopulateTiles / PopulateTile runs while the own view boxes are
// not finite, and the own cell is put back at its last finite position (soft-body particles around
// it, at rest). Old (`coop.fix cell.nan_guard off`): as before.
static bool g_fix_nan_guard = true;
COOP_FIX("cell.nan_guard", "BUG-0299", g_fix_nan_guard, "cell: no tile populating while the own view is NaN, and the own cell goes back to its last finite position");
static uint32_t g_nan_pop_skips, g_nan_heals;
static bool pose_finite(const net::Vec3& p, float vx, float vy, float size)
{
    return views::finite_pos(p) && std::isfinite(vx) && std::isfinite(vy) && std::isfinite(size) && std::fabs(vx) < 1e6f &&
           std::fabs(vy) < 1e6f;
}
// BUG-0295: the game's cell broadphase (the sweep-and-prune step 0xBBB450 / merge 0xBBAF10, called
// from SimUpdate) merges the endpoints in a stack buffer of 4096 entries (2 per cell): a game with
// more than ~2048 live cells overruns it (fault at 0xBBB07B, the stack and heap corrupted, crashes
// later in DestroyCell and on worker threads; docs/re/cell.md#cell-broadphase-cap). With 3-4 players
// the host's partner passes and each guest's 120 m state let the pools grow past 2300 cells. Now:
// a partner's tile is populated only while the host holds fewer than kPopCap (1100) cells (the others wait,
// `g_starved`, and are populated when room comes back; a tile on the partner's own screen up to
// kPopCapScreen), each guest gets at most its kSendCap
// nearest cells, and a guest creates no new mirror at kMirrorCap live cells.
// Old (`coop.fix cell.budget off`): no bound.
static bool g_fix_budget = true;
COOP_FIX("cell.budget", "BUG-0295", g_fix_budget, "cell: the host populates partner tiles only below 1100 cells (its screen below 1300), a guest gets its 900 nearest cells and mirrors at most 1500 (the game's broadphase holds ~2048)");
constexpr uint32_t kPopCap = 1100, kPopCapScreen = 1300, kSendCap = 900, kMirrorCap = 1500;
static int g_tile_level = -1; // the level base of the last PopulateTiles call (tests)
static uint32_t g_tiles_core, g_tiles_starved, g_tiles_fed, g_send_capped, g_mirror_capped, g_cells_peak;
static std::unordered_map<uint32_t, uint8_t> g_starved; // tile objects of ours not populated for the budget -> the partner (0xFF: unknown)
static uint32_t cells_live() { return G() ? F<uint32_t>(G(), 0x1C + 0xC) : 0; }
static bool over_budget() { return g_fix_budget && cells_live() >= kPopCap; }
static uintptr_t a_PopTiles, a_PopTile, a_TileActive, a_ViewBoxes;
constexpr size_t kTileCap = 2048; // entries of our own lists' fixed buffers (the active set is < 512 tiles)
struct TileList {             // the game's fixed_vector layout (PopulateTiles reads +0..+8, +0x10, +0x1018)
    int32_t* begin;
    int32_t* end;
    int32_t* cap;
    uint32_t pad;
    int32_t* fixed;
    uint8_t rest[0x1020 - 0x14];
};
struct PartnerTiles {
    TileList* list = nullptr;
    int32_t* buf = nullptr;
    double seen = 0;
};
static std::unordered_map<int, PartnerTiles> g_ptiles; // partner id -> its tile list
static views::Set g_views;                          // host: itself + every partner (this frame)
static std::vector<std::array<float, 12>> g_view_boxes; // the view boxes of each centre (host first)
static TileList* g_pass_list;   // the list of the pass PopulateTiles is running for us (null: none)
static int g_pass = -1;         // -1 none, 0 the host's pass, k > 0 the pass of centre k
static bool g_union;            // h_TileActive: also true inside another centre's box
static std::unordered_set<uint32_t> g_shadow_tiles; // tile objects not populated (another list has the tile)
static uint32_t g_tiles_unshared, g_tiles_flush, g_tiles_full, g_tiles_partner, g_tiles_shared, g_tiles_bg, g_tile_passes, g_tiles_kept, g_tile_resets, g_tiles_drained;

using PopTiles_t = void(SK_CDECL*)(int, TileList*, int);
static PopTiles_t o_PopTiles;
using PopTile_t = void(SK_CDECL*)(uint32_t*, int);
static PopTile_t o_PopTile;
using TileActive_t = bool(SK_CDECL*)(const int32_t*);
static TileActive_t o_TileActive;

static float* view_boxes() { return a_ViewBoxes ? (float*)a_ViewBoxes : nullptr; }
// BUG-0299: the own view boxes hold garbage (the own cell went NaN)
static bool view_bad()
{
    float* vb = view_boxes();
    if (!g_fix_nan_guard || !vb)
        return false;
    for (int i = 0; i < 12; i++)
        if (!std::isfinite(vb[i]) || std::fabs(vb[i]) > 1e7f)
            return true;
    return false;
}

// a tile object index of the pool cCellGame+0x38 is still alive (pool layout: docs/re/cell.md 0)
static bool tile_alive(uint32_t idx)
{
    uint8_t* g = G();
    if (!g)
        return false;
    uint8_t* data = F<uint8_t*>(g, 0x38);
    uint32_t used = F<uint32_t>(g, 0x38 + 0x10), size = F<uint32_t>(g, 0x38 + 0x14), slot = idx & 0xFFFF;
    if (!data || !size || size > 0x1000 || slot >= used)
        return false;
    return F<uint32_t>(data + (size_t)slot * size, 0) == idx;
}

static TileList* host_tiles() { return G() ? (TileList*)(G() + 0x4128) : nullptr; }

// every list but `except` that holds the tile already
// (a tile of ours left empty for the cell budget, BUG-0295, does not count: it has no cells)
static bool tile_elsewhere(int32_t key, TileList* except)
{
    auto has = [&](TileList* l) {
        if (!l || l == except || !cell_logic::tile_list_has(l->begin, l->end, key))
            return false;
        if (g_starved.empty())
            return true;
        for (int32_t* e = l->begin; e < l->end; e += 2)
            if (e[0] == key && !g_starved.count((uint32_t)e[1]))
                return true;
        return false;
    };
    if (has(host_tiles()))
        return true;
    for (auto& [id, pt] : g_ptiles)
        if (has(pt.list))
            return true;
    return false;
}

// BUG-0295: in a partner's pass (the view boxes moved onto the partner), is the tile on the partner's
// screen? IsTileActive reads the gameplay range as the visible box doubled, so asked with the
// visible box halved it answers for the visible box itself.
static bool partner_core(const uint32_t* t)
{
    float* vb = view_boxes();
    if (!vb || !o_TileActive)
        return false;
    float save[6];
    memcpy(save, vb, sizeof save);
    for (int i : {0, 1}) {
        float c = (save[i] + save[i + 3]) * 0.5f, h = (save[i + 3] - save[i]) * 0.25f;
        vb[i] = c - h;
        vb[i + 3] = c + h;
    }
    bool r = o_TileActive((const int32_t*)(t + 1));
    memcpy(vb, save, sizeof save);
    return r;
}
// a partner's tile waits: above kPopCap cells unless it is on the partner's screen, above
// kPopCapScreen always (the partner's own screen keeps food while the host's pool is busy)
// The cells of a populated tile show up in the pool's live count only over the next frames, so
// several tiles in one frame overshot the caps (host peaks 1714-1722 with 1250 / 1500, labs
// 20261030-053258, -062717): from kPopCap - 250 on, at most 2 partner tiles per frame.
static int g_frame_tiles;
static bool starve_tile(const uint32_t* t)
{
    if (!g_fix_budget)
        return false;
    uint32_t live = cells_live();
    if (live >= kPopCap - 250 && g_frame_tiles >= 2)
        return true;
    if (live < kPopCap)
        return false;
    if (live < kPopCapScreen && partner_core(t)) {
        g_tiles_core++;
        return false;
    }
    return true;
}

static void SK_CDECL h_PopTile(uint32_t* t, int all)
{
    if (view_bad()) {
        g_nan_pop_skips++;
        return;
    }
    if (t && g_pass >= 0) {
        if (g_pass > 0 && t[1] != 0) { // the host does not need a partner's background
            g_tiles_bg++;
            g_shadow_tiles.insert(t[0]);
            return;
        }
        if (tile_elsewhere(cell_logic::tile_key((int32_t)t[1], (int32_t)t[2], (int32_t)t[3], (int32_t)t[4]), g_pass_list)) {
            g_tiles_shared++; // its cells are there already: populating it again doubled the food
            g_shadow_tiles.insert(t[0]);
            return;
        }
        // BUG-0295: populated later, when the pool has room. The host's own gameplay tiles too while
        // partners play (its screen like theirs, the ring around it only below kPopCap): at level 4 the
        // host's own doubled box alone held ~1000 cells and the partners' screens got nothing
        // (cell_4p, lab 20261030-064009)
        bool budgeted = g_pass > 0 || (g_pass == 0 && t[1] == 0 && g_views.size() > 1);
        if (budgeted && (size_t)g_pass < g_views.size() && starve_tile(t)) {
            g_tiles_starved++;
            g_shadow_tiles.insert(t[0]);
            g_starved[t[0]] = (uint8_t)g_views.list()[(size_t)g_pass].player;
            return;
        }
        if (g_pass > 0) {
            g_tiles_partner++;
            g_frame_tiles++;
        }
    } else if (t && !g_shadow_tiles.empty() && g_shadow_tiles.count(t[0]))
        return; // RemoveCellsOfOldScale repopulates every tile object: not the shared ones twice
    o_PopTile(t, all);
}

static bool SK_CDECL h_TileActive(const int32_t* t)
{
    // inside a partner pass the only caller is PopulateTiles' delete loop, whose erase would shorten
    // the host's list (tiles_delete did this list's deletes already): it deletes nothing of ours
    if (g_pass > 0)
        return true;
    bool r = o_TileActive(t);
    float* vb = view_boxes();
    if (r || !g_union || !t || t[0] != 0 || !vb || g_view_boxes.size() < 2)
        return r;
    float save[12];
    memcpy(save, vb, sizeof save);
    for (auto& b : g_view_boxes) {
        if (!memcmp(b.data(), save, sizeof save))
            continue; // the box it was just asked with
        memcpy(vb, b.data(), sizeof save);
        if ((r = o_TileActive(t)))
            break;
    }
    memcpy(vb, save, sizeof save);
    if (r)
        g_tiles_kept++;
    return r;
}

// Delete the tiles of one of our lists that `keep` refuses, as PopulateTiles' own delete loop does
// (the tile's +0x300 object released, the pool G+0x38 slot freed). Never through the game's loop:
// its erase is inlined with the host's list end (`cCellGame+0x412C`), so a tile it deleted from our
// list shortened the HOST's list and read past ours (crash at PopulateTiles+0xC3, lab
// 20261030-001703, BUG-0174).
static std::vector<uint32_t> g_freed_dbg;
// A tile of ours that goes while another list holds the same key unpopulated (a shadow: its cells
// were ours): that tile is populated now, or the area lost its food. Seen after a level change: our
// tiles went back to the pool and the host's shadowed tiles around its own cell stayed empty
// (RemoveCellsOfOldScale skips shadows; cell_new_game: the host found no meat on its screen, lab
// 20261030-022912).
static uint32_t g_tiles_handed;
static void hand_over(int32_t key, TileList* from)
{
    uint8_t* g = G();
    if (!g || g_shadow_tiles.empty())
        return;
    uint8_t* data = F<uint8_t*>(g, 0x38);
    uint32_t size = F<uint32_t>(g, 0x38 + 0x14);
    auto try_list = [&](TileList* l) {
        if (!l || l == from)
            return false;
        for (int32_t* e = l->begin; e < l->end; e += 2)
            if (e[0] == key && g_shadow_tiles.count((uint32_t)e[1]) && tile_alive((uint32_t)e[1])) {
                uint32_t* t = (uint32_t*)(data + (size_t)((uint32_t)e[1] & 0xFFFF) * size);
                if (l != host_tiles() && over_budget()) { // BUG-0295: a partner's area waits for room
                    g_starved.emplace((uint32_t)e[1], (uint8_t)0xFF);
                    return true;
                }
                g_shadow_tiles.erase((uint32_t)e[1]);
                g_starved.erase((uint32_t)e[1]);
                if (t[1] == 0) { // gameplay layer only (a partner's background was never ours)
                    int pass = g_pass;
                    g_pass = -1;
                    o_PopTile(t, 0);
                    g_pass = pass;
                    g_tiles_handed++;
                }
                return true;
            }
        return false;
    };
    if (try_list(host_tiles()))
        return;
    for (auto& [id, pt] : g_ptiles)
        if (try_list(pt.list))
            return;
}

template <typename Keep>
static void tiles_delete(TileList* l, Keep keep)
{
    uint8_t* g = G();
    if (!l || !g)
        return;
    uint8_t* pool = g + 0x38;
    uint8_t* data = F<uint8_t*>(pool, 0);
    uint32_t size = F<uint32_t>(pool, 0x14);
    int32_t* out = l->begin;
    for (int32_t* e = l->begin; e < l->end; e += 2) {
        uint32_t idx = (uint32_t)e[1];
        uint8_t* t = tile_alive(idx) ? data + (size_t)(idx & 0xFFFF) * size : nullptr;
        if (t && (F<int32_t>(t, 8) == -1 || keep((const int32_t*)(t + 4)))) {
            out[0] = e[0];
            out[1] = e[1];
            out += 2;
            continue;
        }
        if (t) {
            if (void* o = F<void*>(t, 0x300)) {
                void** vt = *(void***)o;
                skg::thiscall<void>((uintptr_t)vt[3], o, 1);
                if ((o = F<void*>(t, 0x300))) {
                    F<void*>(t, 0x300) = nullptr;
                    skg::thiscall<void>((uintptr_t)(*(void***)o)[1], o);
                }
            }
            bool was_shadow = g_shadow_tiles.count(idx) != 0;
            // cObjectPool_::DeleteObject 0xB72370: the slot goes to the head of the free list
            F<uint32_t>(t, 0) = F<uint32_t>(pool, 4);
            F<uint32_t>(pool, 0xC)--;
            F<uint32_t>(pool, 4) = idx & 0xFFFF;
            g_shadow_tiles.erase(idx);
            g_starved.erase(idx);
            g_tiles_drained++;
            if (g_freed_dbg.size() < 6)
                g_freed_dbg.push_back(idx);
            if (!was_shadow)
                hand_over(e[0], l);
        }
    }
    l->end = out;
}

// our list forgets its entries without deleting them (the pool they were in is gone)
static void tiles_forget(PartnerTiles& pt)
{
    if (pt.list) {
        pt.list->begin = pt.list->end = pt.list->fixed = pt.buf;
        pt.list->cap = pt.buf + kTileCap * 2;
    }
}

static void tiles_free(PartnerTiles& pt)
{
    delete pt.list; // a buffer the game grew it into (more than kTileCap tiles) is left to the game's heap
    delete[] pt.buf;
    pt.list = nullptr;
    pt.buf = nullptr;
}

// every tile of the list goes (the partner left)
static void tiles_drain(int, PartnerTiles& pt)
{
    if (pt.list)
        tiles_delete(pt.list, [](const int32_t*) { return false; });
}

// The world scale step 0xE73E50 (cdecl void(float dt), early in SimUpdate): while the scale G+0x514C
// moves towards G+0x5150 (a level change, over several frames) it rescales the world through
// 0xE6DB70, which empties the host's tile list and fills it again from EVERY tile object of the pool,
// one per key. Our tiles then sat in both lists (both freed them: faults at CreateObject+0x37 after
// `coop.pool food 1000`, lab 20261030-004900) or replaced the host's own tile of the same key (that
// one leaked). So our tiles go back to the pool before such a step, and no partner pass runs until
// the scale settles (h_PopTiles).
using WorldScale_t = void(SK_CDECL*)(float);
static WorldScale_t o_WorldScale;
static uintptr_t a_WorldScale;
static bool scale_moving()
{
    uint8_t* g = G();
    return g && F<float>(g, 0x514C) != F<float>(g, 0x5150);
}
static void SK_CDECL h_WorldScale(float dt)
{
    g_last_dt = dt;
    own_probe("step start");
    if (!g_ptiles.empty() && in_cell() && scale_moving())
        for (auto& [id, pt] : g_ptiles)
            if (pt.list && pt.list->begin != pt.list->end) {
                g_tiles_flush++;
                tiles_delete(pt.list, [](const int32_t*) { return false; });
            }
    o_WorldScale(dt);
}

static void partner_tiles_reset(bool drain)
{
    for (auto& [id, pt] : g_ptiles) {
        if (drain && in_cell())
            tiles_drain(0, pt);
        tiles_free(pt);
    }
    g_ptiles.clear();
    g_shadow_tiles.clear();
    g_starved.clear();
    g_views.clear();
    g_view_boxes.clear();
    g_union = false;
}

// debug: every tile object is in exactly one list (the host's or one of ours); log the first step
// where the pool's live count and the lists disagree
static uint32_t g_tile_checks_bad;
static void tiles_check(const char* step, int level)
{
    uint8_t* g = G();
    TileList* h = host_tiles();
    if (!g || !h || g_tile_checks_bad > 20)
        return;
    size_t n = (size_t)((h->end - h->begin) / 2), dead = 0;
    uint32_t first_dead = 0, first_key = 0;
    for (int32_t* e = h->begin; e < h->end; e += 2)
        if (!tile_alive((uint32_t)e[1]) && !dead++) {
            first_dead = (uint32_t)e[1];
            first_key = (uint32_t)e[0];
        }
    for (auto& [id, pt] : g_ptiles)
        if (pt.list) {
            n += (size_t)((pt.list->end - pt.list->begin) / 2);
            for (int32_t* e = pt.list->begin; e < pt.list->end; e += 2)
                dead += !tile_alive((uint32_t)e[1]);
        }
    uint32_t live = F<uint32_t>(g, 0x38 + 0xC);
    if (n != live || dead) {
        g_tile_checks_bad++;
        std::string fr;
        for (uint32_t i : g_freed_dbg)
            fr += " " + std::to_string(i & 0xFFFF) + "/" + std::to_string(i >> 16);
        H->log(M, SK_WARN, "coop: tiles: check after %s (level %d): pool live %u, list entries %u, dead entries %u (host: first dead %u/%u key %08X); we freed%s",
               step, level, live, (unsigned)n, (unsigned)dead, first_dead & 0xFFFF, first_dead >> 16, first_key, fr.c_str());
        if (n && h->begin < h->end)
            H->log(M, SK_WARN, "coop: tiles: host list first entries %08X:%u/%u %08X:%u/%u", (uint32_t)h->begin[0],
                   (uint32_t)h->begin[1] & 0xFFFF, (uint32_t)h->begin[1] >> 16, h->end - h->begin > 2 ? (uint32_t)h->begin[2] : 0,
                   h->end - h->begin > 2 ? (uint32_t)h->begin[3] & 0xFFFF : 0, h->end - h->begin > 2 ? (uint32_t)h->begin[3] >> 16 : 0);
    }
}

// BUG-0295: tiles of ours left empty for the cell budget are populated, a few per frame, once the
// host's pool has room again (still alive: the generation of the index matches)
static void feed_starved()
{
    uint8_t* g = G();
    if (!g || g_starved.empty())
        return;
    uint8_t* data = F<uint8_t*>(g, 0x38);
    uint32_t size = F<uint32_t>(g, 0x38 + 0x14);
    int n = 0;
    float* vb = view_boxes();
    for (auto it = g_starved.begin(); it != g_starved.end() && n < 4;) {
        uint32_t idx = it->first;
        if (!tile_alive(idx) || !g_shadow_tiles.count(idx)) {
            it = g_starved.erase(it);
            continue;
        }
        uint32_t live = cells_live();
        if (live >= kPopCapScreen || (live >= kPopCap - 250 && g_frame_tiles >= 2))
            break;
        uint32_t* t = (uint32_t*)(data + (size_t)(idx & 0xFFFF) * size);
        if (live >= kPopCap) { // between the caps: only a tile on its partner's screen (as starve_tile)
            bool core = false;
            for (size_t k = 0; vb && k < g_views.size() && k < g_view_boxes.size(); k++)
                if (g_views.list()[k].player == it->second) {
                    float save[12];
                    memcpy(save, vb, sizeof save);
                    memcpy(vb, g_view_boxes[k].data(), sizeof save);
                    core = partner_core(t);
                    memcpy(vb, save, sizeof save);
                    break;
                }
            if (!core) {
                ++it;
                continue;
            }
            g_tiles_core++;
        }
        // another list populated the same tile meanwhile: ours stays a plain shadow
        bool other = tile_elsewhere(cell_logic::tile_key((int32_t)t[1], (int32_t)t[2], (int32_t)t[3], (int32_t)t[4]), nullptr);
        it = g_starved.erase(it);
        if (other)
            continue;
        g_shadow_tiles.erase(idx);
        if (t[1] == 0) {
            o_PopTile(t, 0);
            g_tiles_fed++;
            g_frame_tiles++;
            n++;
        }
    }
}

static void SK_CDECL h_PopTiles(int level, TileList* list, int all)
{
    if (view_bad()) { // BUG-0299: the active set from NaN boxes is garbage
        g_nan_pop_skips++;
        return;
    }
    float* vb = view_boxes();
    bool ours = g_fix_partner_tiles && vb && list && list == host_tiles() && is_host() && in_cell() && C.puppets;
    if (!ours) {
        if (!g_ptiles.empty() || g_union)
            partner_tiles_reset(in_cell() && list == host_tiles()); // not hosting a Cell session any more: the lists are the old pool's
        bool own = list && list == host_tiles() && in_cell();
        if (own)
            g_tile_level = level;
        if (own)
            tiles_check("the frame between (no partner tiles)", level);
        o_PopTiles(level, list, all);
        if (own) {
            tiles_check("the own pass (no partner tiles)", level);
            static double last_log;
            double now = H->time_seconds();
            if (now - last_log > 10.0) {
                last_log = now;
                uint8_t* tp = G() + 0x38;
                trace::line("tiles", "pool %u/%u, own list %d, level %d (no partner tiles here); cell pool %u/%u",
                            F<uint32_t>(tp, 0xC), F<uint32_t>(tp, 0x18), (int)((list->end - list->begin) / 2), level,
                            F<uint32_t>(G(), 0x1C + 0xC), F<uint32_t>(G(), 0x1C + 0x18));
            }
        }
        return;
    }
    // A level change rescales the world (0xE6DB70, called by 0xE73E50 early in SimUpdate): it empties
    // the host's list and fills it again from EVERY tile object of the pool, ours included. Both
    // lists then held our tiles and both freed them (a double free: faults at CreateObject+0x37 after
    // `coop.pool food 1000`, lab 20261030-004900). Our tiles leave the host's list again here.
    if (!g_ptiles.empty()) {
        std::unordered_set<uint32_t> mine;
        for (auto& [id, pt] : g_ptiles)
            if (pt.list)
                for (int32_t* e = pt.list->begin; e < pt.list->end; e += 2)
                    mine.insert((uint32_t)e[1]);
        int32_t* out = list->begin;
        for (int32_t* e = list->begin; e < list->end; e += 2) {
            if (mine.count((uint32_t)e[1])) {
                g_tiles_unshared++;
                continue;
            }
            out[0] = e[0];
            out[1] = e[1];
            out += 2;
        }
        list->end = out;
    }
    tiles_check("the frame between", level);
    // a rebuild cleared the tile pool (RebuildLevel 0xE80010): our lists point at freed tiles
    for (auto& [id, pt] : g_ptiles)
        if (pt.list && pt.list->begin != pt.list->end && !tile_alive((uint32_t)pt.list->begin[1])) {
            tiles_forget(pt);
            g_tile_resets++;
        }
    if (g_shadow_tiles.size() > 4096)
        for (auto it = g_shadow_tiles.begin(); it != g_shadow_tiles.end();)
            it = tile_alive(*it) ? std::next(it) : g_shadow_tiles.erase(it);
    // the view centres: the host's view box, then every partner's cell
    double now = H->time_seconds();
    std::array<float, 12> host_box;
    memcpy(host_box.data(), vb, sizeof(float) * 12);
    views::Box hb{host_box[0], host_box[1], host_box[3], host_box[4]};
    g_views.clear();
    // the partners' centres: the one registry (views::note, coop.cpp: every partner shown in our
    // place), the same the Creature and Tribe stages use (BUG-0280); a host view that is not finite
    // gets no partner pass at all (BUG-0292)
    if (views::finite_pos({hb.cx(), hb.cy(), 0.f})) {
        g_views.set_host({hb.cx(), hb.cy(), 0.f});
        views::Centre pc[views::kMaxCentres];
        size_t n = views::partners(modes::kCell, now, pc, views::kMaxCentres - 1);
        for (size_t i = 0; i < n; i++)
            g_views.add(pc[i].player, {pc[i].pos.x, pc[i].pos.y, 0.f});
    }
    g_view_boxes.clear();
    for (auto& v : g_views.list()) {
        float dx = v.pos.x - hb.cx(), dy = v.pos.y - hb.cy();
        std::array<float, 12> b = host_box;
        for (int i : {0, 3})
            b[i] += dx;
        for (int i : {1, 4})
            b[i] += dy;
        if (!g_view_boxes.empty()) // a partner's background box is empty: no layer 1 tiles for it
            for (int i = 6; i < 12; i++)
                b[i] = 0;
        g_view_boxes.push_back(b);
    }
    g_union = g_view_boxes.size() > 1;
    // our deletes first (h_TileActive: any player's box), before the host's pass. On a level change
    // (the level base differs) or a rebuild (all), or when the tile pool is short of room, every tile
    // of ours goes back first: the host's own pass must never find the pool full (CreateObject then
    // returns 0 and the tile overwrites slot 0: faults at CreateObject+0x37 after `coop.pool food
    // 1000`, lab 20261030-004900). The partners' passes populate their areas again afterwards.
    static int last_level = INT_MIN;
    g_tile_level = level;
    uint8_t* tp = G() + 0x38;
    bool room = F<uint32_t>(tp, 0x18) >= F<uint32_t>(tp, 0xC) + 4 * 0x40;
    bool all_go = all || level != last_level || !room;
    last_level = level;
    for (auto& [id, pt] : g_ptiles) {
        if (all_go && pt.list && pt.list->begin != pt.list->end) {
            g_tiles_flush++;
            tiles_delete(pt.list, [](const int32_t*) { return false; });
        } else
            tiles_delete(pt.list, [](const int32_t* t) { return h_TileActive(t); });
    }
    tiles_check("our deletes", level);
    g_freed_dbg.clear();
    g_pass = 0;
    g_pass_list = list;
    g_frame_tiles = 0;
    o_PopTiles(level, list, all);
    tiles_check("the host's pass", level);
    if (!all && !scale_moving()) { // a rebuild (all) populates the host's view in one go, a moving
                                   // world scale refills the host's list from every tile: the partners follow
        // BUG-0295: the partners take turns being first, so the one that comes first does not take
        // all the room under the cell budget every frame
        static size_t turn;
        size_t np = g_views.size() - 1;
        turn++;
        for (size_t j = 0; j < np; j++) {
            size_t k = 1 + (turn + j) % np;
            auto& pt = g_ptiles[g_views.list()[k].player];
            pt.seen = now;
            if (!pt.list) {
                pt.buf = new int32_t[kTileCap * 2];
                pt.list = new TileList();
                memset(pt.list, 0, sizeof(TileList));
                tiles_forget(pt);
            }
            // the tile pool (cCellGame+0x38) has a fixed capacity and CreateObject returns 0 when it
            // is full: PopulateTiles then used slot 0 of another tile (a double free later, the free
            // list broken: 4060 faults at CreateObject+0x37, lab 20261030-002902). One call creates
            // at most 0x33 tiles: a partner pass runs only with room for that and the host's next.
            if (F<uint32_t>(tp, 0x18) < F<uint32_t>(tp, 0xC) + 4 * 0x40) {
                g_tiles_full++;
                continue;
            }
            memcpy(vb, g_view_boxes[k].data(), sizeof(float) * 12);
            g_pass = (int)k;
            g_pass_list = pt.list;
            o_PopTiles(level, pt.list, 0);
            g_tile_passes++;
            tiles_check("a partner's pass", level);
        }
        memcpy(vb, host_box.data(), sizeof(float) * 12);
        feed_starved();
    }
    g_pass = -1;
    g_pass_list = nullptr;
    g_cells_peak = std::max(g_cells_peak, cells_live());
    static double last_log;
    if (now - last_log > 10.0) { // the support log: how full the tile pool is with the partners' areas
        last_log = now;
        size_t ours = 0;
        for (auto& [id, pt] : g_ptiles)
            if (pt.list)
                ours += (size_t)((pt.list->end - pt.list->begin) / 2);
        trace::line("tiles", "pool %u/%u, host list %d, partner lists %u (%u tiles), level %d, flushes %u, full %u; cell pool %u/%u",
                    F<uint32_t>(tp, 0xC), F<uint32_t>(tp, 0x18), (int)((list->end - list->begin) / 2),
                    (unsigned)g_ptiles.size(), (unsigned)ours, level, g_tiles_flush, g_tiles_full,
                    F<uint32_t>(G(), 0x1C + 0xC), F<uint32_t>(G(), 0x1C + 0x18));
    }
    // a partner that left (or has no cell here any more): the game deletes its tiles
    for (auto it = g_ptiles.begin(); it != g_ptiles.end();) {
        if (!g_views.find((uint8_t)it->first) && now - it->second.seen > 2.0) {
            tiles_drain(level, it->second);
            tiles_free(it->second);
            it = g_ptiles.erase(it);
        } else
            ++it;
    }
}

const views::Set& view_centres() { return g_views; }

// ShouldNotAttack 0xE57480 (cdecl bool(attacker, victim)): the prey / threat tests of the NPC AI
// (IsPrey 0xE68910, IsThreat 0xE68870) and DamageCell ask it. Its "wontAttackPlayer(WhenSmall)"
// flags of a species (resource +0x300/+0x301) apply only when the victim is the avatar
// (G+0x411C). A partner's cell is a player too: for it the game answers as for the avatar, so an
// NPC that spares the host spares the guest as well (and the other way round).
static NoAttack_t o_NoAttack;
static NoAttack_t& o_NoAttack_ptr() { return o_NoAttack; }
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

// BUG-0306: PickTargets 0xE70CF0 (cdecl bool(cell*, resData*), docs/re/cell.md#cell-pick-stale) runs
// after the behaviour's liveness check 0xE70E30. A cell in a group (+0x38C) copies its leader's
// flee / chase target and timers without checking them; when the leader's chase target died
// and the leader has not run its own check yet (it comes later in the pool, or it skips the
// behaviours), 0xE79C30 passes the dead target to 0xE57CE0 as NULL and IsDamageable reads
// [0+0x108]. Wine swallows the fault and aborts the frame, so the leader never runs again and
// the follower faults every frame (8616 times in lab 20261030-104149). Drop a dead copied target
// here, as 0xE70E30 does on the next frame.
using PickTargets_t = bool(SK_CDECL*)(uint8_t*, void*);
static PickTargets_t o_PickTargets;
static uintptr_t a_PickTargets;
static uint32_t g_stale_targets;
static bool g_fix_pick_guard = true;
COOP_FIX("cell.pick_guard", "BUG-0306", g_fix_pick_guard, "cell: a group member drops a dead flee / chase target copied from its leader");
static bool SK_CDECL h_PickTargets(uint8_t* c, void* res)
{
    bool r = o_PickTargets(c, res);
    if (!g_fix_pick_guard || !c || !G() || modes::cell_is_puppet(c)) // a puppet keeps its -1 (modes.cpp)
        return r;
    uint32_t& chase = F<uint32_t>(c, kChase);
    if (chase && !modes::cell_at(chase)) {
        chase = 0;
        F<float>(c, 0x230) = 0.f; // chase timer
        g_stale_targets++;
    }
    uint32_t& flee = F<uint32_t>(c, kFlee);
    if (flee && !modes::cell_at(flee)) {
        flee = 0;
        F<float>(c, 0x21C) = 0.f; // flee timer
        g_stale_targets++;
    }
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
    if (fg && is_guest() && !g_solo) { // BUG-0205: our own pool while the host is away drops its own loot
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
// ---- soft-body particles (BUG-0338, docs/re/cell.md#cell-particle-pool) ----
// The cells' soft-body particles come from one pool of 1500 (G+0x4108, ParticleAlloc 0xE4DBA0).
// A full pool returns -1 and the game keeps it in the cell's list (CreateSoftBody 0xE6D8F0, the
// re-tier 0xE51A20 when a cell grows or shrinks; the re-tier even frees the -1 of a cell without
// particles). The game then reads and writes particle -1, 8 bytes before the pool's block (its heap
// header; a cell's centre from it jumped thousands of metres), and ParticleFree 0xE4EC10 (no bound
// check) puts -1 on the free list: the next allocations read garbage and fault every frame at
// 0xE4DBD9, and the count fell below what is used (-25 seen). The fix: the pool never gets full;
// a full pool's allocation gets the spare slot 1499 (the game never hands it out),
// set to the asking cell's position, and freeing it, or anything outside the pool, does nothing.
// The game itself never hands out slot 1499 (it refuses at 1499 used, its last slot is 1498).
// Old (`coop.fix cell.particles off`): the game's -1.
static bool g_fix_particles = true;
COOP_FIX_UNSAFE("cell.particles", "BUG-0338", g_fix_particles, "cell: a full soft-body particle pool (1500) gives a spare slot instead of the game's -1 (used before the pool's block, it broke the heap and the free list)");
constexpr int kParticles = 1500, kSpare = kParticles - 1;
static uintptr_t a_PartAlloc, a_PartFree;
static int(SK_CDECL* o_PartAlloc)(uint8_t*, const float*, const float*, float, int);
static void(SK_CDECL* o_PartFree)(uint8_t*, int);
static uint32_t g_part_spare, g_part_bad_frees, g_particles_peak;
static uintptr_t g_part_site[1500]; // RE (BUG-0355): the return address of each slot's last allocation
static int particles_used()
{
    uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
    return parts ? F<int>(parts, 0x1BD54) : -1;
}
// the game's own particle state: cells whose list holds -1 among real particles, and whether the
// free-list head points into the pool (it must while some slot below the high water is free)
static void particle_state(int* minus1_cells, int* head_bad)
{
    *minus1_cells = 0;
    *head_bad = 0;
    uint8_t* g = G();
    uint8_t* parts = g ? F<uint8_t*>(g, 0x4108) : nullptr;
    uint8_t* data = g ? F<uint8_t*>(g, 0x1C) : nullptr;
    if (!parts || !data)
        return;
    int used = F<int>(parts, 0x1BD54), high = F<int>(parts, 0x1BD58), head = F<int>(parts, 0x1BD5C);
    *head_bad = used < high && (head < 0 || head >= kParticles);
    uint32_t n = F<uint32_t>(g, 0x1C + 0x10);
    for (uint32_t slot = 0; slot < n && slot < 4096; slot++) {
        uint8_t* c = data + (size_t)slot * 0x398;
        if ((F<uint32_t>(c, 0) & 0xFFFF) != slot || !F<uint32_t>(c, 0x248))
            continue;
        int k = F<int>(c, 0x250);
        if (k > 1 && k <= 64)
            for (int i = 0; i < k; i++)
                if (F<int>(c, 0x254 + 4 * i) < 0) {
                    (*minus1_cells)++;
                    break;
                }
    }
}
// ---- the cells' graphics pool (BUG-0355, docs/re/cell.md#cell-gfx-pool) ----
// A foreground cell gets a soft body when it comes into the active area (SoftBodyLod 0xE78C00 ->
// CreateSoftBody 0xE6D8F0): its particles first, then a graphics object from the cell graphics pool
// (cCellGFX +0x168). That pool has a fixed capacity; full, it hands out 0, the cell keeps +0x248 = 0
// with its particles in the list, and the next frame's LOD pass asks again: 4 to 64 particles lost
// per such cell per frame (DestroyCell / FreeSoftBody free only with +0x248 set), the graphics
// object Get(0) set up again each time. A 4-player guest's crowd of mirrors on screen filled the
// particle pool in seconds that way, then every cell shared BUG-0338's spare slot (it went NaN and
// spread), and the frame took 300 ms for good. The fix: no CreateSoftBody while the graphics pool
// is full; the cell stays as it was (no soft body, not drawn: what the game meant), the LOD asks
// again next frame and gets one when there is room.
// Old (`coop.fix cell.gfx_full off`): the game's call, the leak.
static bool g_fix_gfx_full = true;
COOP_FIX("cell.gfx_full", "BUG-0355", g_fix_gfx_full, "cell: no soft body is made while the cell graphics pool is full (the game lost the particles every frame: the particle pool filled, 3 fps)");
static uintptr_t a_CreateSoft, a_GfxPtr;
static void(SK_CDECL* o_CreateSoft)(uint32_t, int);
static uint32_t g_soft_refused, g_soft_made, g_gfx_peak, g_soft_count0;
static uint32_t* gfx_pool() // cObjectPool_ fields as uint32: [3] live, [6] capacity
{
    uint8_t* gfx = a_GfxPtr ? *(uint8_t**)a_GfxPtr : nullptr;
    return gfx ? (uint32_t*)(gfx + 0x168) : nullptr;
}
static void SK_CDECL h_CreateSoft(uint32_t idx, int arg)
{
    uint32_t* gp = gfx_pool();
    if (gp && gp[3] > g_gfx_peak)
        g_gfx_peak = gp[3];
    if (g_fix_gfx_full && gp && gp[6] && gp[3] >= gp[6]) {
        if (!g_soft_refused++)
            H->log(M, SK_WARN, "cell: the cell graphics pool is full (%u of %u): no soft body for new cells on screen until there is room (BUG-0355)",
                   gp[3], gp[6]);
        // a new cell has count 0 (the pool's memset) until its first CreateSoftBody: give it the
        // game's own "no particles" form (count 1, -1), as CreateSoftBody does for tier 0
        if (uint8_t* c = modes::cell_at(idx))
            if (F<int>(c, 0x250) < 1) {
                F<int>(c, 0x250) = 1;
                F<int>(c, 0x254) = -1;
                g_soft_count0++;
            }
        return;
    }
    g_soft_made++;
    o_CreateSoft(idx, arg);
}

// ---- room in the particle pool (BUG-0355, docs/re/cell.md#cell-gfx-pool) ----
// BUG-0338's spare slot is one particle for every cell that asks a full pool: a 4-player guest with
// 900 mirrors filled the pool in ordinary play, hundreds of cells then held slot 1499 (up to 64
// times each, the own cell 56 of its 64 when it grew in the end-of-Cell cinematic), every soft-body
// step pulled it towards its own cell, it went NaN within seconds and the NaN spread through every
// cell holding it (their centres are the mean of their particles; guest_tick moves a mirror's
// particles by the NaN difference). The fix: a full pool frees the soft bodies of the cells
// farthest from our own (not our cell, not a partner's puppet, not the cell asking), down to
// kRoomTo used, and hands out a real slot; such a cell keeps its tier and becomes the game's own
// "no particles" form (+0x250 = 1, +0x254 = -1, as a model type 0 / 5 cell has), moved without a
// soft body. A cell left holding the spare (no cell to take from) is freed the same way once a
// frame, and the spare is put back to a finite place.
// Old (`coop.fix cell.spare off`): BUG-0338's shared spare slot.
static bool g_fix_spare = true;
COOP_FIX("cell.spare", "BUG-0355", g_fix_spare, "cell: a full soft-body particle pool frees the particles of the cells farthest from ours instead of handing many cells one shared spare slot (it went NaN and spread)");
constexpr int kRoomTo = 1300; // used particles after a full pool made room (our cell at tier 3 takes 64)
static uint32_t g_part_room_runs, g_part_stripped, g_part_spare_freed, g_bare_moves;
// A mirror whose soft body cell.spare freed was moved like a mirror without one (tier 0): steered by
// its velocity towards the host's pose (set_pos only when over 3 m off). A mirror made from a full
// pool lands on top of its neighbours and the cells around ours; steered, they shoved our own cell
// 8-18 m in a few seconds (cell_particles: 2 of 5 runs, BUG-0355 follow-up). Placed as a soft one
// is (the host's pose, at rest), they do not (own cell at most 1.1 m per round in 6 runs).
// Old (`coop.fix cell.bare_still off`): steered.
static bool g_bare_still = true;
COOP_FIX("cell.bare_still", "BUG-0355", g_bare_still, "cell: a guest's mirror whose soft body was freed for room in the particle pool is placed at the host's pose at rest, not steered into the cells around it");
// the cell's soft body to the game's "no particles" form; returns the particles given back
static int strip_soft_body(uint8_t* parts, uint8_t* c)
{
    int n = F<int>(c, 0x250);
    if (!F<uint32_t>(c, 0x248) || n < 2 || n > 64)
        return 0;
    int freed = 0;
    for (int i = 0; i < n; i++) {
        int k = F<int>(c, 0x254 + 4 * i);
        if (k >= 0 && k < kSpare) {
            o_PartFree(parts, k);
            freed++;
        }
        F<int>(c, 0x254 + 4 * i) = -1;
    }
    F<int>(c, 0x250) = 1;
    g_part_stripped++;
    return freed;
}
// a full pool: free the soft bodies of the farthest cells (not ours, no puppet, not `asking`)
static void make_particle_room(uint8_t* parts, int asking)
{
    uint8_t* av = avatar();
    net::Vec3 me = av ? pos_of(av) : net::Vec3{0, 0, 0};
    std::vector<std::pair<float, uint8_t*>> takers;
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        int n = F<int>(c, 0x250);
        if (!F<uint32_t>(c, 0x248) || n < 2 || n > 64 || (idx & 0xFFFF) == ((uint32_t)asking & 0xFFFF) ||
            modes::cell_is_puppet(c))
            return;
        net::Vec3 p = pos_of(c);
        float d = std::hypot(p.x - me.x, p.y - me.y);
        takers.push_back({std::isfinite(d) ? d : 1e30f, c}); // a NaN cell first
    });
    std::sort(takers.begin(), takers.end(), [](auto& x, auto& y) { return x.first > y.first; });
    g_part_room_runs++;
    for (auto& [d, c] : takers) {
        if (F<int>(parts, 0x1BD54) <= kRoomTo)
            break;
        strip_soft_body(parts, c);
    }
}
// once a frame: no cell but ours (or a puppet) keeps the spare, the spare stays finite
static void spare_tick()
{
    uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
    if (!g_fix_spare || !g_fix_particles || !parts || !g_part_spare)
        return;
    uint8_t* av = avatar();
    for_each_cell([&](uint8_t* c, uint32_t) {
        int n = F<int>(c, 0x250);
        if (!F<uint32_t>(c, 0x248) || n < 2 || n > 64 || modes::cell_is_puppet(c))
            return;
        for (int i = 0; i < n; i++)
            if (F<int>(c, 0x254 + 4 * i) == kSpare) {
                strip_soft_body(parts, c);
                g_part_spare_freed++;
                return;
            }
    });
    auto* q = (float*)(parts + 4 + 12 * kSpare);
    auto* v = (float*)(parts + 0x4654 + 12 * kSpare);
    if (!std::isfinite(q[0]) || !std::isfinite(q[1]) || !std::isfinite(q[2]) || !std::isfinite(v[0]) || !std::isfinite(v[1])) {
        net::Vec3 me = av ? pos_of(av) : net::Vec3{0, 0, 0};
        q[0] = std::isfinite(me.x) ? me.x : 0.f, q[1] = std::isfinite(me.y) ? me.y : 0.f, q[2] = 0.f;
        v[0] = v[1] = v[2] = 0.f;
    }
}
static int SK_CDECL h_PartAlloc(uint8_t* parts, const float* pos, const float* vel, float w, int cell)
{
    int used = parts ? F<int>(parts, 0x1BD54) : 0;
    if (used > (int)g_particles_peak)
        g_particles_peak = (uint32_t)used;
    // The game's own last slot is 1499 too: with no free slot below the high water it hands out
    // index = the new count (slots 1..1499; 1499 at 1498 used), so BUG-0338's spare could be a
    // real cell's particle (and its free was dropped: one slot lost). With cell.spare the pool
    // counts as full at 1498 used and slot 1499 is the spare alone.
    int full_at = g_fix_spare ? kParticles - 2 : kParticles - 1;
    if (g_fix_spare && g_fix_particles && parts && used >= full_at && o_PartFree) {
        make_particle_room(parts, cell);
        used = F<int>(parts, 0x1BD54);
    }
    if (!g_fix_particles || !parts || used < full_at) {
        int k = o_PartAlloc(parts, pos, vel, w, cell);
        if (k >= 0 && k < kParticles) // RE (BUG-0355): who took each slot (coop.pool orphans)
            g_part_site[k] = (uintptr_t)__builtin_return_address(0);
        return k;
    }
    for (int j = 0; j < 3; j++) { // the spare stands where the asking cell is, at rest
        ((float*)(parts + 4 + 12 * kSpare))[j] = pos ? pos[j] : 0.f;
        ((float*)(parts + 0x4654 + 12 * kSpare))[j] = vel ? vel[j] : 0.f;
    }
    if (!g_part_spare++)
        H->log(M, SK_WARN, "cell: the soft-body particle pool is full (%d of %d): the spare slot instead of -1 (BUG-0338)", used, kParticles);
    return kSpare;
}
static void SK_CDECL h_PartFree(uint8_t* parts, int i)
{
    if (g_fix_particles && (i < 0 || i >= kSpare)) {
        g_part_bad_frees++;
        return;
    }
    o_PartFree(parts, i);
}

using Soft_t = void(SK_CDECL*)(uint8_t*, void*, float);
static Soft_t o_Soft;
static void SK_CDECL h_Soft(uint8_t* c, void* res, float dt)
{
    bool own = c && c == avatar();
    if (own)
        own_probe("before the soft body");
    o_Soft(c, res, dt);
    if (own)
        own_probe("soft body");
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

// ---- the cell broadphase's sweep (BUG-0337, docs/re/cell.md#cell-sap-active-list) ----
// SweepPrune::UpdatePairs 0xBBB450 sweeps axis 0 with the open objects in an int[1024] on its
// stack and no bound check: the 1025th open object overwrites its return address (and the next
// ones the caller's frame, SimUpdate's), so `ret` jumps to a cell's broadphase id (0x196, 0x4D6...)
// every frame. The fix runs the same sweep with an open list on the heap: the same merges (the
// game's MergeAxis), the same pair test (the game's), the same pairs in the same order.
// Old (`coop.fix cell.sap_sweep off`): the game's UpdatePairs.
static bool g_fix_sap_sweep = true;
COOP_FIX_UNSAFE("cell.sap_sweep", "BUG-0337", g_fix_sap_sweep, "cell: the broadphase sweep keeps its open objects on the heap, not in the game's int[1024] on the stack (over 1024 open objects overwrote UpdatePairs' return address)");
static uintptr_t a_UpdatePairs, a_MergeAxis, a_Overlap;
static void(__cdecl* o_UpdatePairs)(uint8_t*);
static uint32_t g_sap_over, g_sap_peak, g_sap_sweeps, g_sap_left;
static uint32_t g_sap_pairs, g_sap_pair_cap, g_sap_pairs_full; // tests (BUG-0342): pairs of the last sweep, the pair list size, sweeps that filled it
static std::string sap_report(); // sweeps with over 1024 open objects, the most open at once, sweeps of ours
static void __cdecl h_UpdatePairs(uint8_t* s)
{
    if (!g_fix_sap_sweep || !s || !a_MergeAxis || !a_Overlap)
        return o_UpdatePairs(s);
    int n = 0;
    for (int axis = 0; axis < 2; axis++)
        n = skg::cdecl_call<int>(a_MergeAxis, s, F<void*>(s, 0x24 + 4 * axis), F<int>(s, 0x20), F<void*>(s, 0x30 + 4 * axis),
                                 F<int>(s, 0x2C), axis);
    F<int>(s, 0x20) = n;
    F<int>(s, 0x2C) = 0;
    F<int>(s, 8) = 0;
    g_sap_sweeps++;
    static std::vector<int> open; // reused: no allocation per frame
    open.clear();
    uint32_t peak = 0;
    uint8_t* ep = F<uint8_t*>(s, 0x24);
    uint8_t* objs = F<uint8_t*>(s, 0x14);
    for (int i = 0; i < n; i++) {
        int id = F<int16_t>(ep, 8 * i + 2);
        uint8_t* o = objs + 16 * id;
        if (F<int16_t>(ep, 8 * i) == 0) {
            for (int other : open) {
                uint8_t* p = objs + 16 * other;
                if (skg::cdecl_call<uint8_t>(a_Overlap, s, 0, o, p) && F<int>(s, 8) != F<int>(s, 0)) {
                    int a = F<int>(o, 0), b = F<int>(p, 0);
                    int* pair = (int*)(F<uint8_t*>(s, 0xC) + 8 * (size_t)F<int>(s, 8));
                    pair[0] = std::min(a, b);
                    pair[1] = std::max(a, b);
                    F<int>(s, 8)++;
                }
            }
            open.push_back(id);
            peak = std::max(peak, (uint32_t)open.size());
        } else {
            auto it = std::find(open.begin(), open.end(), id);
            if (it != open.end()) {
                *it = open.back();
                open.pop_back();
            }
        }
    }
    g_sap_pairs = (uint32_t)F<int>(s, 8);
    g_sap_pair_cap = (uint32_t)F<int>(s, 0);
    g_sap_pairs_full += g_sap_pairs >= g_sap_pair_cap;
    g_sap_peak = std::max(g_sap_peak, peak);
    g_sap_left = std::max(g_sap_left, (uint32_t)open.size());
    if (peak > 1024 && !g_sap_over++) // the first one per session (the count is in `coop.pool sap`)
        H->log(M, SK_WARN, "cell: the broadphase sweep held %u open objects (the game's list holds 1024, BUG-0337)", peak);
    // objects still open after the sweep have their max endpoint before their min: a broken order
    static double s_told;
    if (open.size() > 64 && H->time_seconds() - s_told > 30) {
        s_told = H->time_seconds();
        H->log(M, SK_WARN, "cell: %u broadphase objects left open by the sweep (BUG-0337): %s", (unsigned)open.size(),
               sap_report().c_str());
    }
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
    a_CellUI = H->sym("Simulator::Cell::cCellUI::_ptr"); // tests only (rollover_of)
    a_DnaFor = H->sym("Simulator::Cell::DNAForFood"); // BUG-0351 diagnostics (dna_note)
    a_GetDamage = H->sym("Simulator::Cell::GetDamageAmount");
    a_TargetParams = H->sym("Simulator::Cell::GetTargetSearchParams");
    a_IsPrey = H->sym("Simulator::Cell::IsPrey");
    a_ModelData = H->sym("Simulator::Cell::GetCellModelData");
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
    a_PopTiles = H->sym("Simulator::Cell::PopulateTiles");
    a_PopTile = H->sym("Simulator::Cell::PopulateTile");
    a_TileActive = H->sym("Simulator::Cell::IsTileActive");
    a_ViewBoxes = H->sym("Simulator::Cell::ViewBoxes");
    a_WorldScale = H->sym("Simulator::Cell::WorldScaleStep");
    a_PickTargets = H->sym("Simulator::Cell::PickTargets");
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
    if (a_PopTiles && a_PopTile && a_TileActive && a_ViewBoxes && a_WorldScale) { // GOG 2024 only (BUG-0174)
        H->hook_create(M, (void*)a_WorldScale, (void*)h_WorldScale, (void**)&o_WorldScale);
        H->hook_create(M, (void*)a_PopTiles, (void*)h_PopTiles, (void**)&o_PopTiles);
        H->hook_create(M, (void*)a_PopTile, (void*)h_PopTile, (void**)&o_PopTile);
        H->hook_create(M, (void*)a_TileActive, (void*)h_TileActive, (void**)&o_TileActive);
    } else
        a_ViewBoxes = 0;
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
    if (a_PickTargets) // BUG-0306
        H->hook_create(M, (void*)a_PickTargets, (void*)h_PickTargets, (void**)&o_PickTargets);
    a_CreateSoft = H->sym("Simulator::Cell::CreateSoftBody");
    a_GfxPtr = H->sym("Simulator::Cell::cCellGFX::_ptr");
    if (a_CreateSoft && a_GfxPtr) // GOG 2024 only (BUG-0355)
        H->hook_create(M, (void*)a_CreateSoft, (void*)h_CreateSoft, (void**)&o_CreateSoft);
    a_PartAlloc = H->sym("Simulator::Cell::ParticleAlloc");
    a_PartFree = H->sym("Simulator::Cell::ParticleFree");
    if (a_PartAlloc && a_PartFree) { // GOG 2024 only (BUG-0338)
        H->hook_create(M, (void*)a_PartAlloc, (void*)h_PartAlloc, (void**)&o_PartAlloc);
        H->hook_create(M, (void*)a_PartFree, (void*)h_PartFree, (void**)&o_PartFree);
    }
    a_UpdatePairs = H->sym("SweepPrune::UpdatePairs");
    a_MergeAxis = H->sym("SweepPrune::MergeAxis");
    a_Overlap = H->sym("SweepPrune::OverlapOther");
    if (a_UpdatePairs && a_MergeAxis && a_Overlap) // GOG 2024 only (BUG-0337)
        H->hook_create(M, (void*)a_UpdatePairs, (void*)h_UpdatePairs, (void**)&o_UpdatePairs);
    if (a_EatFood) // BUG-0364
        H->hook_create(M, (void*)a_EatFood, (void*)h_EatFood, (void**)&o_EatFood);
    if (a_ChunkA && a_ChunkB) {
        H->hook_create(M, (void*)a_ChunkA, (void*)h_ChunkA, (void**)&o_ChunkA);
        H->hook_create(M, (void*)a_ChunkB, (void*)h_ChunkB, (void**)&o_ChunkB);
    }
    return true;
}

// ---------------- host ----------------
struct SendCell {
    uint8_t* c;
    uint32_t idx;
};
static void send_state(int dest, const std::vector<SendCell>& cells);

static void heal_own(uint8_t* a, const net::Vec3& me, bool bad);
static void host_tick(double now)
{
    own_probe("host tick");
    if (uint8_t* a = avatar()) // BUG-0299 on the host too
        heal_own(a, pos_of(a), !views::finite_pos(pos_of(a)));
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
    using E = SendCell;
    // BUG-0174: each guest gets the cells within 120 m of its own cell (the host populates around it
    // too, h_PopTiles); a guest without a centre yet, or `fix cell.partner_tiles off`: around the host.
    std::vector<E> all;
    bool per_guest = g_fix_partner_tiles && g_views.size() > 1;
    for_each_cell([&](uint8_t* c, uint32_t idx) {
        if (modes::cell_is_puppet(c) || !F<void*>(c, kRes))
            return;
        net::Vec3 p = pos_of(c);
        if (g_fix_finite && !pose_finite(p, F<float>(c, kVel), F<float>(c, kVel + 4), F<float>(c, kSize))) {
            if (!g_nonfinite_sent_skipped++)
                H->log(M, SK_WARN, "coop: cell: host cell %08X has a pose that is not finite: not sent (BUG-0292)", idx);
            return;
        }
        if (per_guest ? !g_views.near_any(p.x, p.y, 120.f) && net::dist(p, me) > 120.f : net::dist(p, me) > 120.f)
            return;
        all.push_back({c, idx});
    });
    g_cycle++;
    for (auto& pl : C.sess->players()) {
        if (pl.id == 0)
            continue;
        const views::Centre* vc = per_guest ? g_views.find((uint8_t)pl.id) : nullptr;
        std::vector<E> mine;
        const std::vector<E>* list = &all;
        if (vc) {
            for (auto& e : all) {
                net::Vec3 p = pos_of(e.c);
                if (std::hypot(p.x - vc->pos.x, p.y - vc->pos.y) <= 120.f)
                    mine.push_back(e);
            }
            list = &mine;
        }
        // BUG-0295: at most kSendCap cells per guest, the nearest to it (its pool and the game's
        // broadphase behind it hold ~2048 cells; 4 players together had ~2300 within 120 m)
        if (g_fix_budget && list->size() > kSendCap) {
            float cx = vc ? vc->pos.x : me.x, cy = vc ? vc->pos.y : me.y;
            std::vector<std::pair<float, size_t>> d;
            d.reserve(list->size());
            for (size_t i = 0; i < list->size(); i++) {
                net::Vec3 p = pos_of((*list)[i].c);
                d.push_back({(p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy), i});
            }
            std::nth_element(d.begin(), d.begin() + kSendCap, d.end());
            std::vector<E> closest;
            closest.reserve(kSendCap);
            for (size_t i = 0; i < kSendCap; i++)
                closest.push_back((*list)[d[i].second]);
            g_send_capped++;
            send_state(pl.id, closest);
            continue;
        }
        send_state(pl.id, *list);
    }
}

static void send_state(int dest, const std::vector<SendCell>& cells)
{
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
        C.sess->send_unreliable_to(dest, net::MSG_CELL_STATE, w);
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
        if (g_fix_finite && (!pose_finite(p, v.x, v.y, size) || !std::isfinite(q.x) || !std::isfinite(q.y) ||
                             !std::isfinite(q.z) || !std::isfinite(q.w))) {
            if (!g_nonfinite_dropped++)
                H->log(M, SK_WARN, "coop: cell: host cell %08X arrived with a pose that is not finite: not mirrored (BUG-0292)", hidx);
            continue; // not in this round: its mirror goes with the round's purge
        }
        g_cycle_seen.insert(hidx);
        auto& m = g_mirrors[hidx];
        m.pos = p; m.vel = v; m.rot = q; m.size = size; m.hp = hp; m.seen = now;
        uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr;
        if (!c && g_fix_budget && cells_live() >= kMirrorCap) {
            g_mirror_capped++; // BUG-0295: the pool is full enough (an older host sends more)
            continue;
        }
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
    g_cells_peak = std::max(g_cells_peak, cells_live());
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

// BUG-0299: our own cell back at its last finite position, its soft-body particles on a small ring
// around it and at rest (their old positions are NaN too)
static net::Vec3 g_own_good;
static bool g_own_good_set;
// BUG-0307: the heal of BUG-0299 fixed the position and the particles' positions and velocities only.
// A NaN left in the rotation / shape matrices, the size or the particles' goal offsets (the rest shape
// of the shape matching) made the next soft-body step NaN again: the own cell went NaN 26 times in
// 6 minutes in lab 20261030-104149. With cell.nan_heal_all every non-finite field is repaired, and
// any non-finite field (not only the position) starts the heal.
static bool g_fix_nan_heal_all = true;
COOP_FIX("cell.nan_heal_all", "BUG-0307", g_fix_nan_heal_all, "cell: the own cell's heal also repairs its matrices, size, velocity and the particles' rest shape, and starts on any non-finite field");
static float g_own_good_size = 0.5f, g_own_good_tsize = 0.5f, g_own_good_mass = 1.f, g_own_good_imass = 1.f;
static void heal_own(uint8_t* a, const net::Vec3& me, bool bad)
{
    unsigned mask = g_fix_nan_heal_all ? cell_nonfinite(a) : 0;
    if (!bad && !mask) {
        g_own_good = me;
        g_own_good_set = true;
        g_own_good_size = F<float>(a, kSize);
        g_own_good_tsize = F<float>(a, kTargetSize);
        g_own_good_mass = F<float>(a, 0xB8);
        g_own_good_imass = F<float>(a, 0xBC);
        return;
    }
    if (!g_fix_nan_guard || !g_own_good_set)
        return;
    const net::Vec3 p = g_fix_nan_heal_all && !bad ? me : g_own_good;
    if (g_fix_nan_heal_all) {
        if (mask & NF_SIZE) {
            F<float>(a, kSize) = g_own_good_size;
            F<float>(a, kTargetSize) = g_own_good_tsize;
        }
        if (mask & NF_MASS) {
            F<float>(a, 0xB8) = g_own_good_mass;
            F<float>(a, 0xBC) = g_own_good_imass;
        }
        if (mask & NF_ROT) {
            static const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
            memcpy(a + kRot, id, sizeof id);
            F<uint16_t>(a, 0x48) |= 2;
        }
        if (mask & NF_SHAPE) {
            static const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
            memcpy(a + 0x24, id, sizeof id);
        }
    }
    float r = std::isfinite(F<float>(a, kSize)) ? std::clamp(F<float>(a, kSize) * 0.4f, 0.05f, 5.f) : 0.3f;
    int n = F<int>(a, 0x250);
    uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
    if (F<uint32_t>(a, 0x248) && parts && n > 1 && n <= 64)
        for (int i = 0; i < n; i++) {
            uint32_t k = F<uint32_t>(a, 0x254 + 4 * i);
            if (k >= 1500)
                continue;
            auto* q = (float*)(parts + 4 + 12 * (size_t)k);
            auto* v = (float*)(parts + 0x4654 + 12 * (size_t)k);
            float ang = 6.2831853f * (float)i / (float)n;
            q[0] = p.x + r * std::cos(ang);
            q[1] = p.y + r * std::sin(ang);
            q[2] = p.z;
            v[0] = v[1] = v[2] = 0;
            auto* goal = (float*)(parts + 0x11944 + 12 * (size_t)k); // the rest offset of the shape matching
            if (g_fix_nan_heal_all && !fin(goal, 3)) {
                goal[0] = r * std::cos(ang);
                goal[1] = r * std::sin(ang);
                goal[2] = 0;
            }
        }
    auto* pos = (float*)(a + 0x4C);
    auto* prev = (float*)(a + 0x84);
    pos[0] = prev[0] = p.x;
    pos[1] = prev[1] = p.y;
    pos[2] = prev[2] = p.z;
    F<uint16_t>(a, 0x48) |= 4;
    F<uint16_t>(a, 0x4A)++;
    stop_at(a, p);
    if (!g_nan_heals++ || (mask && g_nan_heals <= 20))
        H->log(M, SK_WARN, "coop: cell: our own cell went back to its last finite position (%.1f %.1f), non-finite fields 0x%X (BUG-0299, BUG-0307)",
               p.x, p.y, mask);
}

// BUG-0352: after a world load the guest's cell starts at the pool's start, and only an own-cell
// rebuild (creations.cpp apply_own_cell -> join_partner) moved it to the host: a load without one
// (every load after the first of a session, BUG-0349) left it up to 130 m away, outside the host's
// tiles. Every new own avatar (a load, an editor, a rebuild) now goes next to the host once the host's
// cell is known here (its state says the same place), at most 30 s after the avatar appeared.
// `coop.fix cell.join_after_load off`: only after an own-cell rebuild (0.7.3.1).
static bool g_join_load_fix = true;
COOP_FIX("cell.join_after_load", "BUG-0352", g_join_load_fix, "cell: a guest goes next to the host after every Cell load");
static uint32_t g_join_avatar;    // the avatar index this was decided for
static double g_join_since = -1;  // when that avatar appeared (-1: decided)
static double g_join_check = -1;  // when to check that the move held
static net::Vec3 g_join_to;       // where it was put
static int g_join_tries;
static uint32_t g_joins_load;     // tests: guest moved next to the host after a new avatar
static void join_after_load(double now)
{
    uint32_t idx = avatar_idx();
    if (idx != g_join_avatar) {
        g_join_avatar = idx;
        g_join_since = now;
        g_join_check = -1;
        g_join_tries = 0;
    }
    uint8_t* a = avatar();
    if (!g_join_load_fix || !a)
        return;
    if (g_join_check >= 0 && now >= g_join_check) { // the game's hatch can put the cell back at the start
        g_join_check = -1;
        if (net::dist(pos_of(a), g_join_to) > 15.f && g_join_tries < 3)
            g_join_since = now - 2.0; // again
    }
    if (g_join_since < 0 || now - g_join_since < 2.0)
        return;
    if (now - g_join_since > 30.0) {
        g_join_since = -1; // the host never showed up here: leave it
        return;
    }
    net::Vec3 at;
    float d = -1;
    // not during the hatch (the game's input lock after a load or a rebuild), not knocked out, and
    // only once the host's state is here
    if (F<uint8_t>(a, kDying) || g_downed || coop_ui::input_lock_now() > 0 || nearest_partner(1e9f, &at, &d).empty())
        return;
    g_join_since = -1;
    if (d < 15.f)
        return;
    g_join_to = {at.x + 2.5f, at.y + 1.f, 0.f};
    stop_at(a, g_join_to);
    g_join_tries++;
    g_join_check = now + 3.0;
    g_joins_load++;
    H->log(M, SK_INFO, "cell: our new cell joined the host (%.0f m away after the load, try %d)", d, g_join_tries);
}

static void guest_tick(double now)
{
    join_after_load(now);
    flush_food();
    own_probe("guest tick");
    shared_parts_scan(now);
    // BUG-0292 diagnostics: the moment our own cell's position stops being finite, and what was near
    static bool own_bad;
    if (uint8_t* a = avatar()) {
        net::Vec3 me = pos_of(a);
        bool bad = !views::finite_pos(me);
        if (bad && !own_bad) {
            size_t odd = 0;
            for (auto& [hidx, m] : g_mirrors)
                odd += !pose_finite(m.pos, m.vel.x, m.vel.y, m.size);
            H->log(M, SK_WARN, "coop: cell: our own cell's position is not finite (BUG-0292): %u mirrors, %u of them with a pose that is not finite, %u dropped so far",
                   (unsigned)g_mirrors.size(), (unsigned)odd, g_nonfinite_dropped);
        }
        own_bad = bad;
        heal_own(a, me, bad);
    }
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
        } else if (g_bare_still && F<uint32_t>(c, 0x248) && n == 1 && F<int>(c, 0x24C) >= 1) {
            // a mirror whose soft body we freed (BUG-0355, cell.spare): placed as a soft one is (the
            // host's pose, at rest), not steered by velocity into the cells around it
            set_pos(c, want);
            auto* v = (float*)(c + kVel);
            v[0] = v[1] = 0.f;
            g_bare_moves++;
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
    own_probe("mirror moves");
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

// BUG-0205: the guest's own pool while the host is away (see g_fix_away)
static void solo_reset()
{
    if (g_solo_food[0] || g_solo_food[1]) {
        g_solo_dropped++;
        trace::ev("eat", "solo dropped", "food kept while the host was away dropped (meat %d plant %d): the stage or "
                  "the session ended first", g_solo_food[0], g_solo_food[1]);
    }
    g_solo = g_solo_owed = false;
    g_solo_food[0] = g_solo_food[1] = 0;
}

static void solo_start()
{
    // the host's pool is not simulated now: its mirrors become cells of our game, with their AI.
    // Dying and dead ones keep the locks of their death.
    for (auto& [h, m] : g_mirrors) {
        uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr;
        if (!c || F<uint8_t>(c, kDying) || F<uint8_t>(c, kDead))
            continue;
        F<uint8_t>(c, kAiLock) = 0;
        F<uint8_t>(c, kKeepAlive) = 0; // the tile streaming owns them again
        g_solo_released++;
    }
    g_mirrors.clear();
    g_host_of.clear();
    g_touched.clear();
    g_cycle_seen.clear();
    g_rounds.reset();
    if (g_pend_food) { // not sent yet: kept with the rest
        g_solo_food[g_pend_plant ? 1 : 0] += g_pend_food;
        g_pend_food = 0;
    }
    g_solo = true;
    g_solo_owed = false;
    g_solo_times++;
    trace::line("cell", "host away: the guest plays its own pool (%u cells released)", g_solo_released);
}

// the host is back and its pool runs again (its first state or total arrived): what we ate meanwhile
// goes to it as meals; the totals it sends back count them (BUG-0051)
static void solo_send()
{
    g_solo_owed = false;
    for (int k = 0; k < 2; k++)
        while (g_solo_food[k] > 0) {
            int n = std::min(g_solo_food[k], 999); // the host refuses one meal of 1000 or more
            g_solo_food[k] -= n;
            net::Writer w;
            w.u32((uint32_t)n);
            w.u8((uint8_t)k);
            w.u32(g_meals.sent(n));
            C.sess->send_reliable_to(0, net::MSG_CELL_FOOD, w);
            g_solo_sent += n;
            trace::ev("eat", "solo sent", "host back: food +%d %s eaten while it was away sent", n, k ? "plant" : "meat");
        }
    g_solo_food[0] = g_solo_food[1] = 0;
}

static void solo_tick()
{
    bool want = g_fix_away && is_guest() && in_cell() && C.host_away && C.host_away();
    if (want == g_solo)
        return;
    if (want) {
        solo_start();
        return;
    }
    if (is_guest() && in_cell()) { // the host is back: its pool replaces ours at its first state
        g_solo = false;
        g_solo_owed = g_solo_food[0] || g_solo_food[1];
        trace::line("cell", "host back: the host's pool replaces the guest's at its first state");
    } else
        solo_reset();
}

// BUG-0314: the game populates the background decoration (query G+0x4100) of the tiles around the
// camera and frees those cells only on a level change (RemoveCellsOfOldScale). A cell that jumps far
// again and again (BUG-0307's NaN heals, 100-200 m each) left them behind at every place: 1099
// background cells after 40 jumps, both roles; a guest's pool reached its mirror cap (1500 live
// cells, BUG-0295) and it created no mirror any more (coverage 1% in lab 20261030-104149). With
// cell.bg_cull background cells farther than kBgFar from our own cell are destroyed (the game's own
// DestroyCell, as RemoveCellsOfOldScale does for cells outside the view), every 2 s, at most 300 a pass.
static bool g_fix_bg_cull = true;
COOP_FIX("cell.bg_cull", "BUG-0314", g_fix_bg_cull, "cell: background decoration cells left far behind (over 150 m from the own cell) are removed, so jumps do not fill the pool");
constexpr float kBgFar = 150.f;
static uint32_t g_bg_culled;
static uint32_t g_leave_writes, g_leave_dropped; // BUG-0308: our pool work while the stage was left (off) / skipped (on)
static double g_bg_cull_at;
static void bg_cull(double now)
{
    if (!g_fix_bg_cull || now - g_bg_cull_at < 2.0 || scale_moving())
        return;
    g_bg_cull_at = now;
    uint8_t* g = G();
    uint8_t* av = avatar();
    if (!g || !av)
        return;
    net::Vec3 me = pos_of(av);
    if (!views::finite_pos(me))
        return;
    uint8_t* data = F<uint8_t*>(g, 0x1C);
    uint32_t used = F<uint32_t>(g, 0x1C + 0x10), size = F<uint32_t>(g, 0x1C + 0x14);
    void* bq = F<void*>(g, 0x4100);
    if (!data || size != 0x398 || !bq)
        return;
    std::vector<uint32_t> gone;
    for (uint32_t slot = 0; slot < used && slot < 4096 && gone.size() < 300; slot++) {
        uint8_t* c = data + (size_t)slot * size;
        uint32_t idx = F<uint32_t>(c, 0);
        if ((idx & 0xFFFF) != slot || F<void*>(c, kQuery) != bq || F<uint8_t>(c, kKeepAlive))
            continue;
        net::Vec3 q = pos_of(c);
        if (views::finite_pos(q) && std::hypot(q.x - me.x, q.y - me.y) > kBgFar)
            gone.push_back(idx);
    }
    for (uint32_t idx : gone)
        destroy_local(idx);
    if (!gone.empty() && (g_bg_culled == 0 || (g_bg_culled + gone.size()) / 1000 != g_bg_culled / 1000))
        trace::ev("cell", "bg cull", "%u background cells over %.0f m from our cell removed (%u in all, BUG-0314)",
                  (unsigned)gone.size(), kBgFar, g_bg_culled + (unsigned)gone.size());
    g_bg_culled += (uint32_t)gone.size();
}

static bool g_dna_note_due;
// the bounding sphere Collisions 0xE7F260 tests before HandleCollisionPair (0xE6D790): the model
// data's centre and radius (GetCellModelData +0x1238 / +0x1244) times the scale +0x58, turned by
// +0x5C when +0x48 bit 2 is set; a cell without a model: +0x4C and +0x58
static float cell_sphere(uint8_t* c, float* ctr)
{
    float sc = F<float>(c, 0x58);
    auto* k = (uint32_t*)(c + kModelKey);
    if (!k[0]) {
        ctr[0] = F<float>(c, 0x4C), ctr[1] = F<float>(c, 0x50), ctr[2] = F<float>(c, 0x54);
        return sc;
    }
    uint8_t* md = a_ModelData ? skg::cdecl_call<uint8_t*>(a_ModelData, k[0], k[1], k[2]) : nullptr;
    if (!md)
        return -1.f;
    float v[3] = {F<float>(md, 0x1238), F<float>(md, 0x123C), F<float>(md, 0x1240)};
    if (F<uint8_t>(c, 0x48) & 2) {
        float* m = (float*)(c + 0x5C);
        float x = v[0], y = v[1], z = v[2];
        v[0] = m[3] * y + m[6] * z + x * m[0];
        v[1] = m[1] * x + m[4] * y + m[7] * z;
        v[2] = m[2] * x + m[5] * y + m[8] * z;
    }
    for (int i = 0; i < 3; i++)
        ctr[i] = F<float>(c, 0x4C + 4 * i) + v[i] * sc;
    return F<float>(md, 0x1244) * sc;
}

void tick(double now)
{
    static bool was_in_cell;
    bool cell = in_cell();
    if (!cell && (was_in_cell || !g_mirrors.empty()) && g_fix_forget) {
        if (was_in_cell)
            reset_knockout(); // no avatar any more: only our state is reset
        forget_mirrors();
        partner_tiles_reset(false); // the tile pool went with the stage
        g_loot_recent.clear();
        modes::cell_forget(); // the partners' puppets were in the same pool
    }
    if (!cell && (g_solo || g_solo_owed))
        solo_reset(); // BUG-0205: the stage was left
    if (cell && !was_in_cell && C.sess && C.sess->connected())
        g_dna_note_due = true; // BUG-0351: once the Cell game has its data (a load, an editor's return)
    if (cell && g_dna_note_due && S() && avatar()) {
        g_dna_note_due = false;
        dna_note("on entering Cell");
    }
    was_in_cell = cell;
    if (cell && avatar() && game::leaving_stage()) { // BUG-0308: no pool writes while the stage is torn down
        if (modes::cell_leaving())
            return;
        g_leave_writes++;
    }
    solo_tick();
    if (!in_cell() || !C.sess || !C.sess->connected() || !avatar())
        return;
    bg_cull(now);
    spare_tick(); // BUG-0355
    if (is_host())
        host_tick(now);
    else if (is_guest() && !g_solo)
        guest_tick(now);
    symbiosis_tick(now);
}

// the host's reads of a guest's MSG_CELL_EAT / MSG_CELL_FOOD, shared by the in-Cell handlers and the
// acknowledgement of a meal that cannot be counted (BUG-0239), so the two cannot drift apart
struct EatMsg {
    uint32_t idx = 0;
    uint8_t kind = ACT_GONE, arg = 0;
    int food = 0;
    bool plant = false;
    bool has_meal = false;
    uint32_t meal = 0;
};
static bool read_eat(net::Reader& r, EatMsg& m)
{
    m.idx = r.u32();
    if (!r.ok())
        return false;
    m.kind = r.u8(), m.arg = r.u8();
    m.food = (int)r.u32();
    m.plant = r.u8() != 0;
    if (!r.ok()) // an older guest: the index only
        m.kind = ACT_GONE, m.arg = 0, m.food = 0, m.plant = false;
    m.meal = r.u32();
    m.has_meal = r.ok();
    return true;
}
struct FoodMsg {
    int amount = 0;
    bool plant = false, valid = false, has_meal = false;
    uint32_t meal = 0;
};
static FoodMsg read_food(net::Reader& r)
{
    FoodMsg m;
    m.amount = (int)r.u32();
    m.plant = r.u8() != 0;
    m.valid = r.ok();
    m.meal = r.u32();
    m.has_meal = r.ok();
    return m;
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    // a knockout state is kept (and relayed) in any stage: an "up again" dropped while we were in an
    // editor or loading left the partner knocked out here (BUG-0061)
    bool cell = in_cell() && avatar() && !g_test_away;
    if (cell && game::leaving_stage()) { // BUG-0308: the stage is being torn down
        if (modes::cell_leaving()) {
            g_leave_dropped++;
            cell = false;
        } else if (type == net::MSG_CELL_STATE)
            g_leave_writes++;
    }
    // BUG-0239: a guest's meal that reaches us in an editor (or without an avatar, around a rebuild)
    // is acknowledged without its points, as a meal eaten first: the guest takes it back at our next
    // total instead of keeping it "on the way" until a later meal drops it all at once
    if (!cell && g_fix_food_ack_away && is_host() && (type == net::MSG_CELL_EAT || type == net::MSG_CELL_FOOD)) {
        EatMsg e;
        FoodMsg f;
        bool got = type == net::MSG_CELL_EAT ? read_eat(r, e) && e.has_meal : (f = read_food(r)).has_meal;
        if (got) // no game call here: outside Cell the pool can be gone, without an avatar AddFoodPoints is unsafe
            g_food_ack[from] = type == net::MSG_CELL_EAT ? e.meal : f.meal;
        trace::ev("eat", "refused away", "from %u: meal %u refused (this game is not in the Cell stage / has no cell)",
                  from, type == net::MSG_CELL_EAT ? e.meal : f.meal);
        return;
    }
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
    if (g_solo && is_guest() && (type == net::MSG_CELL_STATE || type == net::MSG_CELL_FOOD))
        return; // BUG-0205: our own pool until the host's away state ends (a state of the host's pool comes then)
    if (g_solo_owed && is_guest() && (type == net::MSG_CELL_STATE || type == net::MSG_CELL_FOOD))
        solo_send(); // before the host's first total is applied: it then counts them as on the way (no dip)
    if (type == net::MSG_CELL_STATE && is_guest()) {
        guest_state(r);
    } else if (type == net::MSG_CELL_HIT && is_host()) {
        uint32_t idx = r.u32();
        int dmg = r.u8(), t = r.u8();
        uint8_t* c = modes::cell_at(idx);
        // a bite lands on an NPC of our pool: never on a player's cell (ours or a puppet)
        if (!r.ok() || !c || F<uint8_t>(c, kDying) || c == avatar() || modes::cell_is_puppet(c)) {
            trace::ev("bite", "refused", "from %u c%u refused: %s", from, idx,
                      !c ? "no such cell here" : c == avatar() || modes::cell_is_puppet(c) ? "a player's cell" : "dying");
            return;
        }
        g_hits_in++;
        uint8_t* pc = puppet_cell(from);
        // BUG-0078: judged as DamageCell judges the host's own bite (0xE7A7C0): nothing happens when
        // the victim is not damageable (dead, hp 0), ShouldNotAttack(victim, attacker) is true (same
        // species, friend group, the species' "won't attack the player" flags: through the hook, so
        // the guest's puppet counts as the player) or the victim has +0x17C / +0x111 set. A mouth's
        // chunk (type 6) is EatChunk's, which asks none of these. Old (`fix guestbite 0`): the
        // arguments swapped, no gate.
        // BUG-0240: and IsDamageable's resource part (GOG 2024 0xE52940): the cell type is 2 (solid) and
        // the AI type is not 0 (meat; the victim is never the avatar here). Unreadable data: not spared.
        int ct = -1, ait = -1;
        bool not_damageable = g_fix_guestbite_type && res_types(c, &ct, &ait) && (ct != 2 || ait == 0);
        if (g_fix_guestbite && t != 6 &&
            (F<uint8_t>(c, kDead) == 1 || F<int>(c, kHp) == 0 || F<uint8_t>(c, 0x17C) || F<uint8_t>(c, kInvuln) ||
             not_damageable || (pc && o_NoAttack && h_NoAttack(c, pc)))) {
            g_hits_spared++;
            trace::ev("bite", "spared", "from %u c%u (%s) dmg=%d spared by the game's rules (dead/hp0/invulnerable/"
                      "not damageable/no-attack)", from, idx, kind_of(c), dmg);
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
        trace::ev("bite", "applied", "from %u c%u (%s) dmg=%d hp %d -> %d%s", from, idx, kind_of(c), dmg, F<int>(c, kHp),
                  std::max(hp, 0), hp > 0 ? "" : " killed");
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
        EatMsg e;
        if (!read_eat(r, e))
            return;
        uint32_t idx = e.idx;
        uint8_t kind = e.kind, arg = e.arg;
        int food = e.food;
        bool plant = e.plant;
        if (e.has_meal) // applied (or refused, eaten first) now: its total goes back with this number
            g_food_ack[from] = e.meal;
        if (food <= -1000 || food >= 1000)
            food = 0;
        uint8_t* c = modes::cell_at(idx);
        if (c && (modes::cell_is_puppet(c) || c == avatar()))
            c = nullptr;
        uint8_t* pc = puppet_cell(from);
        const char* what = c ? kind_of(c) : "-";
        if (kind == ACT_GONE)
            trace::ev("eat", c ? "from applied" : food ? "from late" : "from gone", "from %u gone c%u (%s) food=%d %s meal=%u: %s",
                      from, idx, what, food, plant ? "plant" : "meat", e.meal,
                      c ? "applied" : food ? "refused (eaten first / not here): points taken back" : "not here, no points");
        else
            trace::ev("eat", "from kill", "from %u kill c%u (%s): %s", from, idx, what,
                      c && !F<uint8_t>(c, kDying) && !F<uint8_t>(c, kDead) ? "applied" : "refused (gone or dying here)");
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
            FoodMsg f = read_food(r);
            if (f.valid && f.amount > -1000 && f.amount < 1000) {
                uint8_t* s0 = S();
                int before = s0 ? F<int32_t>(s0, 0x1C) : 0;
                o_AddFood(f.amount, f.plant);
                if (C.food_cause && s0 && f.amount != 0)
                    C.food_cause(from, before, f.amount);
            }
            trace::ev("eat", "from food", "from %u food +%d %s meal=%u: %s", from, f.amount, f.plant ? "plant" : "meat", f.meal,
                      f.valid && f.amount > -1000 && f.amount < 1000 ? "applied" : "refused (bad message)");
            if (f.has_meal)
                g_food_ack[from] = f.meal;
        } else if (is_guest()) {
            int food = (int)r.u32(), plant = (int)r.u32();
            uint8_t* s = S();
            if (!r.ok() || !s)
                return;
            uint32_t ack = r.u32(); // a host before 0.8 does not send it
            int diff = g_meals.diff(food, r.ok(), ack, g_pend_food, F<int32_t>(s, 0x1C), g_fix_foodseq);
            if (diff != 0) {
                trace::ev("eat", "total", "host total food=%d (ack meal %u) -> %+d here (pending %d)", food, ack, diff,
                          g_pend_food);
                int before = F<int32_t>(s, 0x1C);
                g_food_guard = true;
                o_AddFood(diff, false); // through the game: level ups happen as usual
                g_food_guard = false;
                if (C.food_cause)
                    C.food_cause(0xFF, before, diff);
            }
            F<int32_t>(s, 0x20) = plant;
        }
    }
}

void on_session_end()
{
    g_kodrop_until = 0;
    g_test_away = false;
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
    solo_reset();
}

// BUG-0205: the guest's own pool while the host is away (`coop.pool`, a line of its own, no key the lines
// above have: tests read the first `food=`)
static std::string away_status()
{
    if (!C.sess || C.sess->role() != net::Role::Client)
        return "";
    char e[200];
    snprintf(e, sizeof e, "cell_away: solo=%d send_due=%d solo_times=%u released=%u solo_spawns=%u kept_meat=%d kept_plant=%d "
             "kept_sent=%u kept_lost=%u\n",
             (int)g_solo, (int)g_solo_owed, g_solo_times, g_solo_released, g_solo_spawns, g_solo_food[0], g_solo_food[1],
             g_solo_sent, g_solo_dropped);
    return e;
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
    int type, ai;
    if (!res_types(c, &type, &ai))
        return "?";
    switch (type) {
    case 1: return "rock";
    case 3: return "plant";
    case 4: return "bubble";
    case 5: return "poison";
    case 6: return "part";
    case 7: return "nanite";
    case 2: return ai != 0 ? "creature" : "meat";
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

// The cell broadphase (SweepPrune, G+0x4104, docs/re/cell.md#cell-sap-active-list): replays the
// axis-0 sweep of SweepPrune::UpdatePairs over the sorted endpoints and reports the most objects
// open at once (the game's stack list holds 1024) and the objects still open at the end (a max
// endpoint before its min), with what those cells are.
static std::string sap_report()
{
    uint8_t* g = G();
    uint8_t* sap = g ? F<uint8_t*>(g, 0x4104) : nullptr;
    if (!sap)
        return "sap: none";
    int cap = F<int>(sap, 4), n = F<int>(sap, 0x20), pend = F<int>(sap, 0x2C);
    auto* ep = F<uint8_t*>(sap, 0x24);
    uint8_t* objs = F<uint8_t*>(sap, 0x14);
    if (!ep || !objs || n < 0 || n > 2 * cap || cap <= 0 || cap > 0x8000)
        return "sap: bad header";
    std::vector<int> active;
    std::vector<uint8_t> open(cap, 0);
    // entries per id: an object has one min and one max per axis; more min entries with one id
    // are endpoints left behind (a RemoveObject that marked other entries)
    std::vector<uint16_t> mins(cap, 0), maxs(cap, 0);
    for (int i = 0; i < n; i++) {
        int16_t type = F<int16_t>(ep, 8 * i), id = F<int16_t>(ep, 8 * i + 2);
        if (id >= 0 && id < cap && type != -2)
            (type == 0 ? mins : maxs)[id]++;
    }
    int dup_min = 0, lone_min = 0, lone_max = 0;
    for (int i = 0; i < cap; i++) {
        dup_min += mins[i] > 1;
        lone_min += mins[i] > maxs[i];
        lone_max += maxs[i] > mins[i];
    }
    int live_objs = F<int>(sap, 0x10);
    int peak = 0, nan_v = 0, removed = 0, unsorted = 0, max_first = 0;
    float prev = -INFINITY;
    for (int i = 0; i < n; i++) {
        int16_t type = F<int16_t>(ep, 8 * i), id = F<int16_t>(ep, 8 * i + 2);
        float v = F<float>(ep, 8 * i + 4);
        if (type == -2) {
            removed++;
            continue;
        }
        if (v != v)
            nan_v++;
        else {
            if (v < prev)
                unsorted++;
            prev = v;
        }
        if (id < 0 || id >= cap)
            continue;
        if (type == 0) {
            active.push_back(id);
            open[id]++;
            peak = std::max(peak, (int)active.size());
        } else {
            auto it = std::find(active.begin(), active.end(), (int)id);
            if (it == active.end())
                max_first++;
            else {
                *it = active.back();
                active.pop_back();
                open[id]--;
            }
        }
    }
    // what the objects left open are: cell +0x364 holds the cell's broadphase object
    std::unordered_set<uint32_t> mirror_local;
    for (auto& [h, m] : g_mirrors)
        mirror_local.insert(m.local);
    int k_mirror = 0, k_puppet = 0, k_avatar = 0, k_bg = 0, k_other = 0, k_none = 0, inverted = 0;
    float rmin = INFINITY, rmax = -INFINITY;
    std::unordered_map<int, uint8_t*> by_obj;
    uint8_t* data = F<uint8_t*>(g, 0x1C);
    uint32_t used = F<uint32_t>(g, 0x1C + 0x10), size = F<uint32_t>(g, 0x1C + 0x14);
    if (data && size == 0x398)
        for (uint32_t slot = 0; slot < used && slot < 4096; slot++) {
            uint8_t* c = data + (size_t)slot * size;
            if ((F<uint32_t>(c, 0) & 0xFFFF) == slot && F<int>(c, 0x364) >= 0 && F<int>(c, 0x364) < cap)
                by_obj[F<int>(c, 0x364)] = c;
        }
    char sample[200] = "";
    for (int id : active) {
        auto it = by_obj.find(id);
        uint8_t* c = it == by_obj.end() ? nullptr : it->second;
        if (!c) {
            k_none++;
            continue;
        }
        // its endpoint values (object record: +8 min index per axis, +0xC max index per axis)
        uint8_t* o = objs + 16 * (size_t)id;
        int mi = F<int16_t>(o, 8) & 0x7FFF, ma = F<int16_t>(o, 0xC) & 0x7FFF;
        if (mi < n && ma < n && F<float>(ep, 8 * mi + 4) > F<float>(ep, 8 * ma + 4))
            inverted++;
        float r = F<float>(c, kSize);
        rmin = std::min(rmin, r), rmax = std::max(rmax, r);
        uint32_t idx = F<uint32_t>(c, 0);
        if (idx == avatar_idx())
            k_avatar++;
        else if (modes::cell_is_puppet(c))
            k_puppet++;
        else if (mirror_local.count(idx))
            k_mirror++;
        else if (F<void*>(c, kQuery) != fg_query())
            k_bg++;
        else
            k_other++;
        if (!sample[0])
            snprintf(sample, sizeof sample, " first=c%u pos=(%.1f %.1f) size=%.3f min=%.2f max=%.2f", idx, pos_of(c).x,
                     pos_of(c).y, r, mi < n ? F<float>(ep, 8 * mi + 4) : 0.f, ma < n ? F<float>(ep, 8 * ma + 4) : 0.f);
    }
    int m1 = 0, hb = 0;
    particle_state(&m1, &hb);
    char b[900];
    snprintf(b, sizeof b,
             "sap: particles=%d peak=%u spare=%u bad_frees=%u minus1_cells=%d free_head_bad=%d endpoints=%d objects_cap=%d pending=%d held=%u removed=%d nan=%d unsorted=%d peak_open=%d open_at_end=%d "
             "max_first=%d live_objects=%d dup_min=%d lone_min=%d lone_max=%d sweeps=%u over_1024=%u sweep_peak=%u sweep_left=%u pairs=%u/%u pairs_full=%u fix=%d | open: mirror=%d other=%d bg=%d puppet=%d avatar=%d no_cell=%d inverted=%d size=%.3f..%.3f%s",
             particles_used(), g_particles_peak, g_part_spare, g_part_bad_frees, m1, hb, n, cap, pend, modes::cell_sap_held(), removed, nan_v, unsorted, peak, (int)active.size(), max_first, live_objs, dup_min, lone_min, lone_max, g_sap_sweeps, g_sap_over, g_sap_peak, g_sap_left,
             g_sap_pairs, g_sap_pair_cap, g_sap_pairs_full, (int)g_fix_sap_sweep, k_mirror, k_other, k_bg, k_puppet,
             k_avatar, k_none, inverted, rmin, rmax, sample);
    return b;
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
    if (!a.empty() && a[0] == "deaths") { // tests (BUG-0221): the game's death count S+0x70 (docs/re/cell.md)
        uint8_t* s0 = S();
        return "deaths=" + std::to_string(s0 ? F<int32_t>(s0, 0x70) : -1);
    }
    if (!a.empty() && a[0] == "npcs") { // tests (BUG-0205): the creatures within <r> m: local index, mirror, AI lock, pos
        uint8_t* av = avatar();
        if (!av)
            return "no cell";
        float r = a.size() > 1 ? (float)atof(a[1].c_str()) : 80.f;
        net::Vec3 me = pos_of(av);
        std::string out;
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            if (c == av || modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying) ||
                strcmp(kind_of(c), "creature") || net::dist(pos_of(c), me) > r)
                return;
            char b[128];
            snprintf(b, sizeof b, "npc %08X mirror=%d ailock=%d pos=(%.2f %.2f)\n", idx, g_host_of.count(idx) ? 1 : 0,
                     F<uint8_t>(c, kAiLock), pos_of(c).x, pos_of(c).y);
            out += b;
        });
        return out.empty() ? "no npcs\n" : out;
    }
    if (!a.empty() && a[0] == "census") { // tests (BUG-0307 hunt): every used slot of the cell pool by kind
        uint8_t* g = G();
        if (!g)
            return "no cell game\n";
        uint8_t* data = F<uint8_t*>(g, 0x1C);
        uint32_t used = F<uint32_t>(g, 0x1C + 0x10), size = F<uint32_t>(g, 0x1C + 0x14);
        if (!data || size != 0x398)
            return "no pool\n";
        void* fq = fg_query();
        uint32_t av = avatar_idx(), live = 0, fg = 0, other_q = 0, mirrors = 0, puppets = 0, strays = 0, keep = 0,
                 dying = 0, dead = 0, stray_keep = 0, stray_dying = 0, own = 0;
        std::map<int, uint32_t> stray_levels, bg_levels;
        uint32_t bg = 0, bg_far = 0, bg_keep = 0, bg_host = 0;
        uint8_t* avc = avatar();
        net::Vec3 me = avc ? pos_of(avc) : net::Vec3{};
        for (uint32_t slot = 0; slot < used && slot < 4096; slot++) {
            uint8_t* c = data + (size_t)slot * size;
            uint32_t idx = F<uint32_t>(c, 0);
            if ((idx & 0xFFFF) != slot)
                continue;
            live++;
            if (idx == av) {
                own++;
                continue;
            }
            if (F<void*>(c, kQuery) != fq) {
                other_q++;
                bool is_bg = F<void*>(c, kQuery) == F<void*>(g, 0x4100);
                bg += is_bg;
                if (is_bg) {
                    bg_levels[F<int>(c, kLevel)]++;
                    net::Vec3 q = pos_of(c);
                    float d = std::hypot(q.x - me.x, q.y - me.y);
                    bg_far += d > 150.f;
                    bg_keep += F<uint8_t>(c, kKeepAlive) != 0;
                    bg_host += g_host_of.count(idx) ? 1 : 0;
                }
                continue;
            }
            fg++;
            bool k = F<uint8_t>(c, kKeepAlive), dy = F<uint8_t>(c, kDying), de = F<uint8_t>(c, kDead);
            keep += k; dying += dy; dead += de;
            if (g_host_of.count(idx))
                mirrors++;
            else if (modes::cell_is_puppet(c))
                puppets++;
            else {
                strays++;
                stray_keep += k;
                stray_dying += dy || de;
                stray_levels[F<int>(c, kLevel)]++;
            }
        }
        char b[900];
        int n = snprintf(b, sizeof b, "census: header=%u used=%u live=%u own=%u fg=%u other_query=%u mirrors=%u (map %u) puppets=%u "
                         "strays=%u (keepalive %u, dying/dead %u) keepalive=%u dying=%u dead=%u stray_levels=",
                         cells_live(), used, live, own, fg, other_q, mirrors, (unsigned)g_mirrors.size(), puppets, strays,
                         stray_keep, stray_dying, keep, dying, dead);
        for (auto& [lv, k] : stray_levels)
            n += snprintf(b + n, sizeof b - std::min<size_t>(n, sizeof b), "%d:%u ", lv, k);
        n += snprintf(b + n, sizeof b - std::min<size_t>(n, sizeof b), " background=%u (over 150 m %u, keepalive %u, mirrors %u) bg_culled=%u leave_writes=%u leave_dropped=%u bg_levels=",
                      bg, bg_far, bg_keep, bg_host, g_bg_culled, g_leave_writes, g_leave_dropped);
        for (auto& [lv, k] : bg_levels)
            n += snprintf(b + n, sizeof b - std::min<size_t>(n, sizeof b), "%d:%u ", lv, k);
        n = std::min<int>(n, (int)sizeof b - 1);
        return std::string(b) + "\n";
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
                 "ailock=%d keepalive=%d ring=%.2f anim=%X idx=%08X onscreen=%d\n", a[1].c_str(), kind_of(c), pos_of(c).x, pos_of(c).y, v[0], v[1],
                 F<float>(c, kSize), F<uint32_t>(c, 0x248), F<int>(c, 0x250), F<int>(c, kHp), F<int>(c, kLevel),
                 F<uint8_t>(c, kAiLock), F<uint8_t>(c, kKeepAlive), rollover_of(F<uint32_t>(c, 0)), F<uint32_t>(c, 0x1B0), F<uint32_t>(c, 0), on_screen_dbg(c));
        return b;
    }
    if (!a.empty() && a[0] == "diet") { // what each cell's mouth eats (the model data CanEat 0xE68470 reads)
        std::string out;
        auto line = [&](const char* who, uint8_t* c) {
            char b[160];
            auto* k = (uint32_t*)(c + kModelKey);
            uint8_t* md = a_ModelData && k[0] ? skg::cdecl_call<uint8_t*>(a_ModelData, k[0], k[1], k[2]) : nullptr;
            snprintf(b, sizeof b, "diet %s model=%08X carnivore=%d herbivore=%d\n", who, k[0], md ? md[0x1250] : -1,
                     md ? md[0x1251] : -1);
            out += b;
        };
        if (uint8_t* av = avatar())
            line("me", av);
        for_each_cell([&](uint8_t* c, uint32_t) {
            if (modes::cell_is_puppet(c))
                line("partner", c);
        });
        return out.empty() ? std::string("diet: no avatar\n") : out;
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
        // "kill <key|-> anyhit [kind]": DamageCell on a cell of any kind (or the nearest of that kind):
        // the host's own bite there, a MSG_CELL_HIT from a guest (BUG-0240)
        bool anyhit = a.size() > 2 && a[2] == "anyhit";
        std::string kind = anyhit && a.size() > 3 ? a[3] : "creature";
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            if (modes::cell_is_puppet(c) || c == av || (!(anyhit && want) && kind != kind_of(c)))
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
            if (a_Bite && !(a.size() > 2 && (a[2] == "hit" || anyhit))) // the game's bite (event 10): DamageCell, or BreakOrSwallow when lethal
                skg::cdecl_call<bool>(a_Bite, avatar_idx(), F<uint32_t>(c, 0), at, dir, 0, 1.f / 30.f);
            else
                h_DamageCell(c, av, dir, 4, 0, 0.f);
        }
        char b[240];
        snprintf(b, sizeof b, "bit c%u %s at %.1f level=%d hp=%d pos=(%.1f %.1f) ring=%.2f rings=%u skip=%s lock=%d invuln=%d noattack=%d%s", key, kind_of(c), v[0].first, F<int>(c, kLevel), F<int>(c, kHp),
                 pos_of(c).x, pos_of(c).y, rollover_of(F<uint32_t>(c, 0)), g_bite_rings, g_bite_ring_skip, F<uint8_t>(c, 0x17C), F<uint8_t>(c, kInvuln),
                 o_NoAttack_ptr() ? (int)o_NoAttack_ptr()(c, av) : -1, F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying) ? " dying" : "");
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
        if (m && a.size() >= 2 && a[1] == "tp") // tests (BUG-0237): our cell right onto the mate (it touches it)
            stop_at(av, pos_of(m));
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
    if (a.size() >= 2 && a[0] == "away") { // host, tests: Cell messages handled as in an editor (BUG-0239)
        g_test_away = a[1] == "1";
        return g_test_away ? "away 1" : "away 0";
    }
    if (a.size() >= 2 && a[0] == "kodrop") { // tests: ignore MSG_CELL_KO for <s> seconds (a late refresh, BUG-0238)
        double s = std::clamp(atof(a[1].c_str()), 0.0, 30.0);
        g_kodrop_until = s > 0 ? H->time_seconds() + s : 0;
        return "kodrop " + std::to_string(s).substr(0, 4);
    }
    if (a.size() >= 2 && a[0] == "invulnfor") { // tests: invulnerable for <s> seconds, as after a respawn (BUG-0237)
        F<uint8_t>(av, kInvuln) = 1;
        g_invuln_until = H->time_seconds() + std::clamp(atof(a[1].c_str()), 0.0, 120.0);
        return "invulnerable until " + std::to_string(g_invuln_until);
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
    if (a.size() >= 3 && a[0] == "tp") { // move our cell to <dx dy> from the nearest partner [or player <id>]
        net::Vec3 at;
        if (a.size() >= 4) { // tests with 3-4 players (BUG-0152): next to that player's cell as we draw it
            uint8_t* pc = puppet_cell(atoi(a[3].c_str()));
            if (!pc)
                return "no such player";
            at = pos_of(pc);
        } else if (nearest_partner(1e9f, &at).empty())
            return "no partner";
        stop_at(av, {at.x + (float)atof(a[1].c_str()), at.y + (float)atof(a[2].c_str()), 0.f});
        return "moved";
    }
    if (!a.empty() && a[0] == "staletarget") { // tests (BUG-0306): AI cells follow a group leader that holds a dead chase target
        // The fixture's groups (+0x38C, pool G+0xA8, leader at group+8) are clusters without AI: their
        // leader never runs the behaviours, so it never drops a target (a leader that skips its check,
        // as in the lab run). <n> AI cells join such a group; at their next pick (PickTargets 0xE70CF0)
        // they copy the leader's dead chase target.
        uint8_t* g = G();
        if (!g)
            return "no cell game\n";
        uint8_t* gdata = F<uint8_t*>(g, 0xA8);
        uint32_t ghigh = F<uint32_t>(g, 0xA8 + 0x10), gsize = F<uint32_t>(g, 0xA8 + 0x14);
        int max = a.size() >= 2 ? atoi(a[1].c_str()) : 8, followers = 0;
        if (!gdata || !gsize || gsize > 0x1000)
            return "no group pool\n";
        uint32_t gi = 0;
        uint8_t* lc = nullptr;
        for_each_cell([&](uint8_t* c, uint32_t idx) {
            uint32_t g1 = F<uint32_t>(c, 0x38C), gs = g1 & 0xFFFF;
            if (lc || !g1 || gs > ghigh || modes::cell_is_puppet(c))
                return;
            uint8_t* grp = gdata + (size_t)gs * gsize;
            uint32_t li = F<uint32_t>(grp, 8);
            uint8_t* l = F<uint32_t>(grp, 0) == g1 ? modes::cell_at(li) : nullptr;
            if (l && li != idx && ai_type(l) <= 0) {
                gi = g1;
                lc = l;
            }
        });
        if (!lc)
            return "staletarget: 0 followers (no group with a leader without AI)\n";
        uint32_t dead = F<uint32_t>(lc, 0) ^ 0x10000; // the leader's slot with another generation: a dead cell
        F<uint32_t>(lc, kChase) = dead;
        F<float>(lc, 0x230) = 60.f; // chase timer
        std::map<std::string, int> types;
        for_each_cell([&](uint8_t* c, uint32_t) {
            int at = ai_type(c);
            if (followers >= max || at <= 0 || c == lc || modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying))
                return;
            F<uint32_t>(c, 0x38C) = gi;
            F<float>(c, 0x224) = F<float>(c, 0x228) = F<float>(c, 0x21C) = 0.f; // free to chase at its next pick
            F<float>(c, 0x238) = 0.f;
            char k[16];
            snprintf(k, sizeof k, "%X", at);
            types[k]++;
            followers++;
        });
        std::string ts;
        for (auto& [t, n] : types)
            ts += " " + t + ":" + std::to_string(n);
        char b[160];
        snprintf(b, sizeof b, "staletarget: %d followers of leader c%u chasing dead c%u, stale_targets=%u, follower ai types", followers,
                 F<uint32_t>(lc, 0), dead, g_stale_targets);
        return b + ts + "\n";
    }
    if (!a.empty() && a[0] == "parts") { // RE (BUG-0355): who holds the soft-body particles
        // cells by tier and distance from our cell, mirrors apart, holders of the spare slot, how far
        // the spare and the cells' particles are from their cells
        net::Vec3 me = pos_of(av);
        uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
        int tiers[4] = {}, held = 0, mir_held = 0, spare_cells = 0, spare_refs = 0, own_spare = 0, near_held = 0, far_held = 0;
        float far_part = 0;
        int nan_cells = 0, nan_parts = 0;
        if (parts)
            for (int k = 0; k < kParticles; k++)
                nan_parts += !std::isfinite(((float*)(parts + 4 + 12 * k))[0]) || !std::isfinite(((float*)(parts + 4 + 12 * k))[1]);
        int t0_big = 0, t0_stamp = 0, t0_nogfx = 0;
        uint32_t stamp = G() ? F<uint32_t>(G(), 0x4100) : 0;
        int near_bare = 0; // cells within 10 m with a graphics object and a tier but no particles (the "no particles" form)
        auto scan = [&](uint8_t* c, bool mirror) {
            int n = F<int>(c, 0x250), t = F<int>(c, 0x24C);
            if (c != av && F<uint32_t>(c, 0x248) && n == 1 && t >= 1) {
                net::Vec3 q = pos_of(c);
                near_bare += std::hypot(q.x - me.x, q.y - me.y) < 10.f;
            }
            if (t == 0) {
                t0_big += F<float>(c, kSize) >= 0.3f;
                t0_stamp += F<uint32_t>(c, 0x35C) == stamp;
                t0_nogfx += !F<uint32_t>(c, 0x248);
            }
            nan_cells += !std::isfinite(F<float>(c, 0x4C)) || !std::isfinite(F<float>(c, 0x50));
            if (t >= 0 && t < 4)
                tiers[t]++;
            if (!F<uint32_t>(c, 0x248) || n < 2 || n > 64)
                return;
            net::Vec3 p = pos_of(c);
            float d = std::hypot(p.x - me.x, p.y - me.y);
            held += n;
            if (mirror)
                mir_held += n;
            (d < 60.f ? near_held : far_held) += n;
            int s = 0;
            for (int i = 0; i < n; i++) {
                int k = F<int>(c, 0x254 + 4 * i);
                s += k == kSpare;
                if (parts && k >= 0 && k < kParticles) {
                    auto* q = (float*)(parts + 4 + 12 * k);
                    far_part = std::max(far_part, std::hypot(q[0] - p.x, q[1] - p.y));
                }
            }
            spare_refs += s;
            spare_cells += s > 0;
            if (c == av)
                own_spare = s;
        };
        for_each_cell([&](uint8_t* c, uint32_t idx) { scan(c, g_host_of.count(idx) > 0); });
        scan(av, false);
        float* sp = parts ? (float*)(parts + 4 + 12 * kSpare) : nullptr;
        static uintptr_t a_box = H->sym("Simulator::Cell::ActiveBox");
        auto* box = (const float*)a_box;
        char bx[200] = "";
        if (box)
            snprintf(bx, sizeof bx, " active_box=%.0fx%.0f m", box[3] - box[0], box[4] - box[1]);
        if (uint32_t* gp = gfx_pool())
            snprintf(bx + strlen(bx), sizeof bx - strlen(bx), " gfx=%u/%u peak=%u soft_made=%u soft_refused=%u count0=%u", gp[3], gp[6], g_gfx_peak,
                     g_soft_made, g_soft_refused, g_soft_count0);
        char b[800];
        snprintf(b, sizeof b,
                 "parts: used=%d held=%d (mirrors %d, within 60 m %d, beyond %d) tiers=%d/%d/%d/%d spare_cells=%d spare_refs=%d own_spare=%d "
                 "own_n=%d spare_at=(%.0f %.0f) farthest_particle=%.0f m nan_cells=%d nan_particles=%d room_runs=%u stripped=%u spare_freed=%u spare=%u tier0: size>=0.3 %d, +0x35C==G+0x4100 %d, no +0x248 %d near_bare=%d bare_moves=%u%s\n",
                 particles_used(), held, mir_held, near_held, far_held, tiers[0], tiers[1], tiers[2], tiers[3], spare_cells, spare_refs,
                 own_spare, F<int>(av, 0x250), sp ? sp[0] : 0.f, sp ? sp[1] : 0.f, far_part, nan_cells, nan_parts, g_part_room_runs, g_part_stripped,
                 g_part_spare_freed, g_part_spare, t0_big, t0_stamp, t0_nogfx, near_bare, g_bare_moves, bx);
        return b;
    }
    if (!a.empty() && a[0] == "orphans") { // RE (BUG-0355): used particles no cell's list holds
        uint8_t* g = G();
        uint8_t* parts = g ? F<uint8_t*>(g, 0x4108) : nullptr;
        uint8_t* data = g ? F<uint8_t*>(g, 0x1C) : nullptr;
        if (!parts || !data)
            return "orphans: no pool\n";
        std::vector<int> holders(kParticles, 0);
        uint32_t nslots = F<uint32_t>(g, 0x1C + 0x10);
        int big_lists = 0, nogfx_lists = 0;
        for (uint32_t slot = 0; slot < nslots && slot < 4096; slot++) {
            uint8_t* c = data + (size_t)slot * 0x398;
            if ((F<uint32_t>(c, 0) & 0xFFFF) != slot)
                continue;
            int k = F<int>(c, 0x250);
            if (k > 64) {
                big_lists++;
                continue;
            }
            if (k > 1 && !F<uint32_t>(c, 0x248))
                nogfx_lists++;
            if (k > 1)
                for (int i = 0; i < k; i++) {
                    int j = F<int>(c, 0x254 + 4 * i);
                    if (j >= 0 && j < kParticles)
                        holders[j]++;
                }
        }
        int high = F<int>(parts, 0x1BD58), used = F<int>(parts, 0x1BD54), owned = 0, orphans = 0, dead_owner = 0, own_av = 0, multi = 0;
        // the free list: slots on it are free
        std::vector<char> on_free(kParticles + 1, 0);
        int head = F<int>(parts, 0x1BD5C), free_n = 0;
        for (int guard = 0; head >= 0 && head < kParticles && !on_free[head] && guard < kParticles; guard++) {
            on_free[head] = 1;
            free_n++;
            head = F<int>(parts, 0x1A5E4 + 4 * head);
        }
        uint32_t avi = avatar_idx();
        std::map<uintptr_t, int> sites;
        for (int i = 0; i <= high && i < kParticles; i++) {
            if (on_free[i])
                continue;
            uint32_t ow = F<uint32_t>(parts, 0x18E74 + 4 * i);
            owned += ow != 0;
            multi += holders[i] > 1;
            if (holders[i] == 0 && ow) {
                orphans++;
                own_av += ow == avi;
                dead_owner += !modes::cell_at(ow);
                sites[g_part_site[i] ? g_part_site[i] - H->image_base() + 0x400000 : 0]++;
            }
        }
        std::string st;
        for (auto& [a_, n_] : sites)
            st += " " + std::to_string(n_) + "@" + [&] { char h[16]; snprintf(h, sizeof h, "%08X", (unsigned)a_); return std::string(h); }();
        char b[300];
        snprintf(b, sizeof b, "orphans: used=%d high=%d on_free_list=%d owned=%d orphans=%d (owner gone %d, owner our cell %d) held_twice=%d lists_over_64=%d lists_without_gfx=%d\n",
                 used, high, free_n, owned, orphans, dead_owner, own_av, multi, big_lists, nogfx_lists);
        std::string first = b;
        snprintf(b, sizeof b, "  destroys of cells with particles: %u, particles %u, given back %u, without +0x248 %u, still alive after %u\n",
                 g_dbg_destroys, g_dbg_expect, g_dbg_freed, g_dbg_nogfx, g_dbg_alive);
        std::string by;
        for (auto& [ra, n_] : g_dbg_destroy_by) {
            char h[96], mod[80];
            coop_module_of(ra, mod, sizeof mod);
            snprintf(h, sizeof h, " %u@%s", n_, mod);
            by += h;
        }
        return first + std::string(b) + "  orphans allocated from:" + st + "\n  DestroyCell from:" + by + "\n";
    }
    if (a.size() >= 2 && a[0] == "particles") { // tests (BUG-0338): fill the soft-body particle pool up to <n> used (owner: our cell)
        uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
        int want = std::min(atoi(a[1].c_str()), kParticles - 1), made = 0;
        net::Vec3 me = pos_of(av);
        float p[3] = {me.x, me.y, me.z}, v[3] = {0, 0, 0};
        while (parts && o_PartAlloc && F<int>(parts, 0x1BD54) < want && made < kParticles) {
            if (o_PartAlloc(parts, p, v, 1.f, (int)avatar_idx()) < 0)
                break;
            made++;
        }
        char b[96];
        snprintf(b, sizeof b, "particles: %d made, %d of %d used\n", made, particles_used(), kParticles);
        return b;
    }
    if (!a.empty() && a[0] == "collide") { // tests (BUG-0342): Collisions 0xE7F260's tests for our cell's pairs
        uint8_t* av = avatar();
        uint8_t* g = G();
        uint8_t* sap = g ? F<uint8_t*>(g, 0x4104) : nullptr;
        if (!av || !sap)
            return "collide: no avatar / sap\n";
        // the bounding sphere 0xE6D790 builds for the narrow test
        auto sphere = [&](uint8_t* c, float* ctr) -> float {
            float sc = F<float>(c, 0x58);
            auto* k = (uint32_t*)(c + kModelKey);
            if (!k[0]) {
                ctr[0] = F<float>(c, 0x4C), ctr[1] = F<float>(c, 0x50), ctr[2] = F<float>(c, 0x54);
                return sc;
            }
            uint8_t* md = a_ModelData ? skg::cdecl_call<uint8_t*>(a_ModelData, k[0], k[1], k[2]) : nullptr;
            if (!md)
                return -1.f;
            float v[3] = {F<float>(md, 0x1238), F<float>(md, 0x123C), F<float>(md, 0x1240)};
            if (F<uint8_t>(c, 0x48) & 2) {
                float* m = (float*)(c + 0x5C);
                float x = v[0], y = v[1], z = v[2];
                v[0] = m[3] * y + m[6] * z + x * m[0];
                v[1] = m[1] * x + m[4] * y + m[7] * z;
                v[2] = m[2] * x + m[5] * y + m[8] * z;
            }
            for (int i = 0; i < 3; i++)
                ctr[i] = F<float>(c, 0x4C + 4 * i) + v[i] * sc;
            return F<float>(md, 0x1244) * sc;
        };
        uint32_t me = F<uint32_t>(av, 0);
        int np = F<int>(sap, 8);
        int* pairs = F<int*>(sap, 0xC);
        std::string out;
        char b[300];
        int n = 0;
        for (int i = 0; pairs && i < np && i < 8192; i++) {
            uint32_t x = (uint32_t)pairs[2 * i], y = (uint32_t)pairs[2 * i + 1];
            if (x != me && y != me)
                continue;
            uint32_t other = x == me ? y : x;
            uint8_t* ca = modes::cell_at(me);
            uint8_t* cb = modes::cell_at(other);
            float pa[3] = {}, pb[3] = {};
            float ra = ca ? sphere(ca, pa) : -1.f, rb = cb ? sphere(cb, pb) : -1.f;
            float d2 = (pa[0] - pb[0]) * (pa[0] - pb[0]) + (pa[1] - pb[1]) * (pa[1] - pb[1]) + (pa[2] - pb[2]) * (pa[2] - pb[2]);
            snprintf(b, sizeof b, "pair me=%08X(%s puppet=%d) other=%08X(%s %s) ra=%.3f rb=%.3f d=%.3f narrow=%d me_obj=%p av=%p\n", me,
                     ca ? "found" : "GONE", ca ? (int)modes::cell_is_puppet(ca) : -1, other, cb ? "found" : "GONE", cb ? kind_of(cb) : "-", ra, rb, std::sqrt(d2),
                     ca && cb && d2 < (ra + rb) * (ra + rb), (void*)ca, (void*)av);
            out += b;
            n++;
        }
        snprintf(b, sizeof b, "collide: %d pairs with our cell %08X of %d, own_pairs=%u\n", n, me, np, g_own_pairs);
        return out + b;
    }
    if (!a.empty() && a[0] == "sapme") { // tests (BUG-0342): our own cell's broadphase object [or c<key>'s]
        uint8_t* av = avatar();
        if (a.size() > 1) {
            uint32_t key = (uint32_t)strtoul(a[1].c_str() + (a[1][0] == 'c' ? 1 : 0), nullptr, 10);
            auto it = g_mirrors.find(key);
            av = is_guest() ? (it != g_mirrors.end() && it->second.local ? modes::cell_at(it->second.local) : nullptr)
                            : modes::cell_at(key);
        }
        uint8_t* g = G();
        uint8_t* sap = g ? F<uint8_t*>(g, 0x4104) : nullptr;
        if (!av || !sap)
            return "sapme: no avatar / sap";
        int id = F<int>(av, 0x364), cap = F<int>(sap, 4);
        char b[400];
        if (id < 0 || id >= cap)
            return "sapme: avatar " + std::to_string(F<uint32_t>(av, 0)) + " sap id " + std::to_string(id);
        uint8_t* o = F<uint8_t*>(sap, 0x14) + 16 * id;
        uint16_t e[4] = {F<uint16_t>(o, 8), F<uint16_t>(o, 10), F<uint16_t>(o, 12), F<uint16_t>(o, 14)};
        auto val = [&](int axis, uint16_t ei) -> float {
            uint8_t* ep = F<uint8_t*>(sap, (ei & 0x8000 ? 0x30 : 0x24) + 4 * axis);
            return F<float>(ep, 8 * (ei & 0x7FFF) + 4);
        };
        auto* k = (uint32_t*)(av + kModelKey);
        uint8_t* md = a_ModelData && k[0] ? skg::cdecl_call<uint8_t*>(a_ModelData, k[0], k[1], k[2]) : nullptr;
        int np = F<int>(sap, 8), mine = 0;
        int* pairs = F<int*>(sap, 0xC);
        for (int i = 0; pairs && i < np && i < cap; i++)
            mine += pairs[2 * i] == F<int>(o, 0) || pairs[2 * i + 1] == F<int>(o, 0);
        auto who = [&](int axis, uint16_t ei) -> int {
            uint8_t* ep = F<uint8_t*>(sap, (ei & 0x8000 ? 0x30 : 0x24) + 4 * axis);
            return F<int16_t>(ep, 8 * (ei & 0x7FFF) + 2);
        };
        snprintf(b, sizeof b, "sapme: avatar %u idx=%08X sapid=%d obj=(%08X %08X) e=%04X/%04X %04X/%04X x=%.2f..%.2f y=%.2f..%.2f "
                 "owners=%d/%d/%d/%d pos=(%.2f %.2f) size=%.2f grp=%08X live=%d pairs_with_me=%d/%d md_parts=%d md_radius=%.3f md_centre=(%.2f %.2f) model=%08X",
                 F<uint32_t>(av, 0), F<uint32_t>(av, 0), id, F<uint32_t>(o, 0), F<uint32_t>(o, 4), e[0], e[1], e[2], e[3],
                 val(0, e[0]), val(0, e[2]), val(1, e[1]), val(1, e[3]), who(0, e[0]), who(0, e[2]), who(1, e[1]), who(1, e[3]),
                 pos_of(av).x, pos_of(av).y, F<float>(av, kSize), F<uint32_t>(av, 0x1B0), F<int>(sap, 0x10), mine, np, md ? F<int>(md, 0) : -1, md ? F<float>(md, 0x1244) : -1.f, md ? F<float>(md, 0x1238) : 0.f,
                 md ? F<float>(md, 0x123C) : 0.f, k[0]);
        std::string out = b;
        float* m = (float*)(av + 0x5C);
        char c2[300];
        snprintf(c2, sizeof c2, " flags48=%04X scale58=%.3f m=(%.2f %.2f %.2f / %.2f %.2f %.2f / %.2f %.2f %.2f) p4c=(%.2f %.2f %.2f)",
                 F<uint16_t>(av, 0x48), F<float>(av, 0x58), m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8],
                 F<float>(av, 0x4C), F<float>(av, 0x50), F<float>(av, 0x54));
        return out + c2;
    }
    if (!a.empty() && a[0] == "sap") // the cell broadphase's axis-0 sweep, as UpdatePairs would run it (BUG-0337)
        return sap_report() + "\n";
    if (a.size() >= 3 && a[0] == "crowd") { // tests (BUG-0355): <n> of our cells on a disc of <r> m around us (a crowd on screen)
        net::Vec3 me = pos_of(av);
        int want = atoi(a[1].c_str()), moved = 0;
        float r = (float)atof(a[2].c_str());
        for_each_cell([&](uint8_t* c, uint32_t) {
            if (moved >= want || modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying))
                return;
            // a sunflower spiral: even density, no two cells on one spot, none on our own (from 3 m)
            float t = (float)(moved + 1) / (float)std::max(want, 1), ang = 2.39996323f * (float)moved;
            float d = 3.f + (r - 3.f) * std::sqrt(t);
            set_pos(c, {me.x + d * std::cos(ang), me.y + d * std::sin(ang), me.z});
            moved++;
        });
        char b[96];
        snprintf(b, sizeof b, "crowd: %d cells moved\n", moved);
        H->log(M, SK_INFO, "cell: %s", b);
        return b;
    }
    if (a.size() >= 2 && a[0] == "column") { // tests (BUG-0337): <n> of our cells on one vertical line next to us (all open at once in the sweep)
        net::Vec3 me = pos_of(av);
        int want = atoi(a[1].c_str()), moved = 0;
        for_each_cell([&](uint8_t* c, uint32_t) {
            if (moved >= want || modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying))
                return;
            // 40 m tall, 8 m to our right: inside our active area (cells taken out of it are culled)
            set_pos(c, {me.x + 8.f, me.y - 20.f + 40.f * (float)moved / (float)std::max(want, 1), me.z});
            moved++;
        });
        char b[96];
        snprintf(b, sizeof b, "column: %d cells moved\n", moved);
        H->log(M, SK_INFO, "cell: %s", b);
        return b;
    }
    if (!a.empty() && a[0] == "nanself") { // tests (BUG-0299): our own cell and its particles NaN, as seen in a 4-player run
        uint8_t* av = avatar();
        if (!av)
            return "no avatar\n";
        float nan = std::nanf("");
        int n = F<int>(av, 0x250);
        uint8_t* parts = G() ? F<uint8_t*>(G(), 0x4108) : nullptr;
        if (F<uint32_t>(av, 0x248) && parts && n > 1 && n <= 64)
            for (int i = 0; i < n; i++) {
                uint32_t k = F<uint32_t>(av, 0x254 + 4 * i);
                if (k < 1500)
                    for (int j = 0; j < 3; j++)
                        ((float*)(parts + 4 + 12 * (size_t)k))[j] = nan;
            }
        for (int j = 0; j < 2; j++) {
            ((float*)(av + 0x4C))[j] = nan;
            ((float*)(av + 0x84))[j] = nan;
        }
        if (a.size() >= 2 && a[1] == "all") { // BUG-0307: also the fields the first heal left alone
            for (int j = 0; j < 9; j++)
                ((float*)(av + kRot))[j] = ((float*)(av + 0x24))[j] = nan;
            if (F<uint32_t>(av, 0x248) && parts && n > 1 && n <= 64)
                for (int i = 0; i < n; i++) {
                    uint32_t k = F<uint32_t>(av, 0x254 + 4 * i);
                    if (k < 1500)
                        for (int j = 0; j < 3; j++)
                            ((float*)(parts + 0x11944 + 12 * (size_t)k))[j] = nan;
                }
            return "own cell NaN (all fields)\n";
        }
        return "own cell NaN\n";
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
             "revive_dist=%.1f local_gameplay_cells=%d own_pairs=%u ko_eats_blocked=%u\n",
             F<int>(av, kHp), F<uint8_t>(av, kInvuln), g_own_eats, g_own_food, g_bites_taken, g_damage_taken, pos_of(av).x, pos_of(av).y, F<float>(av, kSize), F<float>(av, kTargetSize), g_body,
             g_downed ? 1 : 0, pd, revive_dist(av), local, g_own_pairs, g_ko_eats_blocked);
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
    return b + std::string(ob) + ko + status() + away_status(); // the away line last: tests read the first food=
}

// the support trace (trace.h): what the own cell has around it and what it did since the last line.
// Food = plants and meat (kind_of), "screen" = what world_to_screen puts inside the window.
uint32_t own_model()
{
    uint8_t* av = in_cell() ? avatar() : nullptr;
    return av ? F<uint32_t>(av, kModelKey) : 0;
}

std::string trace_line()
{
    uint8_t* av = in_cell() ? avatar() : nullptr;
    if (!av)
        return "";
    net::Vec3 me = pos_of(av);
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    int plants = 0, meat = 0, creatures = 0, all = 0;
    float near_plant = -1, near_meat = -1;
    for_each_cell([&](uint8_t* c, uint32_t) {
        if (modes::cell_is_puppet(c) || F<uint8_t>(c, kDead) || F<uint8_t>(c, kDying))
            return;
        all++;
        const char* k = kind_of(c);
        bool plant = !strcmp(k, "plant"), m = !strcmp(k, "meat"), cr = !strcmp(k, "creature");
        if (!plant && !m && !cr)
            return;
        net::Vec3 p = pos_of(c);
        float d = net::dist(p, me);
        if (plant && (near_plant < 0 || d < near_plant))
            near_plant = d;
        if (m && (near_meat < 0 || d < near_meat))
            near_meat = d;
        float w[3] = {p.x, p.y, p.z}, sc[2];
        if (!H->world_to_screen(w, sc) || sc[0] < 0 || sc[1] < 0 || sc[0] >= sw || sc[1] >= sh)
            return;
        (plant ? plants : m ? meat : creatures)++;
    });
    auto* k = (uint32_t*)(av + kModelKey);
    uint8_t* md = a_ModelData && k[0] ? skg::cdecl_call<uint8_t*>(a_ModelData, k[0], k[1], k[2]) : nullptr;
    uint8_t* s = S();
    // what happened since the last line (counters of status())
    static uint32_t l_eats, l_food, l_sent, l_in, l_late, l_hits_sent, l_hits_in, l_spared, l_kills_sent, l_kills_in;
    char b[600];
    snprintf(b, sizeof b,
             "size=%.2f hp=%d model=%08X diet=%s food=%d plant=%d screen: plants=%d meat=%d creatures=%d "
             "nearest plant=%.0f meat=%.0f pool=%d mirrors=%u downed=%d body=%.2f | since last: ate=%u (+%u pts) "
             "eats_sent=%u eats_in=%u late=%u bites_sent=%u bites_in=%u spared=%u kills_sent=%u kills_in=%u",
             F<float>(av, kSize), F<int>(av, kHp), k[0],
             !md ? "?" : md[0x1250] && md[0x1251] ? "omni" : md[0x1250] ? "carn" : md[0x1251] ? "herb" : "none", s ? F<int32_t>(s, 0x1C) : -1, s ? F<int32_t>(s, 0x20) : -1,
             plants, meat, creatures, near_plant, near_meat, all, (unsigned)g_mirrors.size(), (int)g_downed, g_body,
             g_own_eats - l_eats, g_own_food - l_food, g_eats_sent - l_sent, g_eats_in - l_in, g_eats_late - l_late,
             g_hits_sent - l_hits_sent, g_hits_in - l_hits_in, g_hits_spared - l_spared, g_kills_sent - l_kills_sent,
             g_kills_in - l_kills_in);
    l_eats = g_own_eats, l_food = g_own_food, l_sent = g_eats_sent, l_in = g_eats_in, l_late = g_eats_late;
    l_hits_sent = g_hits_sent, l_hits_in = g_hits_in, l_spared = g_hits_spared, l_kills_sent = g_kills_sent;
    l_kills_in = g_kills_in;
    return b;
}

std::string status()
{
    MEMORYSTATUSEX vm{};
    vm.dwLength = sizeof vm;
    GlobalMemoryStatusEx(&vm);
    char d[320];
    snprintf(d, sizeof d, "cell_contacts: creature_pairs=%u bite_events=%u nonfinite_skipped=%u nonfinite_dropped=%u stale_targets=%u own_nf=%u shared_parts=%u vm_free_mb=%llu created=%u destroyed=%u leave_writes=%u leave_dropped=%u\n", g_creature_pairs, g_bite_events, g_nonfinite_sent_skipped, g_nonfinite_dropped, g_stale_targets, g_own_nf_events, g_shared_parts, vm.ullAvailVirtual >> 20, g_created, g_destroyed, g_leave_writes, g_leave_dropped);
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
    char t[640];
    size_t pcount = 0;
    for (auto& [id, pt] : g_ptiles)
        if (pt.list)
            pcount += (size_t)((pt.list->end - pt.list->begin) / 2);
    snprintf(t, sizeof t,
             "cell_tiles: fix=%d centres=%u partner_lists=%u partner_tiles=%u populated=%u shared=%u bg_skipped=%u passes=%u kept=%u resets=%u drained=%u pool_full=%u flushes=%u unshared=%u handed=%u tile_pool=%u/%u budget=%d starved=%u/%u fed=%u screen=%u send_capped=%u mirror_capped=%u cells=%u peak=%u level=%d nan_pop_skips=%u nan_heals=%u\n",
             (int)g_fix_partner_tiles, (unsigned)g_views.size(), (unsigned)g_ptiles.size(), (unsigned)pcount, g_tiles_partner,
             g_tiles_shared, g_tiles_bg, g_tile_passes, g_tiles_kept, g_tile_resets, g_tiles_drained, g_tiles_full, g_tiles_flush, g_tiles_unshared, g_tiles_handed,
             G() ? F<uint32_t>(G(), 0x38 + 0xC) : 0, G() ? F<uint32_t>(G(), 0x38 + 0x18) : 0, (int)g_fix_budget, g_tiles_starved,
             (unsigned)g_starved.size(), g_tiles_fed, g_tiles_core, g_send_capped, g_mirror_capped, cells_live(), g_cells_peak, g_tile_level, g_nan_pop_skips, g_nan_heals);
    char j[96];
    snprintf(j, sizeof j, "cell_guest_fx: joins_after_load=%u bite_rings=%u\n", g_joins_load, g_bite_rings); // BUG-0352, BUG-0350
    return b + std::string(d) + t + j;
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

bool food_total(int& food)
{
    uint8_t* s = in_cell() ? S() : nullptr;
    if (!s)
        return false;
    food = F<int32_t>(s, 0x1C);
    return true;
}

// BUG-0351 diagnostics: what the cell editor's DNA budget is made of. The game's request (0xE59800)
// passes DNAForFood(S+0x1C) - S+0x2C (spent) to the editor (docs/re/cell.md#cell-editor-dna).
void dna_note(const char* when)
{
    uint8_t* s = S();
    if (!s)
        return;
    int food = F<int32_t>(s, 0x1C), spent = F<int32_t>(s, 0x2C);
    using DnaFor_t = int(__fastcall*)(int);
    int dna = a_DnaFor && food >= 0 && food <= 1000 ? ((DnaFor_t)a_DnaFor)(food) : -1;
    H->log(M, SK_INFO, "cell: DNA %s: food=%d plant=%d spent=%d dna_for_food=%d budget=%d", when, food,
           F<int32_t>(s, 0x20), spent, dna, dna >= 0 ? dna - spent : -1);
}

void join_partner()
{
    uint8_t* a = in_cell() ? avatar() : nullptr;
    net::Vec3 at;
    if (a && !nearest_partner(1e9f, &at).empty())
        stop_at(a, {at.x + 2.5f, at.y + 1.f, 0.f});
}

} // namespace cellsync
