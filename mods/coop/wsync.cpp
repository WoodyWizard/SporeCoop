// Host-owned world state (wsync.h, docs/sync-architecture-plan.md#sync-wsync).
#include "wsync.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

#include "cellsync.h"
#include "civ.h"
#include "fixes.h"
#include "game.h"
#include "modes.h"
#include "net/protocol.h"
#include "sporekit_game.hpp"

namespace wsync {

static const SkHost* H;
static SkMod* M;
static Ctx C;

static uintptr_t S(const char* n) { return H ? H->sym(n) : 0; }
template <typename T>
static T F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }
static bool R(void* p, size_t n) { return p && skg::readable(p, n); }
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float bitsf(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

constexpr int kMaxV = 6, kMaxN = 48;
struct Entry {
    uint32_t key = 0;
    uint32_t v[kMaxV] = {};
};
using Table = std::vector<Entry>;

struct Row {
    uint8_t id;
    const char* name;
    uint8_t nv;
    const bool* on;          // the row's fix switch (off: the guest applies nothing of this row)
    bool (*active)(uint32_t mode);
    bool (*read)(Table& out); // this game's own values (false: not available now)
    // guest: bring this game to `want`; apply false: only count the differences. Returns the differences seen.
    int (*sync)(const Table& want, bool apply);
    bool continuous;         // changes every frame (the sky): sent once a second, a change does not trigger a send
    std::string (*fmt)(const Entry& e);
};

// ---- the switches (COOP_FIX; off = the code before the fix)
// BUG-0579: the sky (planet context orientation) and the day's length as a row, re-applied whenever the guest's
// differs; off: MSG_WORLD_TIME as before (the sky only, applied once a second while the guest has an avatar)
static bool g_tod_fix = true;
COOP_FIX("wsync.tod", "BUG-0579", g_tod_fix, "wsync: a guest's time of day and day length follow the host's after every transition");
// BUG-0565: how every village feels about the player tribe follows the host's (ApplyRelationship)
static bool g_rel_fix = true;
COOP_FIX("wsync.tribe_rel", "BUG-0565", g_rel_fix, "wsync: a guest's villages feel about the player tribe as the host's do");
// BUG-0564 (goal): the Tribe goal card's village and the stage's goal progress follow the host's
static bool g_goal_fix = true;
COOP_FIX("wsync.tribe_goal", "BUG-0564", g_goal_fix, "wsync: a guest's Tribe goal card and goal progress are the host's");
// BUG-0577: the stages done (cPlayer consequence traits) follow the host's
static bool g_stages_fix = true;
COOP_FIX("wsync.stages", "BUG-0577", g_stages_fix, "wsync: a guest's stages done are the host's (also after a rejoin)");

// ---- game access
static void* noun_mgr()
{
    uintptr_t f = S("Simulator::cGameNounManager::Get");
    return f ? skg::cdecl_call<void*>(f) : nullptr;
}
static void* player_tribe()
{
    void* nm = noun_mgr();
    void* t = R(nm, 0x78) ? F<void*>(nm, 0x70) : nullptr;
    return R(t, 0x350) ? t : nullptr;
}
static void* cplayer()
{
    void* nm = noun_mgr();
    void* p = R(nm, 0x78) ? F<void*>(nm, 0x74) : nullptr;
    return R(p, 0x1280) ? p : nullptr;
}
static void* rel_mgr()
{
    uintptr_t f = S("Simulator::cRelationshipManager::Get");
    return f ? skg::cdecl_call<void*>(f) : nullptr;
}
static uint32_t pol(void* o) { return o ? skg::vcall<uint32_t>(o, 0x4C / 4) : 0xFFFFFFFFu; }
static float rel_abs(void* mgr, uint32_t a, uint32_t b)
{
    uintptr_t f = S("Simulator::cRelationshipManager::CalculateRelationshipAbsolute");
    return mgr && f ? skg::thiscall<float>(f, mgr, a, b, 0) : NAN;
}
static float rel(void* mgr, uint32_t a, uint32_t b)
{
    uintptr_t f = S("Simulator::cRelationshipManager::CalculateRelationship");
    return mgr && f ? skg::thiscall<float>(f, mgr, a, b, 0) : NAN;
}

constexpr uint32_t kTribeNoun = 0x018C6D19;
struct TribeRef {
    void* t;
    uint32_t pol;
    float x, y, z;
};
static std::vector<TribeRef> npc_tribes()
{
    std::vector<TribeRef> out;
    void* pt = player_tribe();
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* v = (std::vector<TribeRef>*)u;
            if (noun == kTribeNoun && game::object_alive(o) && R(o, 0x350) && v->size() < kMaxN) {
                float* p = (float*)((uint8_t*)o + 0x124);
                v->push_back({o, pol(o), p[0], p[1], p[2]});
            }
            return true;
        },
        &out);
    for (auto it = out.begin(); it != out.end();)
        it = it->t == pt ? out.erase(it) : std::next(it);
    return out;
}
// the guest's tribe for a host entry. BUG-0591: a village the host's game formed during play is made on the guest
// (tribe.npc_tribes, SpawnNpcTribe at the host's centre, 0.0 m) with the guest's own next political id: the host's
// new villages 2, 3, 4 and the guest's copies got ids in another order, so the host's id of one village was the
// guest's id of another and its relation and food went to that one (play_tribe lab 20261105-015320, from the host's
// forming at 418 s on). Villages do not move: the closest within 40 m first, the same political id only when no
// village is that close. Off (wsync.tribe_pos off): the political id first (0.7.7).
static bool g_tribe_pos_fix = true;
COOP_FIX("wsync.tribe_pos", "BUG-0591", g_tribe_pos_fix, "wsync: a village's relation, food and goal card reach the guest's village at the same place (not the same id)");
static const TribeRef* match(const std::vector<TribeRef>& mine, uint32_t hpol, float x, float y, float z)
{
    const TribeRef* best = nullptr;
    float bd = 40.f * 40.f;
    for (auto& t : mine) {
        float d = (t.x - x) * (t.x - x) + (t.y - y) * (t.y - y) + (t.z - z) * (t.z - z);
        if (d < bd)
            bd = d, best = &t;
    }
    if (g_tribe_pos_fix && best)
        return best;
    for (auto& t : mine)
        if (t.pol == hpol)
            return &t;
    return best;
}

