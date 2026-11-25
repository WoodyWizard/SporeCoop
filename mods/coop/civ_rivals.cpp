// Civilization Rivals mode runtime implementation (WP-R5 of 0.8.0, BUG-1549, docs/11-rivals.md 11.3.4, 11.4-11.9).
#include "civ_rivals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "call_guard.h"
#include "civ_internal.h"
#include "fixes.h"
#include "game.h"
#include "lifecycle.h"
#include "modes.h"
#include "rivals.h"
#include "savestrip.h"
#include "species_line.h"
#include "sporekit_game.hpp"
#include "wire/rivals_faction.h"

namespace civ_rivals {

using namespace civ_rivals_logic;
using civ::C;
using civ::F;
using civ::H;
using civ::M;
using skg::thiscall;
using skg::vcall;

// BUG-1549: master switch for Civilization Rivals mode (own NPC nation takeover, guest perspective swap,
// per-player NPC diplomacy & influence, war PvP & ordinary city capture, capital inviolability & superweapon
// protection, alliance gifts & trade, >50% NPC cities win -> catch-up -> Civ draft).
// Off (SPORE_FIX_OFF=rivals.civ): disables all Civ Rivals mechanics and restores Together behavior.
static bool g_fix_rivals_civ = true;
COOP_FIX("rivals.civ", "BUG-1549", g_fix_rivals_civ,
         "civ: Civilization Rivals mode (own nation takeover, perspective swap, per-player NPC diplomacy, war city capture, inviolable capital, draft)");

// ---- Game addresses ----
static uintptr_t a_GetPlayerCiv = 0;
static uintptr_t a_GetPlayerPol = 0;
static uintptr_t a_CityIsPlayerOwned = 0;
static uintptr_t a_CitySetPlayerOwned = 0;
static uintptr_t a_CivAI = 0;
static uintptr_t a_CivIncomeTick = 0;
static uintptr_t a_CivSetSpeciesKey = 0;
static uintptr_t a_CivSetDesign = 0;
static uintptr_t a_CanTargetSW = 0;
static uintptr_t a_LaunchGlobalSW = 0;
static uintptr_t a_TerrainCursorGet = 0;
static uintptr_t a_TerrainCursorSetObj = 0;
static uintptr_t a_TradeDelivery = 0;
static uintptr_t a_RelMgrGet = 0;
static uintptr_t a_RelCalc = 0;
static uintptr_t a_RelCalcAbs = 0;
static uintptr_t a_RelApply = 0;
static uintptr_t a_GameCivGet = 0;
static uintptr_t a_GameCivShowWon = 0;

constexpr uint32_t kRelDelta = 0x530CF07; // raw delta relationship event (wsync.cpp)
constexpr uintptr_t kCivWealth = 0x98;
constexpr uintptr_t kCivDomesticBudget = 0x4AC;
constexpr uintptr_t kCivExpansionBudget = 0x4B0;
constexpr uintptr_t kCivIsPlayer = 0x89;
constexpr uintptr_t kCivSelectable = 0xD4;
constexpr uintptr_t kCityIsPlayer = 0x33C;
constexpr uintptr_t kCitySpatialOff = 0x120;

// ---- Hook originals ----
using GetPlayerCiv_t = void*(SK_THISCALL*)(void*);
using GetPlayerPol_t = uint32_t(SK_THISCALL*)(void*);
using CityIsPlayer_t = bool(SK_THISCALL*)(void*);
using CivAI_t = void(SK_THISCALL*)(void*);
using CanTargetSW_t = bool(SK_CDECL*)(int, const float*);
using LaunchGlobalSW_t = void(SK_THISCALL*)(void*);
using TradeDelivery_t = float(SK_THISCALL*)(void*, uint32_t, float, void*);

static GetPlayerCiv_t o_GetPlayerCiv = nullptr;
static GetPlayerPol_t o_GetPlayerPol = nullptr;
static CityIsPlayer_t o_CityIsPlayer = nullptr;
static CivAI_t o_CivAI = nullptr;
static CanTargetSW_t o_CanTargetSW = nullptr;
static LaunchGlobalSW_t o_LaunchGlobalSW = nullptr;
static TradeDelivery_t o_TradeDelivery = nullptr;

// ---- Runtime state ----
static bool g_inited = false;
static bool g_bypass_swap = false;
CALL_GUARD("civ_rivals.bypass_swap", g_bypass_swap);
static bool g_in_save = false;

static std::array<PlayerNation, kMaxPlayers> g_nations{};
static NpcRelationTable g_npc_rels;
static std::map<uint32_t, float> g_guest_npc_rels; // guest's received per-player NPC relations
static bool g_swap_applied = false;
static uint32_t g_orig_host_pol = 0;
static bool g_assign_dirty = false;
static bool g_npcrel_dirty = false;
static double g_last_assign_tx = -100.0;
static double g_last_npcrel_tx = -100.0;
static double g_last_win_eval = -100.0;
static double g_last_spawn_req = -100.0;
static double g_last_rel_sync = -100.0;
static bool g_race_ended = false;
static bool g_draft_started = false;
static bool g_transition_ready = false;
static uint8_t g_draft_winner = 255;
static std::map<uint8_t, std::vector<uint32_t>> g_draft_applied_ids; // pid -> applied design_ids
static std::set<uint8_t> g_species_applied;
static std::set<std::pair<uint8_t, uint32_t>> g_war_captured_from; // (winner, loser)

// ---- Diagnostics counters ----
static struct {
    uint32_t takeovers = 0;
    uint32_t ai_blocked = 0;
    uint32_t swap_hits = 0;
    uint32_t cap_blocked = 0;
    uint32_t conv_blocked = 0;
    uint32_t sw_blocked = 0;
    uint32_t pvp_allowed = 0;
    uint32_t pvp_blocked = 0;
    uint32_t war_captures = 0;
    uint32_t gifts_sent = 0;
    uint32_t gifts_done = 0;
    uint32_t gifts_rejected = 0;
    uint32_t spice_gifts = 0;
    uint32_t inf_bought = 0;
    uint32_t inf_turned = 0;
    uint32_t inf_protected = 0;
    uint32_t inf_smoothed = 0;
    uint32_t inf_trade = 0;
    uint32_t assign_tx = 0;
    uint32_t assign_rx = 0;
    uint32_t npcrel_tx = 0;
    uint32_t npcrel_rx = 0;
    uint32_t draft_applied = 0;
    uint32_t allied_trades = 0;
} g_st;

// ---- Helpers ----
static bool sess_connected() { return C.sess && C.sess->connected(); }
static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->connected() && C.sess->role() == net::Role::Client; }
static uint8_t my_id() { return C.sess ? C.sess->my_id() : 0; }
static double now_s() { return H ? H->time_seconds() : 0.0; }

bool enabled() { return g_fix_rivals_civ; }

bool active()
{
    return g_fix_rivals_civ && rivals::stage_is_rivals(rivals::Stage::Civ) && game::mode_id() == modes::kCiv &&
           sess_connected();
}

static void* orig_player_civ()
{
    void* nm = civ::nm();
    if (!nm)
        return nullptr;
    if (o_GetPlayerCiv)
        return o_GetPlayerCiv(nm);
    return civ::player_civ();
}

const void* orig_player_civ_ptr() { return orig_player_civ(); }

static uint32_t orig_player_pol()
{
    void* nm = civ::nm();
    if (!nm)
        return 0xFFFFFFFFu;
    if (o_GetPlayerPol)
        return o_GetPlayerPol(nm);
    void* pc = orig_player_civ();
    return pc ? civ::pol_of(pc) : 0xFFFFFFFFu;
}

static void* civ_by_pol(uint32_t pol)
{
    void* nm = civ::nm();
    if (!nm || !pol || pol == 0xFFFFFFFFu || !civ::a_CivByPol)
        return nullptr;
    return thiscall<void*>(civ::a_CivByPol, nm, pol);
}

static std::vector<void*> all_civs()
{
    void* nm = civ::nm();
    if (!nm || !civ::a_GetAllCivs)
        return {};
    return civ::vec_of(thiscall<void*>(civ::a_GetAllCivs, nm));
}

static Vec3 to_vec3(const net::Vec3& v) { return {v.x, v.y, v.z}; }

static void* rel_mgr() { return a_RelMgrGet ? skg::cdecl_call<void*>(a_RelMgrGet) : nullptr; }

static float rel_get(uint32_t a, uint32_t b)
{
    void* mgr = rel_mgr();
    if (!mgr || !a_RelCalc || !a || !b || a == 0xFFFFFFFFu || b == 0xFFFFFFFFu)
        return 0.0f;
    float v = thiscall<float>(a_RelCalc, mgr, a, b, 0);
    return std::isfinite(v) ? v : 0.0f;
}

static void rel_set(uint32_t a, uint32_t b, float target)
{
    void* mgr = rel_mgr();
    if (!mgr || !a_RelApply || !a_RelCalc || !a || !b || a == b || a == 0xFFFFFFFFu || b == 0xFFFFFFFFu)
        return;
    target = clamp_rel(target);
    float cur = thiscall<float>(a_RelCalc, mgr, a, b, 0);
    if (!std::isfinite(cur) || std::fabs(cur - target) < 0.05f)
        return;
    float abs_v = a_RelCalcAbs ? thiscall<float>(a_RelCalcAbs, mgr, a, b, 0) : cur;
    float base = std::isfinite(abs_v) ? abs_v : cur;
    thiscall<float>(a_RelApply, mgr, a, b, kRelDelta, target - base);
}

static uint32_t guest_my_pol()
{
    uint8_t me = my_id();
    if (me < kMaxPlayers && g_nations[me].assigned)
        return g_nations[me].pol_id;
    return 0;
}

static bool swap_active_now()
{
    return !g_bypass_swap && !g_in_save && active() && is_guest() && guest_my_pol() != 0;
}

void* civ_of_player(uint8_t pid)
{
    if (pid >= kMaxPlayers || !g_nations[pid].assigned)
        return nullptr;
    return civ_by_pol(g_nations[pid].pol_id);
}

int player_of_pol(uint32_t pol)
{
    if (!pol || pol == 0xFFFFFFFFu)
        return -1;
    for (int i = 0; i < kMaxPlayers; i++)
        if (g_nations[i].assigned && g_nations[i].pol_id == pol)
            return i;
    return -1;
}

bool is_guest_nation(void* civ)
{
    if (!civ || !active())
        return false;
    int pid = player_of_pol(civ::pol_of(civ));
    return pid >= 1 && pid < kMaxPlayers;
}

bool is_guest_city(void* city)
{
    if (!city || !active())
        return false;
    int pid = player_of_pol(civ::pol_of(city));
    return pid >= 1 && pid < kMaxPlayers;
}

bool is_player_capital(void* city, uint8_t* owner_out)
{
    if (!city || !active())
        return false;
    return is_capital_city_pos(to_vec3(civ::city_pos(city)), g_nations, owner_out);
}

// ---- Perspective swap restore & apply (R1) ----
static void restore_perspective()
{
    if (!g_swap_applied)
        return;
    g_swap_applied = false;
    if (game::mode_id() != modes::kCiv || !civ::nm())
        return;
    callguard::Hold bypass(g_bypass_swap);
    void* orig_pc = orig_player_civ();
    uint32_t orig_pol = orig_pc ? civ::pol_of(orig_pc) : g_orig_host_pol;
    for (void* c : all_civs()) {
        if (c && skg::readable(c, kCivIsPlayer + 1))
            F<uint8_t>(c, kCivIsPlayer) = (c == orig_pc) ? 1 : 0;
    }
    for (void* city : civ::all_cities()) {
        if (!city)
            continue;
        bool own = orig_pol && civ::pol_of(city) == orig_pol;
        if (a_CitySetPlayerOwned)
            thiscall<void>(a_CitySetPlayerOwned, city, own);
        else if (skg::readable(city, kCityIsPlayer + 1))
            F<uint8_t>(city, kCityIsPlayer) = own ? 1 : 0;
    }
    if (orig_pc && a_TerrainCursorGet && a_TerrainCursorSetObj) {
        if (void* cur = skg::cdecl_call<void*>(a_TerrainCursorGet))
            thiscall<void>(a_TerrainCursorSetObj, cur, (uint8_t*)orig_pc + kCivSelectable);
    }
}

static void apply_guest_perspective()
{
    if (!active() || !is_guest() || g_in_save)
        return;
    uint32_t my_pol = guest_my_pol();
    if (!my_pol)
        return;
    void* my_civ = civ_by_pol(my_pol);
    if (!my_civ || !skg::readable(my_civ, kCivExpansionBudget + 4))
        return;
    if (!g_orig_host_pol)
        g_orig_host_pol = orig_player_pol();

    // Update mIsPlayerOwned flags so GetMoney / SpendMoney / UI read guest's own nation
    for (void* c : all_civs()) {
        if (c && skg::readable(c, kCivIsPlayer + 1))
            F<uint8_t>(c, kCivIsPlayer) = (c == my_civ) ? 1 : 0;
    }
    // Sync authoritative wealth for guest's nation (zero NPC budgets so GetMoney == mWealth)
    const auto& me = g_nations[my_id()];
    if (std::isfinite(me.wealth) && me.wealth >= 0.0f) {
        F<float>(my_civ, kCivWealth) = me.wealth;
        F<float>(my_civ, kCivDomesticBudget) = 0.0f;
        F<float>(my_civ, kCivExpansionBudget) = 0.0f;
    }
    // Update city mbIsPlayerCity flags
    for (void* city : civ::all_cities()) {
        if (!city)
            continue;
        bool own = civ::pol_of(city) == my_pol;
        if (a_CitySetPlayerOwned)
            thiscall<void>(a_CitySetPlayerOwned, city, own);
        else if (skg::readable(city, kCityIsPlayer + 1))
            F<uint8_t>(city, kCityIsPlayer) = own ? 1 : 0;
    }
    // Point TerrainCursor selectable objects vector to guest's own nation (+0xD4)
    if (a_TerrainCursorGet && a_TerrainCursorSetObj) {
        if (void* cur = skg::cdecl_call<void*>(a_TerrainCursorGet))
            thiscall<void>(a_TerrainCursorSetObj, cur, (uint8_t*)my_civ + kCivSelectable);
    }
    if (!g_swap_applied) {
        g_swap_applied = true;
        if (H && M)
            H->log(M, SK_INFO, "civ_rivals: guest perspective swapped to nation pol=0x%08X (civ=%p wealth=%.0f, BUG-1549)",
                   (unsigned)my_pol, my_civ, me.wealth);
    }
}

static void reset_state()
{
    restore_perspective();
    // On host, restore mIsPlayerOwned = 0 on guest-assigned nations if leaving session/mode
    if (is_host() && game::mode_id() == modes::kCiv && civ::nm()) {
        void* orig_pc = orig_player_civ();
        for (int i = 1; i < kMaxPlayers; i++) {
            if (!g_nations[i].assigned)
                continue;
            if (void* gc = civ_by_pol(g_nations[i].pol_id); gc && gc != orig_pc && skg::readable(gc, kCivIsPlayer + 1))
                F<uint8_t>(gc, kCivIsPlayer) = 0;
        }
    }
    for (int i = 0; i < kMaxPlayers; i++)
        g_nations[i] = PlayerNation{(uint8_t)i, 0, {}, 0.0f, false};
    g_npc_rels.clear();
    g_guest_npc_rels.clear();
    g_orig_host_pol = 0;
    g_assign_dirty = false;
    g_npcrel_dirty = false;
    g_race_ended = false;
    g_draft_started = false;
    g_transition_ready = false;
    g_draft_winner = 255;
    g_draft_applied_ids.clear();
    g_species_applied.clear();
    g_war_captured_from.clear();
}

static void on_lifecycle_event(const lifecycle::Ctx& c)
{
    if (c.event == lifecycle::kPlayerLeave) {
        if (c.player >= 1 && c.player < kMaxPlayers && g_nations[c.player].assigned) {
            if (is_host() && game::mode_id() == modes::kCiv) {
                if (void* gc = civ_by_pol(g_nations[c.player].pol_id); gc && skg::readable(gc, kCivIsPlayer + 1))
                    F<uint8_t>(gc, kCivIsPlayer) = 0;
            }
            g_nations[c.player] = PlayerNation{c.player, 0, {}, 0.0f, false};
            g_species_applied.erase(c.player);
            g_assign_dirty = true;
        }
        return;
    }
    reset_state();
}

LIFECYCLE_HOOK("civ_rivals.lifecycle", lifecycle::kSessionEnd | lifecycle::kWorldChange | lifecycle::kPlayerLeave, -20,
               on_lifecycle_event, false);

static void on_save_start()
{
    g_in_save = true;
    restore_perspective();
}

static void on_save_end()
{
    g_in_save = false;
    if (active() && is_guest())
        apply_guest_perspective();
}