static bool planet(uint32_t m)
{
    return m == modes::kCell || m == modes::kCreature || m == modes::kTribe || m == modes::kCiv || m == modes::kSpace;
}
static bool sky(uint32_t m) { return m == modes::kCreature || m == modes::kTribe || m == modes::kCiv; }
static bool tribe_mode(uint32_t m) { return m == modes::kTribe; }

static char g_fmt[160];
static std::string fmt_f(const Entry& e, int n)
{
    std::string s;
    snprintf(g_fmt, sizeof g_fmt, "key=%08X", e.key);
    s = g_fmt;
    for (int i = 0; i < n; i++) {
        snprintf(g_fmt, sizeof g_fmt, " %.4f", bitsf(e.v[i]));
        s += g_fmt;
    }
    return s;
}

// ---- row: the sky (the planet context's orientation, as MSG_WORLD_TIME sends it)
static bool r_tod(Table& out)
{
    net::Quat q;
    if (!game::time_of_day(q))
        return false;
    Entry e;
    e.v[0] = fbits(q.x), e.v[1] = fbits(q.y), e.v[2] = fbits(q.z), e.v[3] = fbits(q.w);
    out.push_back(e);
    return true;
}
static int s_tod(const Table& want, bool apply)
{
    net::Quat q;
    if (want.empty() || !game::time_of_day(q))
        return 0;
    const Entry& w = want[0];
    float wx = bitsf(w.v[0]), wy = bitsf(w.v[1]), wz = bitsf(w.v[2]), ww = bitsf(w.v[3]);
    float dot = std::fabs(q.x * wx + q.y * wy + q.z * wz + q.w * ww);
    // 1e-4: about 0.03 rad of the day (30 s of a 16 min day); the host's sends are 1 s apart
    if (1.f - dot <= 1e-4f)
        return 0;
    if (apply)
        game::set_time_of_day(net::Quat{wx, wy, wz, ww});
    return 1;
}
static std::string f_tod(const Entry& e) { return fmt_f(e, 4); }

// ---- row: the day's length (cTimeOfDay +0x24, SetDayDuration)
static void* tod_obj()
{
    uintptr_t f = S("Simulator::cTimeOfDay::Get");
    void* t = f ? skg::cdecl_call<void*>(f) : nullptr;
    return R(t, 0x28) ? t : nullptr;
}
static bool r_day(Table& out)
{
    void* t = tod_obj();
    if (!t)
        return false;
    Entry e;
    e.v[0] = fbits(F<float>(t, 0x24));
    out.push_back(e);
    return true;
}
static int s_day(const Table& want, bool apply)
{
    void* t = tod_obj();
    uintptr_t set = S("Simulator::cTimeOfDay::SetDayDuration");
    if (want.empty() || !t)
        return 0;
    float w = bitsf(want[0].v[0]);
    if (!(w > 1.f) || std::fabs(F<float>(t, 0x24) - w) < 0.01f)
        return 0;
    if (apply && set)
        skg::thiscall<void>(set, t, w);
    return 1;
}
static std::string f_day(const Entry& e) { return fmt_f(e, 1); }

// ---- row: the stages done (cPlayer +0x10FC..+0x110C, docs/re/creations.md#creations-cplayer-fields)
static bool r_stages(Table& out)
{
    void* p = cplayer();
    if (!p)
        return false;
    Entry e;
    for (int i = 0; i < 5; i++)
        e.v[i] = F<uint32_t>(p, 0x10FC + 4 * i);
    out.push_back(e);
    return true;
}
static int s_stages(const Table& want, bool apply)
{
    void* p = cplayer();
    if (want.empty() || !p)
        return 0;
    int n = 0;
    for (int i = 0; i < 5; i++) {
        uint32_t& mine = *(uint32_t*)((uint8_t*)p + 0x10FC + 4 * i);
        if (mine == want[0].v[i])
            continue;
        n++;
        if (apply)
            mine = want[0].v[i]; // no setter: the History bar and the stage cards read the field
    }
    return n;
}
static std::string f_stages(const Entry& e)
{
    snprintf(g_fmt, sizeof g_fmt, "traits=%08X,%08X,%08X,%08X,%08X", e.v[0], e.v[1], e.v[2], e.v[3], e.v[4]);
    return g_fmt;
}