// ---- Hooks (R1, R2, R4, R7) ----
static void* SK_THISCALL h_GetPlayerCiv(void* nm)
{
    if (swap_active_now()) {
        if (void* my_civ = civ_by_pol(guest_my_pol())) {
            g_st.swap_hits++;
            return my_civ;
        }
    }
    return o_GetPlayerCiv(nm);
}

static uint32_t SK_THISCALL h_GetPlayerPol(void* nm)
{
    if (swap_active_now()) {
        uint32_t p = guest_my_pol();
        if (p) {
            g_st.swap_hits++;
            return p;
        }
    }
    return o_GetPlayerPol(nm);
}

static bool SK_THISCALL h_CityIsPlayer(void* spatial)
{
    if (swap_active_now() && spatial) {
        void* city = (uint8_t*)spatial - kCitySpatialOff;
        if (skg::readable(city, 0x34))
            return civ::pol_of(city) == guest_my_pol();
    }
    return o_CityIsPlayer(spatial);
}

static void SK_THISCALL h_CivAI(void* civ)
{
    if (active() && civ) {
        int pid = player_of_pol(civ::pol_of(civ));
        if (is_host() && pid >= 1 && pid < kMaxPlayers) {
            // R2: guest-assigned nation on host: skip NPC AI planner/diplomacy/growth/budgets,
            // run only the 6 s passive city + spice derrick income tick into mWealth (+0x98).
            g_st.ai_blocked++;
            if (a_CivIncomeTick)
                thiscall<void>(a_CivIncomeTick, civ);
            return;
        }
        if (is_guest() && pid >= 0 && pid < kMaxPlayers) {
            // Guest never runs NPC AI on any player's nation.
            g_st.ai_blocked++;
            return;
        }
    }
    o_CivAI(civ);
}

static bool SK_CDECL h_CanTargetSW(int sw_type, const float* target)
{
    if (active() && target) {
        uint8_t me = my_id();
        Vec3 pt{target[0], target[1], target[2]};
        if (superweapon_blocked_by_capital(me, sw_type, true, pt, g_nations))
            return false;
    }
    return o_CanTargetSW(sw_type, target);
}

static void SK_THISCALL h_LaunchGlobalSW(void* city)
{
    if (active() && city) {
        // R7: Global superweapons (types 2, 5, 8) hit all foreign cities including rival capitals.
        int attacker_pid = player_of_pol(civ::pol_of(city));
        uint8_t att = attacker_pid >= 0 ? (uint8_t)attacker_pid : 0xFF;
        if (superweapon_blocked_by_capital(att, 2, false, {}, g_nations)) {
            g_st.sw_blocked++;
            if (H && M)
                H->log(M, SK_INFO, "civ_rivals: blocked global superweapon launch from city %p (rival capital inviolable, BUG-1549)",
                       city);
            return;
        }
    }
    o_LaunchGlobalSW(city);
}

static float SK_THISCALL h_TradeDelivery(void* city, uint32_t trader_pol, float cargo, void* veh)
{
    float ret = o_TradeDelivery(city, trader_pol, cargo, veh);
    if (active() && is_host() && city && cargo > 0.0f) {
        int trader_pid = player_of_pol(trader_pol);
        int city_pid = player_of_pol(civ::pol_of(city));
        if (trader_pid >= 0 && trader_pid < kMaxPlayers && trader_pid != city_pid) {
            rivals::grant_influence((uint8_t)trader_pid, kTradeInfluenceGain, "trade route");
            g_st.inf_trade++;
            if (city_pid >= 0 && city_pid < kMaxPlayers && rivals::allied((uint8_t)trader_pid, (uint8_t)city_pid)) {
                rivals::grant_influence((uint8_t)city_pid, kTradeInfluenceGain, "allied trade");
                g_st.inf_trade++;
            }
            g_assign_dirty = true;
        }
    }
    return ret;
}

// ---- Host R2 takeover, wealth sync, NPC relations sync & stage end ----
static void host_broadcast_assign(double now, int only_peer = -1)
{
    if (!is_host() || !active())
        return;
    // Refresh live wealths before sending
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!g_nations[i].assigned)
            continue;
        if (void* c = civ_by_pol(g_nations[i].pol_id); c && skg::readable(c, kCivWealth + 4))
            g_nations[i].wealth = F<float>(c, kCivWealth);
    }
    net::Writer w;
    write_assign(w, g_nations);
    if (only_peer >= 1)
        rivals::send_faction_to((uint8_t)only_peer, rivals::Stage::Civ, wire::rivals_faction::SK_ASSIGN, w);
    else
        rivals::broadcast_faction(rivals::Stage::Civ, wire::rivals_faction::SK_ASSIGN, w);
    g_st.assign_tx++;
    g_last_assign_tx = now;
    g_assign_dirty = false;
}

static void host_send_npc_rels(double now, int only_peer = -1)
{
    if (!is_host() || !active() || !C.sess)
        return;
    for (const auto& p : C.sess->players()) {
        if (p.id == 0 || p.id >= kMaxPlayers)
            continue;
        if (only_peer >= 1 && p.id != (uint8_t)only_peer)
            continue;
        net::Writer w;
        write_npc_rels(w, p.id, g_npc_rels);
        if (rivals::send_faction_to(p.id, rivals::Stage::Civ, wire::rivals_faction::SK_NPC_RELS, w))
            g_st.npcrel_tx++;
    }
    g_last_npcrel_tx = now;
    g_npcrel_dirty = false;
}

static void host_apply_guest_species(uint8_t pid, void* gc)
{
    if (!gc || !a_CivSetSpeciesKey || g_species_applied.count(pid))
        return;
    creations::Key sk = species::key(pid, species::Stage::Civ);
    if (!sk.instance)
        return;
    skg::ResourceKey rk{sk.instance, sk.type, sk.group};
    thiscall<void>(a_CivSetSpeciesKey, gc, &rk);
    g_species_applied.insert(pid);
    if (H && M)
        H->log(M, SK_INFO, "civ_rivals: assigned player %u species %08X to nation pol=0x%08X", (unsigned)pid, sk.instance,
               (unsigned)civ::pol_of(gc));
}