// ---- row: how every NPC tribe feels about the player tribe (docs/re/tribe.md B)
constexpr uint32_t kRelDelta = 0x530CF07; // the social success's event: its scale is a raw delta
static bool r_rel(Table& out)
{
    void* pt = player_tribe();
    void* mgr = rel_mgr();
    if (!pt || !mgr)
        return false;
    uint32_t pp = pol(pt);
    for (auto& t : npc_tribes()) {
        Entry e;
        e.key = t.pol;
        e.v[0] = fbits(rel(mgr, t.pol, pp));
        e.v[1] = fbits(t.x), e.v[2] = fbits(t.y), e.v[3] = fbits(t.z);
        out.push_back(e);
    }
    return true;
}
static uint32_t g_rel_stuck; // tests: applies the game did not take
static int s_rel(const Table& want, bool apply)
{
    void* pt = player_tribe();
    void* mgr = rel_mgr();
    uintptr_t ap = S("Simulator::cRelationshipManager::ApplyRelationship");
    if (!pt || !mgr || !ap)
        return 0;
    uint32_t pp = pol(pt);
    auto mine = npc_tribes();
    int n = 0;
    for (auto& w : want) {
        const TribeRef* t = match(mine, w.key, bitsf(w.v[1]), bitsf(w.v[2]), bitsf(w.v[3]));
        if (!t)
            continue;
        float hv = bitsf(w.v[0]);
        float cur = rel(mgr, t->pol, pp);
        if (!std::isfinite(hv) || !std::isfinite(cur) || std::fabs(cur - hv) < 0.05f)
            continue;
        n++;
        if (!apply)
            continue;
        // the delta to the unclamped value (CalculateRelationship clamps to -10..10): the result is the host's
        float a = rel_abs(mgr, t->pol, pp);
        float d = hv - (std::isfinite(a) ? a : cur);
        skg::thiscall<float>(ap, mgr, t->pol, pp, kRelDelta, d);
        if (std::fabs(rel(mgr, t->pol, pp) - hv) >= 0.05f)
            g_rel_stuck++;
    }
    return n;
}
static std::string f_rel(const Entry& e)
{
    snprintf(g_fmt, sizeof g_fmt, "key=%08X rel=%.2f pos=(%.1f %.1f %.1f)", e.key, bitsf(e.v[0]), bitsf(e.v[1]),
             bitsf(e.v[2]), bitsf(e.v[3]));
    return g_fmt;
}

// ---- row: the Tribe goal card (the Tribe Mission Manager's current mission, docs/re/tribe.md#tribe-wsync) and the
// stage's goal progress (cPlayer +0x10F0 / +0x10F4)
static void* mission_mgr()
{
    uintptr_t g = S("SporeCoop::TribeMissionManager_Get");
    void* m = g ? skg::cdecl_call<void*>(g) : nullptr;
    return R(m, 0x88) ? m : nullptr;
}
static void* current_target(void* mgr, uint32_t* state)
{
    void* m = mgr ? F<void*>(mgr, 0x74) : nullptr;
    if (!R(m, 0x94))
        return nullptr;
    if (state)
        *state = F<uint32_t>(m, 0x7C);
    void* t = F<void*>(m, 0x80);
    return R(t, 0x350) ? t : nullptr;
}
static int g_goal_card = -1;
static bool r_goal(Table& out)
{
    void* p = cplayer();
    void* mgr = mission_mgr();
    if (!p || !mgr)
        return false;
    uint32_t st = 0;
    void* t = current_target(mgr, &st);
    Entry e;
    e.key = t ? pol(t) : 0xFFFFFFFFu;
    float* pos = t ? (float*)((uint8_t*)t + 0x124) : nullptr;
    e.v[0] = fbits(pos ? pos[0] : 0), e.v[1] = fbits(pos ? pos[1] : 0), e.v[2] = fbits(pos ? pos[2] : 0);
    e.v[3] = st;
    e.v[4] = fbits(F<float>(p, 0x10F0)), e.v[5] = fbits(F<float>(p, 0x10F4));
    out.push_back(e);
    g_goal_card = -1; // tests: the HUD shows a card of the current mission (read only for the dump)
    uintptr_t pg = S("SporeCoop::MissionCardPanel_Get"), has = S("SporeCoop::MissionCardPanel_HasCard");
    void* panel = pg ? skg::cdecl_call<void*>(pg) : nullptr;
    void* m = F<void*>(mgr, 0x74);
    if (panel && has && m)
        g_goal_card = skg::thiscall<bool>(has, panel, m) ? 1 : 0;
    return true;
}
static void* g_goal_target; // the tribe SetTargetTribe is called with outside the frame (UI: the goal card)
static void goal_task(void*)
{
    void* mgr = mission_mgr();
    uintptr_t set = S("SporeCoop::TribeMission_SetTargetTribe");
    void* t = g_goal_target;
    g_goal_target = nullptr;
    if (!mgr || !set || !t || !game::object_alive(t) || game::mode_id() != modes::kTribe)
        return;
    bool still = false; // the tribe is still in the game's list (not freed since the frame)
    for (auto& x : npc_tribes())
        still |= x.t == t;
    if (still)
        skg::thiscall<void>(set, mgr, t);
}
static int s_goal(const Table& want, bool apply)
{
    void* p = cplayer();
    void* mgr = mission_mgr();
    if (want.empty() || !p || !mgr)
        return 0;
    const Entry& w = want[0];
    int n = 0;
    float hp = bitsf(w.v[4]), ht = bitsf(w.v[5]);
    float& gp = *(float*)((uint8_t*)p + 0x10F0);
    float& gt = *(float*)((uint8_t*)p + 0x10F4);
    if (std::isfinite(hp) && std::isfinite(ht) && ht > 0 && (std::fabs(gp - hp) > 1e-3f || std::fabs(gt - ht) > 1e-3f)) {
        n++;
        if (apply)
            gp = hp, gt = ht; // the HUD's bar reads them every frame (0xCCDDB0)
    }
    if (w.key != 0xFFFFFFFFu) {
        auto mine = npc_tribes();
        const TribeRef* want_t = match(mine, w.key, bitsf(w.v[0]), bitsf(w.v[1]), bitsf(w.v[2]));
        void* cur = current_target(mgr, nullptr);
        if (want_t && cur != want_t->t) {
            n++;
            if (apply && !g_goal_target) {
                g_goal_target = want_t->t;
                H->run_outside_frame(goal_task, nullptr);
            }
        }
    }
    return n;
}
static std::string f_goal(const Entry& e)
{
    snprintf(g_fmt, sizeof g_fmt, "target=%08X pos=(%.1f %.1f %.1f) state=%u progress=%.1f/%.1f card=%d", e.key,
             bitsf(e.v[0]), bitsf(e.v[1]), bitsf(e.v[2]), e.v[3], bitsf(e.v[4]), bitsf(e.v[5]), g_goal_card);
    return g_fmt;
}

// ---- row: Cell, the DNA the host's editors spent (cCellSerializableData +0x2C; the food +0x1C is MSG_CELL_FOOD's):
// what the guest's Cell DNA shows is DNAForFood(food) - spent (docs/re/cell.md#cell-editor-dna)
static bool g_cell_spent_fix = true;
COOP_FIX("wsync.cell_spent", "BUG-0560", g_cell_spent_fix, "wsync: a guest's Cell DNA counts what the host's editor spent");
static bool cell_mode(uint32_t m) { return m == modes::kCell; }
static bool r_spent(Table& out)
{
    int v;
    if (!cellsync::spent_dna(v))
        return false;
    Entry e;
    e.v[0] = (uint32_t)v;
    e.v[1] = (uint32_t)cellsync::editor_budget();
    out.push_back(e);
    return true;
}
static int s_spent(const Table& want, bool apply)
{
    int v;
    if (want.empty() || !cellsync::spent_dna(v) || v == (int)want[0].v[0])
        return 0;
    if (apply)
        cellsync::set_spent_dna((int)want[0].v[0]);
    return 1;
}
static std::string f_spent(const Entry& e)
{
    snprintf(g_fmt, sizeof g_fmt, "spent=%d budget=%d", (int)e.v[0], (int)e.v[1]);
    return g_fmt;
}

// ---- row: every NPC tribe's food (BUG-0580): the bundles of its cGameBundleContainer (+0x20C, vector +4/+8, amount
// +0x134, as cTribe::GetTotalFood sums them). A guest's NPC tribes are the host's members' mirrors: they gather and
// spend nothing of the host's (the detector's Tribe:state:tribe_food, 145 on the host, 0 on the guest). The guest
// changes its copy by the difference through the game's own cTribe::AddFood (positive: the bundles a gathering
// makes; negative: the game's spending, RemoveBundles), as res.food_add does for the player tribe.
static bool g_tribe_food_fix = true;
COOP_FIX("wsync.tribe_food", "BUG-0580", g_tribe_food_fix, "wsync: a guest's NPC tribes have the host's food");
static float npc_food(void* t)
{
    uint8_t* cont = (uint8_t*)t + 0x20C;
    if (!R(cont, 0xC))
        return NAN;
    void** b = F<void**>(cont, 4);
    void** e = F<void**>(cont, 8);
    if (b == e)
        return 0.f; // never filled or emptied by the game's spending
    if (!b || e < b || e - b > 256 || !R(b, (e - b) * 4))
        return NAN;
    float v = 0;
    for (void** it = b; it < e; it++)
        if (*it && R(*it, 0x138))
            v += F<float>(*it, 0x134);
    return v;
}
static bool r_tfood(Table& out)
{
    if (!player_tribe())
        return false;
    for (auto& t : npc_tribes()) {
        float f = npc_food(t.t);
        if (!std::isfinite(f))
            continue;
        Entry e;
        e.key = t.pol;
        e.v[0] = fbits(std::floor(f)); // whole food: the fraction a gathering adds a frame at a time is not sent
        e.v[1] = fbits(t.x), e.v[2] = fbits(t.y), e.v[3] = fbits(t.z);
        out.push_back(e);
    }
    return true;
}
static uint32_t g_tfood_adds; // tests: AddFood calls on NPC tribes
static int s_tfood(const Table& want, bool apply)
{
    uintptr_t add = S("SporeCoop::cTribe_AddFood");
    if (!add || !player_tribe())
        return 0;
    auto mine = npc_tribes();
    int n = 0;
    for (auto& w : want) {
        const TribeRef* t = match(mine, w.key, bitsf(w.v[1]), bitsf(w.v[2]), bitsf(w.v[3]));
        if (!t)
            continue;
        float hv = bitsf(w.v[0]), cur = npc_food(t->t);
        if (!std::isfinite(hv) || !std::isfinite(cur))
            continue;
        int d = (int)std::lround(hv - std::floor(cur));
        if (d == 0)
            continue;
        n++;
        if (!apply)
            continue;
        skg::thiscall<void>(add, t->t, d);
        g_tfood_adds++;
    }
    return n;
}
static std::string f_tfood(const Entry& e)
{
    snprintf(g_fmt, sizeof g_fmt, "key=%08X food=%.0f pos=(%.1f %.1f %.1f)", e.key, bitsf(e.v[0]), bitsf(e.v[1]),
             bitsf(e.v[2]), bitsf(e.v[3]));
    return g_fmt;
}