static void host_ensure_assignments(double now)
{
    void* host_civ = orig_player_civ();
    if (!host_civ || !C.sess)
        return;
    auto host_cities = civ::vec_of((uint8_t*)host_civ + civ::kCivCities);
    if (host_cities.empty())
        return;

    uint32_t host_pol = civ::pol_of(host_civ);
    Vec3 host_cap = to_vec3(civ::city_pos(host_cities[0]));
    if (!g_nations[0].assigned || g_nations[0].pol_id != host_pol) {
        g_nations[0] = {0, host_pol, host_cap, F<float>(host_civ, kCivWealth), true};
        g_assign_dirty = true;
    } else {
        g_nations[0].wealth = F<float>(host_civ, kCivWealth);
    }

    // Collect already-taken pols
    std::vector<uint32_t> taken = {host_pol};
    for (int i = 1; i < kMaxPlayers; i++) {
        if (g_nations[i].assigned) {
            if (!civ_by_pol(g_nations[i].pol_id)) {
                g_nations[i].assigned = false;
                g_nations[i].pol_id = 0;
                g_assign_dirty = true;
            } else {
                taken.push_back(g_nations[i].pol_id);
                host_apply_guest_species((uint8_t)i, civ_by_pol(g_nations[i].pol_id));
            }
        }
    }

    // Build candidate list from free NPC civilizations with at least 1 city
    std::vector<BaseCandidate> cands;
    for (void* c : all_civs()) {
        if (!c || c == host_civ)
            continue;
        uint32_t pol = civ::pol_of(c);
        if (!pol || pol == 0xFFFFFFFFu)
            continue;
        if (std::find(taken.begin(), taken.end(), pol) != taken.end())
            continue;
        auto cs = civ::vec_of((uint8_t*)c + civ::kCivCities);
        if (cs.empty())
            continue;
        cands.push_back({pol, to_vec3(civ::city_pos(cs[0])), (int)cs.size()});
    }

    bool need_npc_spawn = false;
    for (const auto& pl : C.sess->players()) {
        if (pl.id == 0 || pl.id >= kMaxPlayers || g_nations[pl.id].assigned)
            continue;
        uint32_t pick = select_base_nation(host_cap, cands, rivals::base_distance(), taken);
        if (!pick) {
            need_npc_spawn = true;
            continue;
        }
        void* gc = civ_by_pol(pick);
        if (!gc || !skg::readable(gc, kCivExpansionBudget + 4))
            continue;
        auto gcities = civ::vec_of((uint8_t*)gc + civ::kCivCities);
        if (gcities.empty())
            continue;
        Vec3 cap = to_vec3(civ::city_pos(gcities[0]));

        // R2 takeover: fold NPC domestic/expansion budgets back into mWealth and enforce baseline floor.
        // Do NOT set +0x89 (mIsPlayer) = 1 on guest nations on the host: cRelationshipManager::CalculateRelationshipAbsolute
        // (0x00D05AF4) swaps (polA, polB) and recurses into CalculateRelationship whenever civA->mIsPlayer != 0, so
        // having two civilizations with +0x89 == 1 causes infinite recursion / stack overflow. Guest AI is disabled
        // via h_CivAI + h_CreateBuilding + h_BuyTurret + h_SpawnVeh + h_WealthToBudgets instead; on the guest,
        // apply_guest_perspective() sets ONLY the guest's own civ to +0x89 = 1 and clears the host's civ to 0.
        float w = F<float>(gc, kCivWealth) + F<float>(gc, kCivDomesticBudget) + F<float>(gc, kCivExpansionBudget);
        if (!std::isfinite(w) || w < kStartingWealthFloor)
            w = kStartingWealthFloor;
        F<float>(gc, kCivWealth) = w;
        F<float>(gc, kCivDomesticBudget) = 0.0f;
        F<float>(gc, kCivExpansionBudget) = 0.0f;
        F<uint8_t>(gc, kCivIsPlayer) = 0;

        g_nations[pl.id] = {pl.id, pick, cap, w, true};
        taken.push_back(pick);
        g_npc_rels.remove_npc(pick); // no longer an NPC nation
        host_apply_guest_species(pl.id, gc);
        g_st.takeovers++;
        g_assign_dirty = true;
        g_npcrel_dirty = true;

        if (H && M)
            H->log(M, SK_INFO,
                   "civ_rivals: player %u (%s) took over NPC nation pol=0x%08X (dist=%s, d=%.1f m, wealth=%.0f, BUG-1549)",
                   (unsigned)pl.id, pl.name.c_str(), (unsigned)pick, rivals::distance_name(rivals::base_distance()),
                   dist3(host_cap, cap), w);
        if (C.notice) {
            char b[160];
            snprintf(b, sizeof b, "%s took command of rival nation 0x%08X", pl.name.c_str(), (unsigned)pick);
            C.notice(b);
        }
    }

    // If a guest needs a nation and no free NPC nation exists yet, spawn one instantly
    if (need_npc_spawn && now - g_last_spawn_req >= 3.0 && civ::a_ExpansionGet && civ::o_SpawnNextCity) {
        g_last_spawn_req = now;
        if (void* ex = skg::cdecl_call<void*>(civ::a_ExpansionGet)) {
            civ::o_SpawnNextCity(ex, 1, nullptr);
            if (H && M)
                H->log(M, SK_INFO, "civ_rivals: spawned next NPC city site for unassigned guest");
        }
    }
}

static void host_sync_native_diplomacy(double now)
{
    if (now - g_last_rel_sync < 1.0)
        return;
    g_last_rel_sync = now;

    // 1. Keep native player-vs-player relations in cRelationshipManager aligned with rivals::relation
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!g_nations[i].assigned)
            continue;
        for (int j = i + 1; j < kMaxPlayers; j++) {
            if (!g_nations[j].assigned)
                continue;
            rivals::Relation r = rivals::relation((uint8_t)i, (uint8_t)j);
            float target = (r == rivals::Relation::Ally) ? 8.0f : (r == rivals::Relation::War ? -8.0f : 0.0f);
            rel_set(g_nations[i].pol_id, g_nations[j].pol_id, target);
            rel_set(g_nations[j].pol_id, g_nations[i].pol_id, target);
        }
    }

    // 2. Refresh remaining NPC nations in g_npc_rels
    std::set<uint32_t> live_npcs;
    for (void* c : all_civs()) {
        if (!c)
            continue;
        uint32_t pol = civ::pol_of(c);
        if (!pol || pol == 0xFFFFFFFFu || player_of_pol(pol) >= 0)
            continue;
        auto cs = civ::vec_of((uint8_t*)c + civ::kCivCities);
        if (cs.empty())
            continue;
        live_npcs.insert(pol);
        if (!g_npc_rels.has_npc(pol)) {
            float init_rel = g_nations[0].assigned ? rel_get(pol, g_nations[0].pol_id) : 0.0f;
            g_npc_rels.ensure_npc(pol, init_rel);
            for (int p = 0; p < kMaxPlayers; p++) {
                if (g_nations[p].assigned)
                    g_npc_rels.set(pol, (uint8_t)p, rel_get(pol, g_nations[p].pol_id));
            }
            g_npcrel_dirty = true;
        } else {
            // Keep host's native cRelationshipManager aligned with our authoritative per-player table
            for (int p = 0; p < kMaxPlayers; p++) {
                if (!g_nations[p].assigned)
                    continue;
                rel_set(pol, g_nations[p].pol_id, g_npc_rels.get(pol, (uint8_t)p));
            }
        }
    }
    // Prune conquered/dead NPC nations
    std::vector<uint32_t> gone;
    for (const auto& [pol, arr] : g_npc_rels.scores) {
        (void)arr;
        if (!live_npcs.count(pol))
            gone.push_back(pol);
    }
    for (uint32_t pol : gone) {
        g_npc_rels.remove_npc(pol);
        g_npcrel_dirty = true;
    }
}

static void host_eval_stage_progress(double now)
{
    if (now - g_last_win_eval < 1.0 || !C.sess)
        return;
    g_last_win_eval = now;

    CityCounts cc{};
    for (const auto& pl : C.sess->players()) {
        if (pl.id < kMaxPlayers && g_nations[pl.id].assigned) {
            cc.present[pl.id] = true;
            cc.wealth[pl.id] = g_nations[pl.id].wealth;
        }
    }
    int assigned_count = 0;
    for (int i = 0; i < kMaxPlayers; i++)
        assigned_count += cc.present[i] ? 1 : 0;
    if (assigned_count < 2)
        return; // wait until at least host + 1 guest have assigned nations

    for (void* city : civ::all_cities()) {
        if (!city)
            continue;
        int owner = player_of_pol(civ::pol_of(city));
        if (owner >= 0 && owner < kMaxPlayers) {
            cc.total_cities[owner]++;
            if (!is_player_capital(city)) {
                cc.non_cap_cities[owner]++;
                cc.total_contestable++;
            }
        } else {
            cc.free_npc_cities++;
            cc.total_contestable++;
        }
    }

    int need = cities_needed_to_win(cc.total_contestable);
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!cc.present[i])
            continue;
        rivals::PlayerProgress pr;
        pr.player_id = (uint8_t)i;
        pr.present = true;
        pr.primary = (float)cc.non_cap_cities[i];
        pr.secondary = (float)need;
        pr.stat_a = (uint16_t)cc.total_cities[i];
        pr.stat_b = g_npc_rels.allied_count((uint8_t)i);
        pr.extra = cc.wealth[i];
        rivals::set_player_progress((uint8_t)i, pr);
    }

    auto ev = evaluate_stage_win(cc);
    if (!ev.has_winner)
        return;

    const auto& race = rivals::race_state();
    // Report primary winner first, then any additional qualified players
    if (race.players[ev.winner].finish_rank == 0)
        rivals::report_stage_complete(ev.winner);
    for (int i = 0; i < kMaxPlayers; i++) {
        if (ev.qualified[i] && i != ev.winner && race.players[i].finish_rank == 0)
            rivals::report_stage_complete((uint8_t)i);
    }
}

void host_sync_owners()
{
    if (!active() || !is_host())
        return;
    civ::g_city_owner.clear();
    for (void* city : civ::all_cities()) {
        if (!city)
            continue;
        int owner = player_of_pol(civ::pol_of(city));
        if (owner >= 0 && owner < kMaxPlayers)
            civ::g_city_owner[city] = (uint8_t)owner;
    }
}

bool host_charge_player(uint8_t pid, float amount)
{
    if (!active() || !is_host() || !std::isfinite(amount) || amount <= 0.0f)
        return false;
    void* c = civ_of_player(pid);
    if (!c || !skg::readable(c, kCivWealth + 4))
        return false;
    float& w = F<float>(c, kCivWealth);
    if (w < amount)
        return false;
    w -= amount;
    g_nations[pid].wealth = w;
    g_assign_dirty = true;
    return true;
}