// ---- Civ (BUG-0593): the NPC civilizations' feelings about the player civilization and their money, and the
// owners of every city's buildings and turrets. A guest's Civ AI is the host's mirror (it captures, buys and converts
// nothing itself), so what the host's game changes there stayed the save's on the guest: the relation a capture
// costs (000021CB -3.21 on the host, 3.00 on the guest), an NPC's money, and the buildings of a captured city (the
// city's owner comes with MSG_CIV_STATE, civ.cpp guest_cities, through the game's cCity::ChangeOwner, which gives
// the buildings the new owner only when Capture set the city's +0x2E2 flag first: 0xBE4C80 City_SetPartsOwner).
constexpr uint32_t kCivNoun = 0x018C816A, kCityNoun = 0x018C43E8;
static bool civ_mode(uint32_t m) { return m == modes::kCiv; }
static void* player_civ()
{
    void* nm = noun_mgr();
    uintptr_t f = S("Simulator::cGameNounManager::GetPlayerCivilization");
    return nm && f ? skg::thiscall<void*>(f, nm) : nullptr;
}
static std::vector<void*> objects_of(uint32_t want)
{
    struct Q {
        uint32_t noun;
        std::vector<void*> v;
    } q{want, {}};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* q = (Q*)u;
            if (noun == q->noun && game::object_alive(o) && q->v.size() < kMaxN)
                q->v.push_back(o);
            return true;
        },
        &q);
    return q.v;
}
static std::vector<void*> npc_civs()
{
    void* pc = player_civ();
    std::vector<void*> out;
    for (void* c : objects_of(kCivNoun))
        if (c != pc && R(c, 0x9C))
            out.push_back(c);
    return out;
}

static bool g_civ_rel_fix = true;
COOP_FIX("wsync.civ_rel", "BUG-0593", g_civ_rel_fix, "wsync: a guest's other nations feel about the player civilization as the host's do");
static bool r_civ_rel(Table& out)
{
    void* pc = player_civ();
    void* mgr = rel_mgr();
    if (!pc || !mgr)
        return false;
    uint32_t pp = pol(pc);
    for (void* c : npc_civs()) {
        Entry e;
        e.key = pol(c);
        e.v[0] = fbits(rel(mgr, e.key, pp));
        out.push_back(e);
    }
    return true;
}
static int s_civ_rel(const Table& want, bool apply)
{
    void* pc = player_civ();
    void* mgr = rel_mgr();
    uintptr_t ap = S("Simulator::cRelationshipManager::ApplyRelationship");
    if (!pc || !mgr || !ap)
        return 0;
    uint32_t pp = pol(pc);
    std::vector<uint32_t> mine;
    for (void* c : npc_civs())
        mine.push_back(pol(c));
    int n = 0;
    for (auto& w : want) {
        if (std::find(mine.begin(), mine.end(), w.key) == mine.end())
            continue; // the civilizations come from the save: the same political ids in both games
        float hv = bitsf(w.v[0]), cur = rel(mgr, w.key, pp);
        if (!std::isfinite(hv) || !std::isfinite(cur) || std::fabs(cur - hv) < 0.05f)
            continue;
        n++;
        if (!apply)
            continue;
        float a = rel_abs(mgr, w.key, pp);
        skg::thiscall<float>(ap, mgr, w.key, pp, kRelDelta, hv - (std::isfinite(a) ? a : cur));
        if (std::fabs(rel(mgr, w.key, pp) - hv) >= 0.05f)
            g_rel_stuck++;
    }
    return n;
}
static std::string f_civ_rel(const Entry& e)
{
    snprintf(g_fmt, sizeof g_fmt, "key=%08X rel=%.2f", e.key, bitsf(e.v[0]));
    return g_fmt;
}

static bool g_civ_money_fix = true;
COOP_FIX("wsync.civ_money", "BUG-0593", g_civ_money_fix, "wsync: a guest's other nations have the host's money");
static bool r_civ_money(Table& out)
{
    if (!player_civ())
        return false;
    for (void* c : npc_civs()) {
        Entry e;
        e.key = pol(c);
        e.v[0] = fbits(std::floor(F<float>(c, 0x98))); // cCivilization mWealth (GetMoney adds the NPC budgets +0x4AC / +0x4B0)
        out.push_back(e);
    }
    return true;
}
static int s_civ_money(const Table& want, bool apply)
{
    if (!player_civ())
        return 0;
    int n = 0;
    for (void* c : npc_civs()) {
        uint32_t cp = pol(c);
        for (auto& w : want) {
            if (w.key != cp)
                continue;
            float hv = bitsf(w.v[0]);
            float& cur = *(float*)((uint8_t*)c + 0x98);
            if (!std::isfinite(hv) || std::fabs(cur - hv) < 1.f)
                break;
            n++;
            if (apply)
                cur = hv; // no setter: AddMoney applies the difficulty, SpendMoney refills an NPC's budget
            break;
        }
    }
    return n;
}
static std::string f_civ_money(const Entry& e)
{
    snprintf(g_fmt, sizeof g_fmt, "key=%08X money=%.0f", e.key, bitsf(e.v[0]));
    return g_fmt;
}

// city_owner: key = the city's owner, v0..2 its position; a guest's city near it whose buildings or turrets have
// another owner than the city gets them through the game's own City_SetPartsOwner (ChangeOwner's +0x2E2 step:
// SetPoliticalID of every building, turret and +0x308 part, then the new owner's building models)
static bool g_city_owner_fix = true;
COOP_FIX("wsync.city_owner", "BUG-0593", g_city_owner_fix, "wsync: the buildings of a city the host's player took are the player's on a guest");
static uint32_t g_parts_fixed; // tests: City_SetPartsOwner calls
static int parts_off(void* city, uint32_t cp)
{
    int n = 0;
    for (uintptr_t off : {0x340u, 0x354u}) { // mBuildings, mTurrets
        void** b = F<void**>(city, off);
        void** e = F<void**>(city, off + 4);
        if (!b || e < b || e - b > 64 || !R(b, (e - b) * 4))
            continue;
        for (void** it = b; it < e; it++)
            if (*it && R(*it, 0x40) && pol(*it) != cp)
                n++;
    }
    return n;
}
static bool r_city(Table& out)
{
    if (!player_civ())
        return false;
    for (void* c : objects_of(kCityNoun)) {
        if (!R(c, 0x7F0))
            continue;
        float* p = (float*)((uint8_t*)c + 0x124);
        Entry e;
        e.key = pol(c);
        e.v[0] = fbits(p[0]), e.v[1] = fbits(p[1]), e.v[2] = fbits(p[2]);
        e.v[3] = (uint32_t)parts_off(c, e.key); // the host's own (0 when the game is right)
        out.push_back(e);
    }
    return true;
}
static int s_city(const Table& want, bool apply)
{
    void* nm = noun_mgr();
    uintptr_t byPol = S("Simulator::cGameNounManager::GetCivilizationByPoliticalID");
    uintptr_t setParts = S("SporeCoop::City_SetPartsOwner");
    if (!nm || !byPol || !setParts || !player_civ())
        return 0;
    auto mine = objects_of(kCityNoun);
    int n = 0;
    for (auto& w : want) {
        float x = bitsf(w.v[0]), y = bitsf(w.v[1]), z = bitsf(w.v[2]);
        void* best = nullptr;
        float bd = 30.f * 30.f;
        for (void* c : mine) {
            if (!R(c, 0x7F0))
                continue;
            float* p = (float*)((uint8_t*)c + 0x124);
            float d = (p[0] - x) * (p[0] - x) + (p[1] - y) * (p[1] - y) + (p[2] - z) * (p[2] - z);
            if (d < bd)
                bd = d, best = c;
        }
        if (!best)
            continue;
        uint32_t cp = pol(best);
        bool city_differs = cp != w.key; // MSG_CIV_STATE changes the city's owner (civ.cpp), counted here
        int off = parts_off(best, cp);
        if (!city_differs && off == 0)
            continue;
        n++;
        if (!apply || city_differs || civ::planner_holds_city(best))
            continue;
        void* civ = skg::thiscall<void*>(byPol, nm, cp);
        if (!civ)
            continue;
        skg::thiscall<void>(setParts, best, civ);
        g_parts_fixed++;
    }
    return n;
}
static std::string f_city(const Entry& e)
{
    snprintf(g_fmt, sizeof g_fmt, "owner=%08X pos=(%.1f %.1f %.1f) parts_other=%u", e.key, bitsf(e.v[0]), bitsf(e.v[1]),
             bitsf(e.v[2]), e.v[3]);
    return g_fmt;
}

static const Row kRows[] = {
    {1, "tod", 4, &g_tod_fix, sky, r_tod, s_tod, true, f_tod},
    {2, "day", 1, &g_tod_fix, sky, r_day, s_day, false, f_day},
    {3, "stages", 5, &g_stages_fix, planet, r_stages, s_stages, false, f_stages},
    {4, "tribe_rel", 4, &g_rel_fix, tribe_mode, r_rel, s_rel, false, f_rel},
    {5, "tribe_goal", 6, &g_goal_fix, tribe_mode, r_goal, s_goal, false, f_goal},
    {6, "cell_spent", 2, &g_cell_spent_fix, cell_mode, r_spent, s_spent, false, f_spent},
    {7, "tribe_food", 4, &g_tribe_food_fix, tribe_mode, r_tfood, s_tfood, false, f_tfood},
    {8, "civ_rel", 1, &g_civ_rel_fix, civ_mode, r_civ_rel, s_civ_rel, false, f_civ_rel},
    {9, "civ_money", 1, &g_civ_money_fix, civ_mode, r_civ_money, s_civ_money, false, f_civ_money},
    {10, "city_owner", 4, &g_city_owner_fix, civ_mode, r_city, s_city, false, f_city},
};
constexpr int kNRows = sizeof kRows / sizeof kRows[0];