void host_credit_player(uint8_t pid, float amount)
{
    if (!active() || !is_host() || !std::isfinite(amount) || amount <= 0.0f)
        return;
    void* c = civ_of_player(pid);
    if (!c || !skg::readable(c, kCivWealth + 4))
        return;
    float& w = F<float>(c, kCivWealth);
    w = std::min(999999.0f, w + amount);
    g_nations[pid].wealth = w;
    g_assign_dirty = true;
}

void host_mark_assign_dirty() { g_assign_dirty = true; }

// ---- R7 & War/Neutral/Ally enforcement ----
CombatGate damage_gate(uint8_t attacker_pid, uint8_t victim_pid, bool is_capital, int dmg_type)
{
    if (!active())
        return CombatGate::Pass;
    bool war = (attacker_pid < kMaxPlayers && victim_pid < kMaxPlayers) ? rivals::at_war(attacker_pid, victim_pid) : false;
    CombatGate g = pvp_damage_gate(attacker_pid, victim_pid, war, is_capital, dmg_type);
    if (g == CombatGate::Allow)
        g_st.pvp_allowed++;
    else if (g == CombatGate::Block) {
        g_st.pvp_blocked++;
        if (is_capital && (dmg_type == 0xC || dmg_type == 0xD))
            g_st.sw_blocked++;
    }
    return g;
}

bool block_attack_order(uint8_t actor_pid, void* target)
{
    if (!active() || !target || actor_pid >= kMaxPlayers)
        return false;
    int vic_pid = player_of_pol(civ::pol_of(target));
    if (vic_pid < 0 || vic_pid >= kMaxPlayers || vic_pid == (int)actor_pid)
        return false;
    if (!rivals::at_war(actor_pid, (uint8_t)vic_pid)) {
        g_st.pvp_blocked++;
        return true;
    }
    return false;
}

static void clear_pending_capture_flags(void* city)
{
    // Clear cCity +0x2E0..+0x2E6 capture flags (docs/re/civ.md#civ-wsync-owners)
    if (city && skg::readable(city, 0x2E8))
        memset((uint8_t*)city + 0x2E0, 0, 7);
    if (city && civ::a_SetCapture)
        thiscall<void>(civ::a_SetCapture, city, 0xFFFFFFFFu, 0.0f);
}

bool block_capture(void* city, uint32_t new_pol)
{
    if (!active() || !city)
        return false;
    int att_pid = player_of_pol(new_pol);
    int def_pid = player_of_pol(civ::pol_of(city));
    bool is_cap = is_player_capital(city);
    bool war = (att_pid >= 0 && def_pid >= 0) ? rivals::at_war((uint8_t)att_pid, (uint8_t)def_pid) : false;
    uint8_t a = att_pid >= 0 ? (uint8_t)att_pid : 0xFF;
    uint8_t d = def_pid >= 0 ? (uint8_t)def_pid : 0xFF;
    if (city_capture_blocked(a, d, war, is_cap)) {
        clear_pending_capture_flags(city);
        g_st.cap_blocked++;
        if (H && M)
            H->log(M, SK_INFO, "civ_rivals: blocked capture of city %p (capital=%d att=%d def=%d war=%d, BUG-1549)",
                   city, (int)is_cap, att_pid, def_pid, (int)war);
        return true;
    }
    return false;
}

bool block_capture_damage(void* city, uint8_t attacker_pid, uint32_t attacker_pol)
{
    if (!active() || !city)
        return false;
    int ap = attacker_pid < kMaxPlayers ? (int)attacker_pid : player_of_pol(attacker_pol);
    int dp = player_of_pol(civ::pol_of(city));
    bool is_cap = is_player_capital(city);
    bool war = (ap >= 0 && dp >= 0) ? rivals::at_war((uint8_t)ap, (uint8_t)dp) : false;
    if (city_capture_blocked(ap >= 0 ? (uint8_t)ap : 0xFF, dp >= 0 ? (uint8_t)dp : 0xFF, war, is_cap)) {
        if (is_cap && civ::a_SetCapture)
            thiscall<void>(civ::a_SetCapture, city, 0xFFFFFFFFu, 0.0f);
        g_st.cap_blocked++;
        return true;
    }
    return false;
}

bool block_convert(void* city, uint32_t converter_pol)
{
    if (!active() || !city)
        return false;
    int ap = player_of_pol(converter_pol);
    int dp = player_of_pol(civ::pol_of(city));
    bool is_cap = is_player_capital(city);
    bool war = (ap >= 0 && dp >= 0) ? rivals::at_war((uint8_t)ap, (uint8_t)dp) : false;
    if (city_capture_blocked(ap >= 0 ? (uint8_t)ap : 0xFF, dp >= 0 ? (uint8_t)dp : 0xFF, war, is_cap)) {
        g_st.conv_blocked++;
        return true;
    }
    return false;
}

bool block_superweapon(uint8_t attacker_pid, int sw_type, const float* target)
{
    if (!active())
        return false;
    bool has_t = target != nullptr;
    Vec3 pt = has_t ? Vec3{target[0], target[1], target[2]} : Vec3{};
    uint8_t prot_owner = 255;
    if (superweapon_blocked_by_capital(attacker_pid, sw_type, has_t, pt, g_nations, &prot_owner)) {
        g_st.sw_blocked++;
        if (C.notice)
            C.notice("Superweapons cannot target a rival's capital!");
        if (H && M)
            H->log(M, SK_INFO, "civ_rivals: blocked superweapon type=%d by player %u targeting player %u's capital (BUG-1549)",
                   sw_type, (unsigned)attacker_pid, (unsigned)prot_owner);
        return true;
    }
    return false;
}

void on_city_captured(void* city, uint32_t new_pol)
{
    if (!active() || !is_host() || !city)
        return;
    int old_pid = -1;
    if (auto it = civ::g_city_owner.find(city); it != civ::g_city_owner.end() && it->second < kMaxPlayers)
        old_pid = it->second;
    int new_pid = player_of_pol(new_pol);
    if (new_pid >= 0 && new_pid < kMaxPlayers) {
        civ::g_city_owner[city] = (uint8_t)new_pid;
        if (old_pid >= 0 && old_pid != new_pid) {
            g_st.war_captures++;
            if (g_war_captured_from.insert({(uint8_t)new_pid, (uint8_t)old_pid}).second)
                rivals::add_chronicle_points((uint8_t)new_pid, rivals::kPtsNpcFaction, "war city capture");
        } else if (old_pid < 0) {
            rivals::grant_influence((uint8_t)new_pid, kCaptureInfluenceGain, "city subjugated");
            rivals::add_chronicle_points((uint8_t)new_pid, rivals::kPtsNpcFaction, "npc city subjugated");
        }
        if (C.feed && C.sess)
            C.feed(C.sess->name_of((uint8_t)new_pid) + " captured a city");
    } else {
        civ::g_city_owner.erase(city);
    }
    g_assign_dirty = true;
}

void on_allied_trade(uint8_t a, uint8_t b, int gain)
{
    if (!active() || !is_host() || gain <= 0 || !rivals::allied(a, b))
        return;
    float half = (float)gain * 0.5f;
    host_credit_player(a, half);
    host_credit_player(b, half);
    rivals::grant_influence(a, kTradeInfluenceGain, "allied trade");
    rivals::grant_influence(b, kTradeInfluenceGain, "allied trade");
    g_st.allied_trades++;
    g_st.inf_trade += 2;
}

std::vector<void*> all_player_vehicles()
{
    std::vector<void*> out;
    if (!active() || !civ::nm())
        return out;
    std::unordered_set<void*> civ_vehs;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!g_nations[i].assigned)
            continue;
        if (void* c = civ_by_pol(g_nations[i].pol_id)) {
            for (void* v : civ::vec_of((uint8_t*)c + 0xB0))
                civ_vehs.insert(v);
        }
    }
    for (void* v : civ::all_vehicles())
        if (civ_vehs.count(v))
            out.push_back(v);
    return out;
}