// ---- engine state
struct HostRow {
    Table sent;
};
struct GuestRow {
    Table want;
    uint32_t mode = 0;
    double t = -1; // received
    uint32_t received = 0, applied = 0, reapplied = 0, checks = 0;
    int differ = 0;         // at the last check
    bool matched = false;   // the last check found no difference (an apply after it is a re-alignment)
};
static HostRow g_host[kNRows];
static GuestRow g_guest[kNRows];
static double g_send_at = 0, g_check_at = 0, g_read_at = 0;
static bool g_force = true;
constexpr double kRefresh = 1.0, kRead = 0.25, kCheck = 0.5, kForget = 6.0;

void init(const SkHost* host, SkMod* mod, const Ctx& ctx);

void on_session_end()
{
    for (auto& g : g_guest)
        g = GuestRow{};
    for (auto& h : g_host)
        h = HostRow{};
    g_force = true;
}

void on_player_join() { g_force = true; }

bool owns_time_of_day()
{
    return g_tod_fix && C.sess && C.sess->role() == net::Role::Client;
}

static bool same(const Table& a, const Table& b, int nv)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].key != b[i].key)
            return false;
        for (int j = 0; j < nv; j++)
            if (a[i].v[j] != b[i].v[j])
                return false;
    }
    return true;
}

static void host_tick(double now)
{
    uint32_t mode = game::mode_id();
    if (now - g_read_at < kRead && !g_force)
        return;
    g_read_at = now;
    bool due = g_force || now - g_send_at >= kRefresh;
    Table cur[kNRows];
    bool have[kNRows] = {};
    for (int i = 0; i < kNRows; i++) {
        const Row& r = kRows[i];
        if (!r.active(mode) || !r.read(cur[i]))
            continue;
        have[i] = true;
        if (!r.continuous && !same(cur[i], g_host[i].sent, r.nv))
            due = true;
    }
    if (!due)
        return;
    net::Writer w;
    w.u8(0);
    w.u32(mode);
    int n = 0;
    for (int i = 0; i < kNRows; i++)
        n += have[i];
    w.u8((uint8_t)n);
    for (int i = 0; i < kNRows; i++) {
        if (!have[i])
            continue;
        const Row& r = kRows[i];
        w.u8(r.id);
        w.u8(r.nv);
        w.u8((uint8_t)cur[i].size());
        for (auto& e : cur[i]) {
            w.u32(e.key);
            for (int j = 0; j < r.nv; j++)
                w.u32(e.v[j]);
        }
        g_host[i].sent = cur[i];
    }
    for (auto& p : C.sess->players())
        if (p.id != 0)
            C.sess->send_unreliable_to(p.id, net::MSG_WSTATE, w);
    g_send_at = now;
    g_force = false;
}

static void guest_tick(double now)
{
    if (now - g_check_at < kCheck)
        return;
    g_check_at = now;
    uint32_t mode = game::mode_id();
    bool playing = !C.playing || C.playing();
    for (int i = 0; i < kNRows; i++) {
        const Row& r = kRows[i];
        GuestRow& g = g_guest[i];
        if (g.t < 0 || now - g.t > kForget || g.mode != mode || !r.active(mode) || !playing) {
            g.matched = false;
            continue;
        }
        bool on = *r.on;
        if (!on) { // the switch off: this game is not touched or read (the code before the row)
            g.differ = -1;
            g.matched = false;
            continue;
        }
        int d = r.sync(g.want, true);
        g.checks++;
        g.differ = d;
        if (d > 0 && on) {
            g.applied++;
            if (g.matched)
                g.reapplied++; // the game drifted away after a match: a transition, re-aligned
        }
        g.matched = d == 0;
    }
}

void tick(double now)
{
    if (!C.sess || !C.sess->connected())
        return;
    if (C.sess->role() == net::Role::Host)
        host_tick(now);
    else if (C.sess->role() == net::Role::Client)
        guest_tick(now);
}

void on_message(uint8_t type, uint8_t from, net::Reader& rd)
{
    if (type != net::MSG_WSTATE || from != 0 || !C.sess || C.sess->role() != net::Role::Client)
        return;
    if (rd.u8() != 0)
        return;
    uint32_t mode = rd.u32();
    int n = rd.u8();
    double now = H->time_seconds();
    for (int k = 0; k < n && rd.ok(); k++) {
        uint8_t id = rd.u8();
        int nv = rd.u8();
        int ne = rd.u8();
        if (!rd.ok() || nv > kMaxV || ne > kMaxN)
            return;
        Table t;
        for (int e = 0; e < ne; e++) {
            Entry x;
            x.key = rd.u32();
            for (int j = 0; j < nv; j++)
                x.v[j] = rd.u32();
            t.push_back(x);
        }
        if (!rd.ok())
            return;
        for (int i = 0; i < kNRows; i++)
            if (kRows[i].id == id && kRows[i].nv == nv) {
                g_guest[i].want = std::move(t);
                g_guest[i].mode = mode;
                g_guest[i].t = now;
                g_guest[i].received++;
                break;
            }
    }
}

std::string status()
{
    std::string out;
    char b[200];
    double now = H->time_seconds();
    bool host = C.sess && C.sess->role() == net::Role::Host;
    for (int i = 0; i < kNRows; i++) {
        const Row& r = kRows[i];
        const GuestRow& g = g_guest[i];
        snprintf(b, sizeof b, "row %s on=%d active=%d %s want=%d age=%.1f received=%u checks=%u differ=%d applied=%u reapplied=%u\n",
                 r.name, (int)*r.on, (int)r.active(game::mode_id()), host ? "host" : "guest", (int)g.want.size(),
                 g.t < 0 ? -1.0 : now - g.t, g.received, g.checks, g.differ, g.applied, g.reapplied);
        out += b;
    }
    snprintf(b, sizeof b, "rel_stuck=%u tribe_food_adds=%u city_parts_fixed=%u\n", g_rel_stuck, g_tfood_adds, g_parts_fixed);
    out += b;
    return out;
}

// `coop.wsync` the rows' sync state; `coop.wsync dump [row]` this game's own values (tests compare the host's and
// the guest's); `coop.wsync want <row>` the host's values a guest holds; `coop.wsync rel <pol hex> <delta>` the
// game's ApplyRelationship on this game (tests: a village's feelings change in the host's game)
static int c_wsync(void*, int argc, const char* const* argv, SkOut* out)
{
    std::string o;
    if (argc >= 2 && (!strcmp(argv[1], "dump") || !strcmp(argv[1], "want"))) {
        bool want = !strcmp(argv[1], "want");
        for (int i = 0; i < kNRows; i++) {
            const Row& r = kRows[i];
            if (argc >= 3 && strcmp(argv[2], r.name))
                continue;
            Table t;
            if (want)
                t = g_guest[i].want;
            else if (!r.active(game::mode_id()) || !r.read(t))
                continue;
            for (auto& e : t)
                o += std::string(r.name) + " " + r.fmt(e) + "\n";
        }
        H->out_printf(out, "%send wsync\n", o.c_str());
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "food")) { // food <pol hex> <n>: the game's AddFood on that NPC tribe here
        uint32_t np = (uint32_t)strtoul(argv[2], nullptr, 16);
        uintptr_t add = S("SporeCoop::cTribe_AddFood");
        for (auto& t : npc_tribes())
            if (t.pol == np && add) {
                float before = npc_food(t.t);
                skg::thiscall<void>(add, t.t, atoi(argv[3]));
                H->out_printf(out, "food %08X: %.1f -> %.1f\n", np, before, npc_food(t.t));
                return 0;
            }
        H->out_printf(out, "no such NPC tribe here\n");
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "civmoney")) { // civmoney <pol hex> <n>: an NPC civilization's wealth += n here
        uint32_t np = (uint32_t)strtoul(argv[2], nullptr, 16);
        for (void* c : npc_civs())
            if (pol(c) == np) {
                float& w = *(float*)((uint8_t*)c + 0x98);
                float before = w;
                w += (float)atof(argv[3]);
                H->out_printf(out, "civmoney %08X: %.0f -> %.0f\n", np, before, w);
                return 0;
            }
        H->out_printf(out, "no such NPC civilization here\n");
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "rel")) {
        void* pt = game::mode_id() == modes::kCiv ? player_civ() : player_tribe(); // Civ: the player civilization
        void* mgr = rel_mgr();
        uintptr_t ap = S("Simulator::cRelationshipManager::ApplyRelationship");
        uint32_t np = (uint32_t)strtoul(argv[2], nullptr, 16);
        if (!pt || !mgr || !ap) {
            H->out_printf(out, "no player tribe here\n");
            return 0;
        }
        float before = rel(mgr, np, pol(pt));
        skg::thiscall<float>(ap, mgr, np, pol(pt), kRelDelta, (float)atof(argv[3]));
        H->out_printf(out, "rel %08X -> %08X: %.2f -> %.2f\n", np, pol(pt), before, rel(mgr, np, pol(pt)));
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "stage")) { // tests: `stage <0..4> <hex>` writes a consequence trait here
        void* p = cplayer();
        int i = atoi(argv[2]);
        if (!p || i < 0 || i > 4) {
            H->out_printf(out, "no cPlayer here\n");
            return 0;
        }
        *(uint32_t*)((uint8_t*)p + 0x10FC + 4 * i) = (uint32_t)strtoul(argv[3], nullptr, 16);
        H->out_printf(out, "stage %d trait %08X\n", i, F<uint32_t>(p, 0x10FC + 4 * i));
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "goal")) { // tests: the host's goal card on the village of this pol
        uint32_t np = (uint32_t)strtoul(argv[2], nullptr, 16);
        for (auto& t : npc_tribes())
            if (t.pol == np) {
                g_goal_target = t.t;
                H->run_outside_frame(goal_task, nullptr);
                H->out_printf(out, "goal %08X set\n", np);
                return 0;
            }
        H->out_printf(out, "no tribe %08X\n", np);
        return 0;
    }
    H->out_printf(out, "%s", status().c_str());
    return 0;
}

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
    H->command_add(mod, "coop.wsync",
                   "coop.wsync [dump [row]|want <row>|rel <pol> <delta>|goal <pol>] - host-owned world state (time of day, "
                   "stages done, village relations, the Tribe goal): rows, this game's values",
                   c_wsync, nullptr, SK_CMD_MAIN_THREAD);
}

} // namespace wsync