// ---- Alliance gift transfer (SK_GIFT) ----
static std::string host_apply_gift(uint8_t from, const GiftMsg& g)
{
    if (!active() || !is_host())
        return "not host in Civ Rivals";
    void* src = civ_of_player(from);
    void* dst = civ_of_player(g.target_pid);
    if (!src || !dst) {
        g_st.gifts_rejected++;
        return "player nation not assigned";
    }
    float sw = F<float>(src, kCivWealth);
    if (!can_send_gift(from, g.target_pid, rivals::allied(from, g.target_pid), g.kind, g.amount, sw)) {
        g_st.gifts_rejected++;
        return !rivals::allied(from, g.target_pid) ? "not allied" : "insufficient funds or invalid amount";
    }
    F<float>(src, kCivWealth) = sw - g.amount;
    F<float>(dst, kCivWealth) = std::min(999999.0f, F<float>(dst, kCivWealth) + g.amount);
    g_nations[from].wealth = F<float>(src, kCivWealth);
    g_nations[g.target_pid].wealth = F<float>(dst, kCivWealth);
    g_st.gifts_done++;
    if (g.kind == GiftKind::Spice)
        g_st.spice_gifts++;
    rivals::grant_influence(from, 5, "alliance gift");
    host_broadcast_assign(now_s());
    if (C.notice && C.sess) {
        char b[160];
        snprintf(b, sizeof b, "%s sent %.0f %s to ally %s", C.sess->name_of(from).c_str(), g.amount,
                 g.kind == GiftKind::Spice ? "spice value" : "Sporebucks", C.sess->name_of(g.target_pid).c_str());
        C.notice(b);
    }
    return "";
}

static std::string request_gift(uint8_t target_pid, GiftKind kind, float amount)
{
    if (!active())
        return "Civ Rivals not active";
    GiftMsg g{target_pid, kind, amount};
    if (is_host())
        return host_apply_gift(0, g);
    if (is_guest()) {
        net::Writer w;
        write_gift(w, g);
        if (!rivals::send_faction_to(0, rivals::Stage::Civ, wire::rivals_faction::SK_GIFT, w))
            return "host does not support MSG_RIVALS_FACTION";
        g_st.gifts_sent++;
        return "";
    }
    return "not connected";
}

// ---- MSG_RIVALS_FACTION (Stage::Civ) handler ----
static void on_faction_msg(uint8_t from, uint8_t subkind, net::Reader& r)
{
    if (!g_fix_rivals_civ)
        return;
    using namespace wire::rivals_faction;
    if (subkind == SK_ASSIGN) {
        if (!is_guest() || from != 0)
            return;
        std::array<PlayerNation, kMaxPlayers> parsed{};
        if (!read_assign(r, parsed))
            return;
        g_nations = parsed;
        g_st.assign_rx++;
        apply_guest_perspective();
        return;
    }
    if (subkind == SK_NPC_RELS) {
        if (!is_guest() || from != 0)
            return;
        uint8_t tpid = 255;
        std::vector<std::pair<uint32_t, float>> rows;
        if (!read_npc_rels(r, tpid, rows) || tpid != my_id())
            return;
        g_guest_npc_rels.clear();
        uint32_t my_pol = guest_my_pol();
        for (const auto& [npc_pol, score] : rows) {
            g_guest_npc_rels[npc_pol] = score;
            if (my_pol)
                rel_set(npc_pol, my_pol, score);
        }
        g_st.npcrel_rx++;
        return;
    }
    if (subkind == SK_GIFT) {
        if (!is_host() || from == 0 || from >= kMaxPlayers)
            return;
        GiftMsg g;
        if (!read_gift(r, g))
            return;
        host_apply_gift(from, g);
        return;
    }
    if (subkind == SK_DRAFT_APPLY) {
        if (!is_guest() || from != 0)
            return;
        DraftApplyMsg dm;
        if (!read_draft_apply(r, dm) || dm.target_pid != my_id())
            return;
        g_draft_applied_ids[dm.target_pid].push_back(dm.design_id);
        g_st.draft_applied++;
        if (void* my_civ = civ_of_player(my_id()); my_civ && a_CivSetDesign && dm.key[0])
            thiscall<void>(a_CivSetDesign, my_civ, dm.design_id, dm.key);
        if (H && M)
            H->log(M, SK_INFO, "civ_rivals: applied drafted design 0x%08X (key %08X) to player %u", (unsigned)dm.design_id,
                   (unsigned)dm.key[0], (unsigned)dm.target_pid);
        return;
    }
}

// ---- R4: NPC Influence callback ----
static bool on_npc_influence(rivals::Stage stage, rivals::InfluenceAction action, uint8_t actor, uint32_t npc_id,
                             uint8_t rival_id)
{
    if (stage != rivals::Stage::Civ || !active() || !is_host())
        return false;
    if (!g_npc_rels.has_npc(npc_id))
        return false;
    if (action == rivals::InfluenceAction::TurnAgainst) {
        if (rival_id >= kMaxPlayers || rival_id == actor || !g_nations[rival_id].assigned)
            return false;
        float nv = 0.0f;
        if (!g_npc_rels.apply_turn_against(npc_id, rival_id, kNpcRelStep, &nv))
            return false;
        rel_set(npc_id, g_nations[rival_id].pol_id, nv);
        g_st.inf_turned++;
        g_npcrel_dirty = true;
        host_send_npc_rels(now_s());
        return true;
    }
    if (action == rivals::InfluenceAction::Protect) {
        g_st.inf_protected++;
        return true;
    }
    if (action == rivals::InfluenceAction::SmoothOver) {
        if (actor >= kMaxPlayers || !g_nations[actor].assigned)
            return false;
        float nv = 0.0f;
        if (!g_npc_rels.apply_smooth_over(npc_id, actor, kNpcRelStep, &nv))
            return false;
        rel_set(npc_id, g_nations[actor].pol_id, nv);
        g_st.inf_smoothed++;
        g_npcrel_dirty = true;
        host_send_npc_rels(now_s());
        return true;
    }
    return false;
}

// ---- StageHooks: buy influence, draft pick, stage race end, F9 UI rows ----
static bool hook_buy_influence(uint8_t actor, int32_t price, int32_t /*gain*/)
{
    if (!active() || !is_host())
        return true; // fall back to core when Civ Rivals is not active
    float cost = influence_buy_civ_cost(price);
    if (!host_charge_player(actor, cost))
        return false;
    g_st.inf_bought++;
    host_broadcast_assign(now_s());
    return true;
}

static void hook_draft_pick(uint8_t actor, uint32_t item_id, uint8_t /*kind*/, const std::string& label)
{
    if (!active() || !is_host())
        return;
    uint32_t key[3] = {0, 0, 0};
    if (g_draft_winner < kMaxPlayers) {
        if (void* wc = civ_of_player(g_draft_winner); wc && civ::a_DesignKey) {
            if (uint32_t* k = thiscall<uint32_t*>(civ::a_DesignKey, wc, item_id)) {
                key[0] = k[0];
                key[1] = k[1];
                key[2] = k[2];
            }
        }
    }
    // Apply on host to actor's nation
    if (void* ac = civ_of_player(actor); ac && a_CivSetDesign && key[0])
        thiscall<void>(a_CivSetDesign, ac, item_id, key);
    g_draft_applied_ids[actor].push_back(item_id);
    g_st.draft_applied++;
    // Forward to guest if actor is a guest
    if (actor >= 1 && actor < kMaxPlayers) {
        DraftApplyMsg dm{actor, item_id, {key[0], key[1], key[2]}};
        net::Writer w;
        write_draft_apply(w, dm);
        rivals::send_faction_to(actor, rivals::Stage::Civ, wire::rivals_faction::SK_DRAFT_APPLY, w);
    }
    if (H && M)
        H->log(M, SK_INFO, "civ_rivals: draft pick '%s' (0x%08X, key=%08X) copied to player %u", label.c_str(),
               (unsigned)item_id, (unsigned)key[0], (unsigned)actor);
}

static void hook_stage_race_end(uint8_t winner)
{
    if (!active() || !is_host() || g_race_ended)
        return;
    g_race_ended = true;
    g_draft_winner = winner;

    // Collect available designs from winner's civilization for Civ Draft (11.6)
    std::vector<uint32_t> avail;
    if (void* wc = civ_of_player(winner); wc && civ::a_DesignKey) {
        for (const auto& sp : civ_draft_specs()) {
            uint32_t* k = thiscall<uint32_t*>(civ::a_DesignKey, wc, sp.design_id);
            if (k && k[0] && k[0] != 0xFFFFFFFFu)
                avail.push_back(sp.design_id);
        }
    }
    auto items = build_civ_draft_items(avail);
    if (rivals::start_draft(winner, items))
        g_draft_started = true;

    // R3: enable Civ -> Space advance UI on host even if host did not win the native 10-city check
    g_transition_ready = true;
    if (a_GameCivGet && a_GameCivShowWon) {
        if (void* gc = skg::cdecl_call<void*>(a_GameCivGet))
            thiscall<void>(a_GameCivShowWon, gc);
    }
    if (H && M)
        H->log(M, SK_INFO, "civ_rivals: stage race ended (winner=%u, draft_items=%u, transition_ready=1, BUG-1549)",
               (unsigned)winner, (unsigned)items.size());
}

static std::vector<rivals::UiRow> hook_extra_ui_rows()
{
    std::vector<rivals::UiRow> rows;
    if (!active() || !C.sess)
        return rows;
    uint32_t kGold = 0xFFF2D479, kGreen = 0xFF7BE882, kDim = 0xFFB0BEC8;
    char buf[192];
    rows.push_back({"Civ Rivals: Nations & Capitals", "", nullptr, kGold});
    uint8_t me = my_id();
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!g_nations[i].assigned)
            continue;
        std::string nm = C.sess->name_of((uint8_t)i);
        bool is_ally = (i != me) && rivals::allied(me, (uint8_t)i);
        snprintf(buf, sizeof buf, "  %s: pol=0x%08X $%.0f (capital protected)", nm.c_str(),
                 (unsigned)g_nations[i].pol_id, g_nations[i].wealth);
        uint8_t tid = (uint8_t)i;
        rows.push_back({buf, is_ally ? "Gift $500" : "",
                        is_ally ? std::function<void()>([tid] { request_gift(tid, GiftKind::Money, 500.0f); }) : nullptr,
                        i == me ? kGreen : 0});
    }
    // NPC nation relations for local player
    rows.push_back({"Civ Rivals: NPC Nations", "", nullptr, kGold});
    if (is_host()) {
        for (const auto& [pol, arr] : g_npc_rels.scores) {
            snprintf(buf, sizeof buf, "  NPC 0x%08X: rel=%.1f (cat=%d)", (unsigned)pol, arr[0], rel_category(arr[0]));
            uint32_t np = pol;
            rows.push_back({buf, "Smooth", [np] { rivals::request_influence(rivals::InfluenceAction::SmoothOver, np, 255); }, kDim});
        }
    } else {
        for (const auto& [pol, sc] : g_guest_npc_rels) {
            snprintf(buf, sizeof buf, "  NPC 0x%08X: rel=%.1f (cat=%d)", (unsigned)pol, sc, rel_category(sc));
            uint32_t np = pol;
            rows.push_back({buf, "Smooth", [np] { rivals::request_influence(rivals::InfluenceAction::SmoothOver, np, 255); }, kDim});
        }
    }
    return rows;
}

// ---- Init & Tick ----
void init()
{
    if (g_inited || !H || !M)
        return;
    g_inited = true;

    a_GetPlayerCiv = H->sym("Simulator::cGameNounManager::GetPlayerCivilization");
    a_GetPlayerPol = H->sym("SporeCoop::GameNounManager_GetPlayerPoliticalID");
    a_CityIsPlayerOwned = H->sym("SporeCoop::City_IsPlayerOwned");
    a_CitySetPlayerOwned = H->sym("SporeCoop::City_SetIsPlayerOwned");
    a_CivAI = H->sym("SporeCoop::Civilization_AI");
    a_CivIncomeTick = H->sym("SporeCoop::Civ_IncomeTick");
    a_CivSetSpeciesKey = H->sym("SporeCoop::Civ_SetSpeciesKey");
    a_CivSetDesign = H->sym("SporeCoop::Civ_SetDesign");
    a_CanTargetSW = H->sym("SporeCoop::City_CanTargetSuperweapon");
    a_LaunchGlobalSW = H->sym("SporeCoop::City_LaunchGlobalSuperweapon");
    a_TerrainCursorGet = H->sym("SporeCoop::TerrainCursor_Get");
    a_TerrainCursorSetObj = H->sym("SporeCoop::TerrainCursor_SetObjects");
    a_TradeDelivery = H->sym("SporeCoop::City_TradeDelivery");
    a_RelMgrGet = H->sym("Simulator::cRelationshipManager::Get");
    a_RelCalc = H->sym("Simulator::cRelationshipManager::CalculateRelationship");
    a_RelCalcAbs = H->sym("Simulator::cRelationshipManager::CalculateRelationshipAbsolute");
    a_RelApply = H->sym("Simulator::cRelationshipManager::ApplyRelationship");
    a_GameCivGet = H->sym("SporeCoop::GameCiv_Get");
    a_GameCivShowWon = H->sym("SporeCoop::GameCiv_ShowPlanetWon");

    if (a_GetPlayerCiv)
        H->hook_create(M, (void*)a_GetPlayerCiv, (void*)h_GetPlayerCiv, (void**)&o_GetPlayerCiv);
    if (a_GetPlayerPol)
        H->hook_create(M, (void*)a_GetPlayerPol, (void*)h_GetPlayerPol, (void**)&o_GetPlayerPol);
    if (a_CityIsPlayerOwned)
        H->hook_create(M, (void*)a_CityIsPlayerOwned, (void*)h_CityIsPlayer, (void**)&o_CityIsPlayer);
    if (a_CivAI)
        H->hook_create(M, (void*)a_CivAI, (void*)h_CivAI, (void**)&o_CivAI);
    if (a_CanTargetSW)
        H->hook_create(M, (void*)a_CanTargetSW, (void*)h_CanTargetSW, (void**)&o_CanTargetSW);
    if (a_LaunchGlobalSW)
        H->hook_create(M, (void*)a_LaunchGlobalSW, (void*)h_LaunchGlobalSW, (void**)&o_LaunchGlobalSW);
    if (a_TradeDelivery)
        H->hook_create(M, (void*)a_TradeDelivery, (void*)h_TradeDelivery, (void**)&o_TradeDelivery);

    savestrip::add_save_hooks(on_save_start, on_save_end);

    rivals::register_faction_handler(rivals::Stage::Civ, on_faction_msg);
    rivals::register_npc_influence_callback(rivals::Stage::Civ, on_npc_influence);
    rivals::StageHooks hooks;
    hooks.on_buy_influence = hook_buy_influence;
    hooks.on_draft_pick = hook_draft_pick;
    hooks.on_stage_race_end = hook_stage_race_end;
    hooks.on_extra_ui_rows = hook_extra_ui_rows;
    rivals::register_stage_hooks(rivals::Stage::Civ, std::move(hooks));

    H->log(M, SK_INFO, "civ_rivals: initialized (rivals.civ=%d, hooks=%d/%d/%d/%d/%d/%d/%d)",
           (int)g_fix_rivals_civ, (int)(o_GetPlayerCiv != nullptr), (int)(o_GetPlayerPol != nullptr),
           (int)(o_CityIsPlayer != nullptr), (int)(o_CivAI != nullptr), (int)(o_CanTargetSW != nullptr),
           (int)(o_LaunchGlobalSW != nullptr), (int)(o_TradeDelivery != nullptr));
}

void tick(double now)
{
    if (!active()) {
        if (g_swap_applied)
            restore_perspective();
        return;
    }
    if (is_host()) {
        host_ensure_assignments(now);
        host_sync_native_diplomacy(now);
        host_eval_stage_progress(now);
        if (g_assign_dirty || now - g_last_assign_tx >= 2.0)
            host_broadcast_assign(now);
        if (g_npcrel_dirty || now - g_last_npcrel_tx >= 3.0)
            host_send_npc_rels(now);
    } else if (is_guest()) {
        apply_guest_perspective();
    }
}

// ---- Console diagnostics (`coop.civ rivals ...`) ----
std::string debug(const std::vector<std::string>& a)
{
    char b[512];
    if (!a.empty() && a[0] == "gift" && a.size() >= 4) {
        uint8_t to = (uint8_t)atoi(a[1].c_str());
        GiftKind k = (a[2] == "spice") ? GiftKind::Spice : GiftKind::Money;
        float amt = (float)atof(a[3].c_str());
        std::string err = request_gift(to, k, amt);
        return err.empty() ? "ok\n" : ("error: " + err + "\n");
    }
    if (!a.empty() && a[0] == "assign" && is_host()) {
        if (!active())
            return "error: Civ Rivals not active\n";
        host_ensure_assignments(now_s());
        host_broadcast_assign(now_s());
        return "ok\n";
    }
    if (!a.empty() && a[0] == "addmoney" && a.size() >= 3 && is_host()) {
        uint8_t pid = (uint8_t)atoi(a[1].c_str());
        float delta = (float)atof(a[2].c_str());
        void* c = civ_of_player(pid);
        if (!c || !skg::readable(c, kCivWealth + 4))
            return "error: no nation for player\n";
        float& w = F<float>(c, kCivWealth);
        w = std::max(0.0f, std::min(999999.0f, w + delta));
        g_nations[pid].wealth = w;
        host_broadcast_assign(now_s());
        snprintf(b, sizeof b, "player %u wealth=%.0f\n", (unsigned)pid, w);
        return b;
    }
    if (!a.empty() && a[0] == "capture_test" && a.size() >= 3 && is_host()) {
        int ci = atoi(a[1].c_str());
        uint8_t ap = (uint8_t)atoi(a[2].c_str());
        void* city = civ::host_city((size_t)ci);
        void* ac = civ_of_player(ap);
        if (!city || !ac)
            return "error: invalid city or player\n";
        uint32_t before = civ::pol_of(city);
        civ::h_Capture(city, civ::pol_of(ac), 0, 0, 0, 0);
        uint32_t after = civ::pol_of(city);
        // If capture succeeded synchronously or queued via ChangeOwner, ensure immediate ChangeOwner in instant test
        if (after == before && !block_capture(city, civ::pol_of(ac)) && civ::a_ChangeOwner) {
            thiscall<void>(civ::a_ChangeOwner, city, civ::pol_of(ac), 0, 0);
            after = civ::pol_of(city);
            if (after == civ::pol_of(ac))
                on_city_captured(city, after);
        }
        snprintf(b, sizeof b, "capture_test city=%d pol 0x%08X -> 0x%08X cap_blocked=%u\n", ci, (unsigned)before,
                 (unsigned)after, g_st.cap_blocked);
        return b;
    }
    if (!a.empty() && a[0] == "convert_test" && a.size() >= 3 && is_host()) {
        int ci = atoi(a[1].c_str());
        uint8_t ap = (uint8_t)atoi(a[2].c_str());
        void* city = civ::host_city((size_t)ci);
        void* ac = civ_of_player(ap);
        if (!city || !ac)
            return "error: invalid city or player\n";
        bool blocked = block_convert(city, civ::pol_of(ac));
        snprintf(b, sizeof b, "convert_test city=%d blocked=%d conv_blocked=%u\n", ci, (int)blocked, g_st.conv_blocked);
        return b;
    }
    if (!a.empty() && a[0] == "sw_test" && a.size() >= 3) {
        int sw_type = atoi(a[1].c_str());
        int ci = atoi(a[2].c_str());
        uint8_t ap = a.size() >= 4 ? (uint8_t)atoi(a[3].c_str()) : my_id();
        auto cs = civ::all_cities();
        void* city = (ci >= 0 && ci < (int)cs.size()) ? cs[ci] : nullptr;
        if (!city)
            return "error: invalid city\n";
        net::Vec3 p = civ::city_pos(city);
        float pt[3] = {p.x, p.y, p.z};
        bool blocked = block_superweapon(ap, sw_type, pt);
        bool can_t = a_CanTargetSW ? skg::cdecl_call<bool>(a_CanTargetSW, sw_type, pt) : !blocked;
        snprintf(b, sizeof b, "sw_test type=%d city=%d att=%u blocked=%d can_target=%d sw_blocked=%u\n", sw_type, ci,
                 (unsigned)ap, (int)blocked, (int)can_t, g_st.sw_blocked);
        return b;
    }
    if (!a.empty() && a[0] == "pvp_test" && a.size() >= 4 && is_host()) {
        uint16_t avid = (uint16_t)atoi(a[1].c_str());
        uint16_t vvid = (uint16_t)atoi(a[2].c_str());
        float dmg = (float)atof(a[3].c_str());
        auto av = civ::hv_by_id(avid);
        auto vv = civ::hv_by_id(vvid);
        if (!av || !vv || !civ::hv_live(av) || !civ::hv_live(vv))
            return "error: invalid vehicle id\n";
        float before = F<float>(vv.v, civ::kVHp);
        civ::ff_deal_vehicle_hit(vv.v, av.v, dmg);
        float after = F<float>(vv.v, civ::kVHp);
        snprintf(b, sizeof b, "pvp_test %u->%u hp %.1f -> %.1f allowed=%u blocked=%u\n", (unsigned)avid, (unsigned)vvid,
                 before, after, g_st.pvp_allowed, g_st.pvp_blocked);
        return b;
    }

    // Default: comprehensive status summary
    void* cur_pc = civ::nm() ? civ::player_civ() : nullptr;
    uint32_t cur_pol = cur_pc ? civ::pol_of(cur_pc) : 0;
    float cur_money = (cur_pc && civ::a_GetMoney) ? thiscall<float>(civ::a_GetMoney, cur_pc) : 0.0f;
    uint32_t api_pol = (civ::nm() && a_GetPlayerPol) ? thiscall<uint32_t>(a_GetPlayerPol, civ::nm()) : 0;
    int assigned_n = 0;
    for (int i = 0; i < kMaxPlayers; i++)
        assigned_n += g_nations[i].assigned ? 1 : 0;

    snprintf(b, sizeof b,
             "civ_rivals: fix=%d active=%d assigned=%d swap=%d player_pol=0x%08X api_pol=0x%08X player_money=%.0f "
             "takeovers=%u ai_blocked=%u swap_hits=%u cap_blocked=%u conv_blocked=%u sw_blocked=%u "
             "pvp_allowed=%u pvp_blocked=%u war_captures=%u gifts=%u/%u spice=%u "
             "inf_buy=%u inf_turn=%u inf_prot=%u inf_smooth=%u inf_trade=%u "
             "assign_tx=%u assign_rx=%u npcrel_tx=%u npcrel_rx=%u "
             "race_ended=%d draft_started=%d draft_applied=%u transition_ready=%d\n",
             (int)g_fix_rivals_civ, (int)active(), assigned_n, (int)g_swap_applied, (unsigned)cur_pol, (unsigned)api_pol,
             cur_money, g_st.takeovers, g_st.ai_blocked, g_st.swap_hits, g_st.cap_blocked, g_st.conv_blocked,
             g_st.sw_blocked, g_st.pvp_allowed, g_st.pvp_blocked, g_st.war_captures, g_st.gifts_done, g_st.gifts_rejected,
             g_st.spice_gifts, g_st.inf_bought, g_st.inf_turned, g_st.inf_protected, g_st.inf_smoothed, g_st.inf_trade,
             g_st.assign_tx, g_st.assign_rx, g_st.npcrel_tx, g_st.npcrel_rx, (int)g_race_ended, (int)g_draft_started,
             g_st.draft_applied, (int)g_transition_ready);
    std::string out = b;

    for (int i = 0; i < kMaxPlayers; i++) {
        if (!g_nations[i].assigned)
            continue;
        void* c = civ_by_pol(g_nations[i].pol_id);
        int nc = c ? (int)civ::vec_of((uint8_t*)c + civ::kCivCities).size() : 0;
        int nv = c ? (int)civ::vec_of((uint8_t*)c + 0xB0).size() : 0;
        float mw = (c && skg::readable(c, kCivWealth + 4)) ? F<float>(c, kCivWealth) : g_nations[i].wealth;
        snprintf(b, sizeof b, "  nation p=%d pol=0x%08X cities=%d vehs=%d wealth=%.0f cap=(%.1f %.1f %.1f)\n", i,
                 (unsigned)g_nations[i].pol_id, nc, nv, mw, g_nations[i].cap_pos.x, g_nations[i].cap_pos.y,
                 g_nations[i].cap_pos.z);
        out += b;
    }
    if (is_host()) {
        for (const auto& [pol, arr] : g_npc_rels.scores) {
            snprintf(b, sizeof b, "  npcrel pol=0x%08X p0=%.2f p1=%.2f p2=%.2f p3=%.2f\n", (unsigned)pol, arr[0], arr[1],
                     arr[2], arr[3]);
            out += b;
        }
    } else {
        uint32_t mp = guest_my_pol();
        for (const auto& [pol, sc] : g_guest_npc_rels) {
            float live = mp ? rel_get(pol, mp) : 0.0f;
            snprintf(b, sizeof b, "  npcrel pol=0x%08X synced=%.2f live=%.2f\n", (unsigned)pol, sc, live);
            out += b;
        }
    }
    const auto& dr = rivals::draft_state();
    if (dr.active && !dr.items.empty()) {
        out += "  draft_items:";
        for (const auto& it : dr.items) {
            snprintf(b, sizeof b, " 0x%08X", (unsigned)it.id);
            out += b;
        }
        out += "\n";
    }
    return out;
}

} // namespace civ_rivals
