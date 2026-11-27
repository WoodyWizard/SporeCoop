// Space Rivals runtime module (WP-R6 of 0.8.0, BUG-1550, docs/11-rivals.md sections 11.3.5, 11.4, 11.7, 11.9).
// Implements R5 guest homeworld emigration, R7 homeworld inviolability (Planet Buster + capture protection),
// unified rivals.h integration, per-player Space NPC empire influence, Race stats, and Chronicle Galaxy Core finale.
#include "space_rivals.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "call_guard.h"
#include "fixes.h"
#include "game.h"
#include "lifecycle.h"
#include "modes.h"
#include "own_world.h"
#include "player_life.h"
#include "rivals.h"
#include "space_ext.h"
#include "space_logic.h"
#include "space_map.h"
#include "space_rivals_logic.h"
#include "spacecoop.h"
#include "sporekit_game.hpp"
#include "store.h"
#include "wire/rivals_faction.h"
#include "world.h"

using skg::cdecl_call;
using skg::thiscall;
using skg::vcall;

namespace spacerivals {

static const SkHost* H = nullptr;
static SkMod* M = nullptr;

// Master switch for WP-R6 Space Rivals (BUG-1550).
// Off (SPORE_FIX_OFF=rivals.space): disables R5 emigration, R7 homeworld protection, Space NPC influence,
// Space race stats reporting, and Chronicle Galaxy Core finale bonus/summary.
static bool g_space_rivals_fix = true;
COOP_FIX("rivals.space", "BUG-1550", g_space_rivals_fix,
         "space rivals: guest homeworld emigration, inviolable homeworlds, NPC empire influence, race stats and core finale");

// BUG-1567: relinquishing the host's original home star previously cleared only mEmpireID (+0x54 = 0xFFFFFFFF)
// while leaving star mTechLevel (+0x4C) and planet mTechLevel (+0x194) at 5 and keeping the star in SpaceNearStars.
// When space.allies is off (Rivals preset) or before mirror apply, Galaxy hover/click faults at 0x0106CA9F/0x00FDC5FD
// and Jump -> EnterSolarSystem crashes at 0x00BB0F22 (GetEmpireForStar -> AddStarOwnership -> UpdateAndGetColor).
static bool g_relinquish_clean_fix = true;
COOP_FIX("rivals.space_relinquish_clean", "BUG-1567", g_relinquish_clean_fix,
         "space rivals: relinquishing host homeworld clears star/planet tech level 5 and rebuilds near-stars");

// BUG-1582: R7 homeworld protection HP floor + intro cinematic suppression, Civ->Space homeworld cleanup,
// spherical surface orientation for cities, and Space Rivals guest emigration starter/colony unlocks.
static bool g_r7_guard_fix = true;
COOP_FIX("rivals.space_r7_guard", "BUG-1582", g_r7_guard_fix,
         "space rivals: restore city HP floor on blocked DestroyCity and suppress R7 false positives during intro cinematics");
static bool g_home_cleanup_fix = true;
COOP_FIX("rivals.space_home_cleanup", "BUG-1582", g_home_cleanup_fix,
         "space rivals: sweep leftover foreign Civ vehicles and unify rival Civ cities on host homeworld upon entering Space");
static bool g_city_orient_fix = true;
COOP_FIX("rivals.space_city_orient", "BUG-1582", g_city_orient_fix,
         "space rivals: orient sanitized/emigrated cities along the planet radial normal instead of identity quaternion");
static bool g_emig_unlock_fix = true;
COOP_FIX("rivals.space_emig_unlock", "BUG-1582", g_emig_unlock_fix,
         "space rivals: unlock tutorial travel flags, starter drives/tools and a starter Colony Pack for emigrated guests");

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }
static bool readable(const void* p, size_t n) { return p && !IsBadReadPtr(p, n); }

// ---- Game symbols ----
static uintptr_t a_StarMgr, a_GetEmpire, a_RequirePlanets, a_GetPlanets, a_PlayerEmpire, a_PlayerEmpireID,
    a_ActivePlanetRec, a_ActiveStarRec, a_AddMoney, a_AddStar, a_AddPlanet, a_SetHomePlanet, a_PlanetTech,
    a_FillPlanet, a_GameAlloc, a_CityName, a_CityCtor, a_AddCityData, a_Context, a_SpacePlayerData, a_SpaceGame,
    a_PlayerUFOGet, a_RelMgr, a_ApplyRel, a_CalcRel, a_MakeContact, a_ToolMgr, a_GetTool, a_AddItemByKey,
    a_OnUseTool, a_DestroyPlanet, a_CaptureSystem, a_TransferStar, a_SetCaptureProgress, a_DestroyCity, a_NounMgr,
    a_NearRebuild, a_NearVt, a_GetAllCities, a_GetAllVehicles, a_DestroyInstance, a_VehicleRemove,
    a_CityChangeOwner, a_CitySetPlayerOwned, a_CivByPol, a_CombatantSetHealth, a_MissionMgr, a_SetFlightSchool,
    a_SetMission201;

// ---- R7 Hook originals ----
using OnUseTool_t = bool(SK_THISCALL*)(void*, void*, const float*, int);
using DestroyPlanet_t = void(SK_THISCALL*)(void*);
using CaptureSystem_t = void(__cdecl*)(void*, uint32_t);
using TransferStar_t = void(__cdecl*)(void*, uint32_t);
using SetCaptureProgress_t = void(SK_THISCALL*)(void*, uint32_t, float);
using DestroyCity_t = void(SK_THISCALL*)(void*, void*, uint32_t);

static OnUseTool_t o_OnUseTool = nullptr;
static DestroyPlanet_t o_DestroyPlanet = nullptr;
static CaptureSystem_t o_CaptureSystem = nullptr;
static TransferStar_t o_TransferStar = nullptr;
static SetCaptureProgress_t o_SetCaptureProgress = nullptr;
static DestroyCity_t o_DestroyCity = nullptr;

// ---- Runtime state ----
static bool g_hooks_ok = false;
static bool g_emigrating_guard = false;
CALL_GUARD("space_rivals.emigrating", g_emigrating_guard);
static bool g_applying_influence = false;
CALL_GUARD("space_rivals.influence", g_applying_influence);

// R5 Emigration state
static EmigrationStore g_emig_store;                  // host: persisted by player_key
static std::string g_emig_store_world;
static uint32_t g_emig_store_serial = 0;
static std::map<uint8_t, EmigrationRecord> g_emig_by_id; // host: active guest id -> assignment
static std::map<uint8_t, uint16_t> g_guest_civ_cities;   // host: optional Civ city count per guest
static EmigrationRecord g_my_emig;                       // guest: received assignment
static bool g_my_emig_pending = false;
static GuestEmigMachine g_emig_sm;                       // guest: CreateHome -> RelinquishHost -> TeleportHome
static uint32_t g_relinquished_host_home_planet = 0;
static uint32_t g_relinquished_host_home_star = 0;
static std::string g_last_emig_notice;

// R7 Protection state
static HomeProtectionTable g_homes;
static double g_last_prot_notice = 0.0;
static uint32_t g_buster_blocked = 0;
static uint32_t g_capture_blocked = 0;
static uint32_t g_city_hp_restored = 0;

// BUG-1582: Civ -> Space homeworld leftover cleanup & emigrated guest starter/colony unlock state
static uint32_t g_home_cleanup_epoch = 0;
static double g_home_cleanup_start = 0.0;
static double g_home_cleanup_last = 0.0;
static bool g_home_cleanup_done = false;
static bool g_home_cleanup_pending = false;
static uint32_t g_civ_vehicles_swept = 0;
static uint32_t g_civ_cities_unified = 0;
static uint32_t g_emig_unlock_epoch = 0;
static uint32_t g_emig_colony_pack_epoch = 0;
static bool g_emig_unlock_pending = false;
static uint32_t g_emig_unlocks_applied = 0;

// NPC Influence & Space budgets
static uint32_t g_inf_seq = 0;
static uint32_t g_inf_applied = 0;
static uint32_t g_draft_payouts = 0;
static int32_t g_queued_draft_money = 0;
static bool g_draft_money_pending = false;
static std::map<uint32_t, double> g_inf_earn_cd;
static std::map<uint8_t, int32_t> g_guest_money;
static double g_last_trickle = 0.0;

// Race stats & Chronicle Finale
static std::map<uint8_t, SpaceRaceStats> g_race_stats;
static SpaceRaceStats g_last_sent_stats{};
static bool g_have_sent_stats = false;
static double g_next_progress_send = 0.0;
static double g_next_host_sync = 0.0;
static uint32_t g_core_star_id = 0;
static double g_core_scan_next = 0.0;
static FinaleState g_finale;
static std::string g_finale_summary;

// Cleanup per-player maps when a player leaves
PLAYER_STATE(
    "space_rivals",
    [](uint8_t id) {
        g_emig_by_id.erase(id);
        g_guest_civ_cities.erase(id);
        g_guest_money.erase(id);
        g_race_stats.erase(id);
    },
    [](uint8_t id) -> size_t {
        return g_emig_by_id.count(id) + g_guest_civ_cities.count(id) + g_guest_money.count(id) +
               g_race_stats.count(id);
    });

// ---- Public queries ----
bool enabled() { return g_space_rivals_fix; }
bool stage_rivals_wanted() { return g_space_rivals_fix && rivals::enabled() && rivals::stage_is_rivals(rivals::Stage::Space); }
bool active() { return stage_rivals_wanted() && spaceext::in_space(); }

// BUG-1557: true while a Space Rivals guest is still waiting for or running Phase 1 homeworld emigration,
// so history::guest_space waits to call SpaceGame_SetArchetype until the emigrated homeworld is established.
bool emigration_in_progress()
{
    if (!active() || spaceext::is_host())
        return false;
    if (g_my_emig_pending || g_emigrating_guard)
        return true;
    if (g_my_emig.valid() && !g_emig_sm.home_created)
        return true;
    return false;
}

bool is_relinquished_host_home(uint32_t planet_id)
{
    return active() && !spaceext::is_host() && g_relinquished_host_home_planet != 0 &&
           planet_id == g_relinquished_host_home_planet;
}

void set_guest_civ_cities(uint8_t player_id, uint16_t cities)
{
    if (rivals::valid_player(player_id) && player_id > 0)
        g_guest_civ_cities[player_id] = clamp_emig_cities((int)cities);
}

uint16_t guest_civ_cities(uint8_t player_id)
{
    auto it = g_guest_civ_cities.find(player_id);
    return it == g_guest_civ_cities.end() ? 0 : it->second;
}

// ---- Low-level game helpers ----
static double now_s() { return H ? H->time_seconds() : 0.0; }
static void* star_mgr() { return a_StarMgr ? cdecl_call<void*>(a_StarMgr) : nullptr; }
static void* player_empire() { return a_PlayerEmpire ? cdecl_call<void*>(a_PlayerEmpire) : nullptr; }
static uint32_t my_pol() { return a_PlayerEmpireID ? cdecl_call<uint32_t>(a_PlayerEmpireID) : 0xFFFFFFFFu; }
static int context() { return a_Context && spaceext::in_space() ? cdecl_call<int>(a_Context) : -1; }
static void* rel_mgr() { return a_RelMgr ? cdecl_call<void*>(a_RelMgr) : nullptr; }

static void* empire_of(uint32_t pol)
{
    void* sm = star_mgr();
    return sm && a_GetEmpire && pol != 0xFFFFFFFFu ? thiscall<void*>(a_GetEmpire, sm, pol) : nullptr;
}

static bool is_npc_empire_pol(uint32_t pol)
{
    return (pol & 0x80000000u) != 0 && pol != 0xFFFFFFFFu && pol != my_pol() && !spaceext::allies_mirror_pol(pol) &&
           empire_of(pol) != nullptr;
}

static std::vector<void*> ptr_vec(void* v, size_t max = 4096)
{
    std::vector<void*> out;
    if (!readable(v, 8))
        return out;
    void** b = F<void**>(v, 0);
    void** e = F<void**>(v, 4);
    if (!b || e <= b || (size_t)(e - b) > max || !readable(b, (size_t)(e - b) * 4))
        return out;
    out.assign(b, e);
    return out;
}

static std::string str16(void* s)
{
    if (!readable(s, 12))
        return "";
    const char16_t* b = F<const char16_t*>(s, 0);
    const char16_t* e = F<const char16_t*>(s, 4);
    if (!b || e <= b || e - b > 200 || !readable(b, (size_t)(e - b) * 2))
        return "";
    std::string out;
    for (const char16_t* p = b; p < e; p++) {
        uint32_t c = *p;
        if (c < 0x80)
            out += (char)c;
        else if (c < 0x800) {
            out += (char)(0xC0 | (c >> 6));
            out += (char)(0x80 | (c & 0x3F));
        } else {
            out += (char)(0xE0 | (c >> 12));
            out += (char)(0x80 | ((c >> 6) & 0x3F));
            out += (char)(0x80 | (c & 0x3F));
        }
    }
    return out;
}

static std::vector<void*> planets_of(void* star)
{
    if (!readable(star, 0xB0))
        return {};
    if (!F<uint8_t>(star, 0xAC) && a_RequirePlanets && star_mgr())
        thiscall<void>(a_RequirePlanets, star_mgr(), star, 0);
    void* v = a_GetPlanets ? thiscall<void*>(a_GetPlanets, star) : (uint8_t*)star + 0x84;
    return ptr_vec(v, 64);
}

static void* planet_record(uint32_t planet_id)
{
    if (!planet_id)
        return nullptr;
    void* s = spacecoop::star_record(planet_id & 0xFFFFFFu);
    if (!s)
        return nullptr;
    auto ps = planets_of(s);
    uint32_t idx = planet_id >> 24;
    if (idx < ps.size() && readable(ps[idx], 0x1A4) && F<uint32_t>(ps[idx], 0x184) == planet_id)
        return ps[idx];
    for (void* p : ps)
        if (readable(p, 0x1A4) && F<uint32_t>(p, 0x184) == planet_id)
            return p;
    return nullptr;
}

static uint32_t active_planet_id()
{
    if (!spaceext::in_space() || !a_ActivePlanetRec)
        return 0;
    void* rec = cdecl_call<void*>(a_ActivePlanetRec);
    return readable(rec, 0x188) ? F<uint32_t>(rec, 0x184) : 0;
}

static uint32_t active_star_id()
{
    if (!spaceext::in_space() || !a_ActiveStarRec)
        return 0;
    void* st = cdecl_call<void*>(a_ActiveStarRec);
    return readable(st, 0x74) ? F<uint32_t>(st, 0x70) : 0;
}

static void* my_civ_on(void* rec, uint32_t pol)
{
    if (!readable(rec, 0x168))
        return nullptr;
    void* best = nullptr;
    size_t best_cities = 0;
    for (void* cd : ptr_vec((uint8_t*)rec + 0x15C, 32)) {
        if (!readable(cd, 0x50) || F<uint32_t>(cd, 0x14) != pol)
            continue;
        size_t n = ptr_vec((uint8_t*)cd + 0x3C, 64).size();
        if (!best || n > best_cities) {
            best = cd;
            best_cities = n;
        }
    }
    return best;
}

static uint16_t count_cities_on(void* rec, uint32_t pol)
{
    if (!readable(rec, 0x168))
        return 0;
    size_t total = 0;
    for (void* cd : ptr_vec((uint8_t*)rec + 0x15C, 32))
        if (readable(cd, 0x50) && F<uint32_t>(cd, 0x14) == pol)
            total += ptr_vec((uint8_t*)cd + 0x3C, 64).size();
    return (uint16_t)std::min<size_t>(total, 0xFFFFu);
}

static void erase_from_vec(void* vec, void* p)
{
    if (!readable(vec, 8))
        return;
    void** b = F<void**>(vec, 0);
    void** e = F<void**>(vec, 4);
    if (!b || e <= b || e - b > 4096 || !readable(b, (size_t)(e - b) * 4))
        return;
    for (void** it = b; it < e; it++) {
        if (*it == p) {
            memmove(it, it + 1, (size_t)(e - it - 1) * sizeof(void*));
            F<void**>(vec, 4) = e - 1;
            vcall<void>(p, 1); // Release
            return;
        }
    }
}

static void remove_colony_from_spd(void* rec)
{
    void* spd = a_SpacePlayerData ? *(void**)a_SpacePlayerData : nullptr;
    if (!readable(spd, 0x2C))
        return;
    void** b = F<void**>(spd, 0x20);
    void** e = F<void**>(spd, 0x24);
    if (!b || e <= b || e - b > 4096 || !readable(b, (size_t)(e - b) * 4))
        return;
    for (void** it = b; it < e; it++) {
        if (readable(*it, 0x140) && F<void*>(*it, 0x13C) == rec) {
            void* p = *it;
            memmove(it, it + 1, (size_t)(e - it - 1) * sizeof(void*));
            F<void**>(spd, 0x24) = e - 1;
            vcall<void>(p, 0xC0 / 4); // Release
            return;
        }
    }
}

// Walk an EASTL rb_tree map (root at map+0xC, right +0, left +4)
template <typename Fn>
static void walk_map(void* map, Fn fn)
{
    if (!readable(map, 0x18))
        return;
    void* root = F<void*>(map, 0xC);
    std::vector<void*> st;
    void* n = root;
    int guard = 0;
    while ((n || !st.empty()) && guard++ < 20000) {
        while (n) {
            if (!readable(n, 0x40) || st.size() > 64)
                return;
            st.push_back(n);
            n = F<void*>(n, 4);
        }
        n = st.back();
        st.pop_back();
        fn(n);
        n = F<void*>(n, 0);
    }
}

static uint16_t count_allied_npcs()
{
    void* rm = rel_mgr();
    uint32_t me = my_pol();
    if (!rm || me == 0xFFFFFFFFu)
        return 0;
    uint16_t allies = 0;
    walk_map((uint8_t*)rm + 0x24, [&](void* node) {
        uint32_t a = F<uint32_t>(node, 0x10);
        uint32_t b = F<uint32_t>(node, 0x14);
        uint32_t npc = a == me ? b : (b == me ? a : 0);
        if (!npc || !is_npc_empire_pol(npc))
            return;
        uint32_t flags = F<uint32_t>(node, 0x1C);
        if (flags & 2u) // bit 1 = alliance
            allies++;
    });
    return allies;
}

static float calc_dist_to_core()
{
    void* sm = star_mgr();
    if (!sm)
        return 0.0f;
    void* core = g_core_star_id ? spacecoop::star_record(g_core_star_id) : nullptr;
    double now = now_s();
    if (!core && now >= g_core_scan_next) {
        g_core_scan_next = now + 10.0;
        if (readable((uint8_t*)sm + 0xC8, 8)) {
            uint8_t* b = F<uint8_t*>(sm, 0xC8);
            uint8_t* e = F<uint8_t*>(sm, 0xCC);
            if (b && e > b && (e - b) % 20 == 0 && readable(b, (size_t)(e - b))) {
                for (uint8_t* s = b; s < e && !core; s += 20) {
                    void** rb = *(void***)s;
                    void** re = *(void***)(s + 4);
                    if (!rb || re <= rb || !readable(rb, (size_t)((uint8_t*)re - (uint8_t*)rb)))
                        continue;
                    for (void** r = rb; r < re; r++) {
                        if (*r && readable(*r, 0x74) && F<int>(*r, 0x48) == 1) {
                            core = *r;
                            g_core_star_id = F<uint32_t>(*r, 0x70);
                            break;
                        }
                    }
                }
            }
        }
    }
    float cx = core && readable(core, 0x48) ? F<float>(core, 0x3C) : 0.0f;
    float cy = core && readable(core, 0x48) ? F<float>(core, 0x40) : 0.0f;
    float px = 0.0f, py = 0.0f;
    bool have_pos = false;
    if (context() == 2 && a_PlayerUFOGet) {
        void* mgr = cdecl_call<void*>(a_PlayerUFOGet);
        void* u = readable(mgr, 0x44) ? F<void*>(mgr, 0x40) : nullptr;
        if (readable(u, 0x44)) {
            px = F<float>(u, 0x38);
            py = F<float>(u, 0x3C);
            have_pos = true;
        }
    }
    if (!have_pos && a_ActiveStarRec) {
        void* act = cdecl_call<void*>(a_ActiveStarRec);
        if (readable(act, 0x48)) {
            px = F<float>(act, 0x3C);
            py = F<float>(act, 0x40);
            have_pos = true;
        }
    }
    if (!have_pos)
        return 0.0f;
    return std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
}

// ---- R7: Homeworld Protection Helpers & Hooks ----
static void refresh_homes_from_rosters()
{
    if (!active())
        return;
    for (uint8_t id : spacemap::roster_ids()) {
        const spacemap::Roster* r = spacemap::roster_of(id);
        if (r && r->home_star && r->home_planet)
            g_homes.set(id, r->home_star, r->home_planet);
    }
    if (void* emp = player_empire(); readable(emp, 0xB8)) {
        uint32_t hs = F<uint32_t>(emp, 0xB0);
        uint32_t hp = F<uint32_t>(emp, 0xB4);
        if (hs && hs != 0xFFFFFFFFu && hp && hp != 0xFFFFFFFFu)
            g_homes.set(spaceext::my_id(), hs, hp);
    }
    if (g_relinquished_host_home_star && g_relinquished_host_home_planet)
        g_homes.set(0, g_relinquished_host_home_star, g_relinquished_host_home_planet);
}

bool is_protected_homeworld_planet(uint32_t planet_id)
{
    if (!active() || !planet_id)
        return false;
    return g_homes.is_protected_planet(planet_id);
}

bool is_protected_homeworld_star(uint32_t star_id)
{
    if (!active() || !star_id)
        return false;
    return g_homes.is_protected_star(star_id);
}

static void emit_prot_notice(uint32_t tool_inst, bool is_capture, bool has_rival_attacker = true)
{
    if (g_r7_guard_fix && !r7_should_emit_notice(world::cinematic_playing(), has_rival_attacker))
        return;
    double now = now_s();
    if (now - g_last_prot_notice < 2.0)
        return;
    g_last_prot_notice = now;
    std::string msg = format_protection_notice(tool_inst, is_capture, spaceext::russian());
    spaceext::notice(msg);
    if (H && M)
        H->log(M, SK_INFO, "space_rivals: R7 blocked (%s)", msg.c_str());
}

// BUG-1582: restore a protected homeworld city's City Hall and ruined buildings above 0 HP so Spore does not
// re-invoke DestroyCity every simulation tick forever (fixes 67x infinite R7 spam).
static void restore_protected_city_hp(void* city)
{
    if (!readable(city, 0x350))
        return;
    auto heal_building = [](void* bld) {
        if (!readable(bld, 0x160))
            return;
        float max_hp = F<float>(bld, 0x14C);
        float floor_hp = r7_city_hp_floor(max_hp);
        // cSimPlanetHighLOD::Update triggers DestroyCity on any non-zero cCombatant::GetState (+0x154).
        if (F<int>(bld, 0x154) != 0)
            F<int>(bld, 0x154) = 0;
        float cur_hp = F<float>(bld, 0x158);
        if (!std::isfinite(cur_hp) || cur_hp < floor_hp) {
            if (a_CombatantSetHealth)
                thiscall<void>(a_CombatantSetHealth, (uint8_t*)bld + 0x120, floor_hp);
            else
                F<float>(bld, 0x158) = floor_hp;
            g_city_hp_restored++;
        }
    };
    heal_building(F<void*>(city, 0x320)); // mpCityHall
    for (void* b : ptr_vec((uint8_t*)city + 0x340, 64))
        heal_building(b);
    if (o_SetCaptureProgress)
        o_SetCaptureProgress(city, 0xFFFFFFFFu, 0.0f);
}

static bool SK_THISCALL h_OnUseTool(void* mgr, void* tool, const float* aim_pos, int flags)
{
    if (active() && readable(tool, 0x10)) {
        const uint32_t* key = vcall<const uint32_t*>(tool, 0x18 / 4);
        uint32_t inst = readable(key, 4) ? key[0] : 0;
        uint32_t ap = active_planet_id();
        if (g_homes.should_block_tool(inst, ap)) {
            if (inst == kToolPlanetBuster)
                g_buster_blocked++;
            else
                g_capture_blocked++;
            emit_prot_notice(inst, inst == kToolFanaticalFrenzy);
            return false;
        }
    }
    return o_OnUseTool(mgr, tool, aim_pos, flags);
}

static void SK_THISCALL h_DestroyPlanet(void* self)
{
    if (active()) {
        uint32_t ap = active_planet_id();
        if (g_homes.is_protected_planet(ap)) {
            g_buster_blocked++;
            emit_prot_notice(kToolPlanetBuster, false);
            return;
        }
    }
    o_DestroyPlanet(self);
}

static void __cdecl h_CaptureSystem(void* star, uint32_t new_pol)
{
    if (g_space_rivals_fix && rivals::stage_is_rivals(rivals::Stage::Space) && readable(star, 0x74)) {
        uint32_t sid = F<uint32_t>(star, 0x70);
        uint32_t cur_owner = F<uint32_t>(star, 0x54);
        bool block = g_r7_guard_fix
                         ? r7_should_block_star_transfer(g_homes.is_protected_star(sid), g_emigrating_guard,
                                                         world::cinematic_playing(), cur_owner, new_pol)
                         : g_homes.should_block_capture(sid, 0, g_emigrating_guard);
        if (block) {
            g_capture_blocked++;
            emit_prot_notice(0, true);
            return;
        }
    }
    o_CaptureSystem(star, new_pol);
}

static void __cdecl h_TransferStar(void* star, uint32_t new_pol)
{
    if (g_space_rivals_fix && rivals::stage_is_rivals(rivals::Stage::Space) && readable(star, 0x74)) {
        uint32_t sid = F<uint32_t>(star, 0x70);
        uint32_t cur_owner = F<uint32_t>(star, 0x54);
        bool block = g_r7_guard_fix
                         ? r7_should_block_star_transfer(g_homes.is_protected_star(sid), g_emigrating_guard,
                                                         world::cinematic_playing(), cur_owner, new_pol)
                         : g_homes.should_block_capture(sid, 0, g_emigrating_guard);
        if (block) {
            g_capture_blocked++;
            emit_prot_notice(0, true);
            return;
        }
    }
    o_TransferStar(star, new_pol);
}

static void SK_THISCALL h_SetCaptureProgress(void* city, uint32_t pol, float pct)
{
    if (active() && pct > 0.0f && pol != 0xFFFFFFFFu) {
        uint32_t ap = active_planet_id();
        uint32_t city_pol = readable(city, 0x34) ? F<uint32_t>(city, 0x30) : 0;
        bool block = g_r7_guard_fix
                         ? r7_should_block_city_capture(g_homes.is_protected_planet(ap), world::cinematic_playing(),
                                                        city_pol, pol, pct)
                         : g_homes.is_protected_planet(ap);
        if (block) {
            g_capture_blocked++;
            emit_prot_notice(0, true);
            o_SetCaptureProgress(city, 0xFFFFFFFFu, 0.0f);
            return;
        }
    }
    o_SetCaptureProgress(city, pol, pct);
}

static void SK_THISCALL h_DestroyCity(void* self, void* city, uint32_t attacker_pol)
{
    if (active()) {
        uint32_t ap = active_planet_id();
        if (g_homes.is_protected_planet(ap)) {
            if (g_r7_guard_fix)
                restore_protected_city_hp(city);
            g_capture_blocked++;
            bool has_attacker = (attacker_pol != my_pol());
            emit_prot_notice(0, false, has_attacker);
            return;
        }
    }
    o_DestroyCity(self, city, attacker_pol);
}

// ---- Wire send helpers (MSG_RIVALS_FACTION = 81, Stage::Space) ----
static void write_i32(net::Writer& w, int32_t v) { w.u32((uint32_t)v); }
static int32_t read_i32(net::Reader& r) { return (int32_t)r.u32(); }
static void write_str64(net::Writer& w, const std::string& s)
{
    w.str8(s.size() > 64 ? s.substr(0, 64) : s);
}
static std::string read_str64(net::Reader& r)
{
    std::string s = r.str8();
    if (s.size() > 64)
        s.resize(64);
    return s;
}

static void send_emig_msg(uint8_t target, EmigKind kind, const EmigrationRecord& r)
{
    net::Writer w;
    w.u8((uint8_t)kind);
    w.u8(r.player_id);
    w.u32(r.orig_home_star);
    w.u32(r.orig_home_planet);
    w.u32(r.new_home_star);
    w.u32(r.new_home_planet);
    w.u16(r.target_cities);
    write_str64(w, r.old_planet_name);
    write_str64(w, r.new_planet_name);
    if (target == 255)
        rivals::broadcast_faction(rivals::Stage::Space, wire::rivals_faction::SK_EMIGRATE, w);
    else
        rivals::send_faction_to(target, rivals::Stage::Space, wire::rivals_faction::SK_EMIGRATE, w);
}

static void send_homes_sync()
{
    if (!spaceext::is_host())
        return;
    net::Writer w;
    w.u8((uint8_t)OP_HOMES_SYNC);
    w.u8((uint8_t)std::min<size_t>(g_homes.homes.size(), 4));
    size_t n = 0;
    for (const auto& [pid, h] : g_homes.homes) {
        if (n++ >= 4)
            break;
        w.u8(pid);
        w.u32(h.star_id);
        w.u32(h.planet_id);
    }
    rivals::broadcast_faction(rivals::Stage::Space, wire::rivals_faction::SK_STAGE_CUSTOM, w);
}

static void send_finale_summary()
{
    if (!spaceext::is_host() || !g_finale.awarded)
        return;
    net::Writer w;
    w.u8((uint8_t)OP_FINALE_SUMMARY);
    w.u8(g_finale.first_player);
    w.u8(g_finale.winner);
    write_i32(w, g_finale.bonus);
    for (int i = 0; i < rivals::kMaxPlayers; i++)
        write_i32(w, g_finale.totals[i]);
    rivals::broadcast_faction(rivals::Stage::Space, wire::rivals_faction::SK_STAGE_CUSTOM, w);
}

// ---- R5: Host planet picker & assignment ----
static bool host_pick_emigration_for(uint8_t guest_id, EmigrationRecord& out)
{
    void* sm = star_mgr();
    void* emp = player_empire();
    if (!sm || !readable(emp, 0xD4))
        return false;
    uint32_t orig_star_id = F<uint32_t>(emp, 0xB0);
    uint32_t orig_planet_id = F<uint32_t>(emp, 0xB4);
    if (!orig_star_id || orig_star_id == 0xFFFFFFFFu || !orig_planet_id || orig_planet_id == 0xFFFFFFFFu)
        return false;
    void* orig_star = spacecoop::star_record(orig_star_id);
    void* orig_rec = planet_record(orig_planet_id);
    if (!orig_star || !orig_rec)
        return false;

    HomeRef ref;
    ref.star_id = orig_star_id;
    ref.planet_id = orig_planet_id;
    ref.type = F<int32_t>(orig_rec, 0x28);
    ref.is_moon = (F<uint32_t>(orig_rec, 0x2C) & 0x4u) != 0;
    ref.atmo = F<float>(orig_rec, 0xB0);
    ref.temp = F<float>(orig_rec, 0xB4);
    ref.water = F<float>(orig_rec, 0xB8);
    uint16_t orig_cities = count_cities_on(orig_rec, my_pol());
    ref.cities = orig_cities > 0 ? clamp_emig_cities(orig_cities) : kDefaultEmigCities;
    if (auto it = g_guest_civ_cities.find(guest_id); it != g_guest_civ_cities.end())
        ref.cities = clamp_emig_cities(it->second);

    std::set<uint32_t> reserved;
    reserved.insert(orig_star_id);
    for (const auto& [k, r] : g_emig_store.by_key)
        if (r.valid())
            reserved.insert(r.new_home_star);
    for (const auto& [pid, r] : g_emig_by_id)
        if (r.valid())
            reserved.insert(r.new_home_star);

    // Collect nearby unowned normal stars sorted by proximity to preferred distance band
    float* hp = &F<float>(orig_star, 0x3C);
    float pref_d = preferred_emigration_distance(rivals::base_distance());
    struct StarCand {
        float sort_key;
        float dist;
        void* star;
    };
    std::vector<StarCand> stars;
    if (readable((uint8_t*)sm + 0xC8, 8)) {
        uint8_t* b = F<uint8_t*>(sm, 0xC8);
        uint8_t* e = F<uint8_t*>(sm, 0xCC);
        if (b && e > b && (e - b) % 20 == 0 && readable(b, (size_t)(e - b))) {
            for (uint8_t* sec = b; sec < e; sec += 20) {
                void** arr = *(void***)sec;
                void** end = *(void***)(sec + 4);
                if (!arr || end <= arr || !readable(arr, (size_t)((uint8_t*)end - (uint8_t*)arr)))
                    continue;
                for (void** it = arr; it < end && it - arr < 4096; it++) {
                    void* st = *it;
                    if (!readable(st, 0xB0))
                        continue;
                    uint32_t sid = F<uint32_t>(st, 0x70);
                    if (!sid || reserved.count(sid))
                        continue;
                    if (F<uint32_t>(st, 0x54) != 0xFFFFFFFFu)
                        continue;
                    if (F<uint32_t>(st, 0x5C) & 7u)
                        continue;
                    int32_t stype = F<int32_t>(st, 0x48);
                    if (stype < 2 || stype > 6)
                        continue;
                    float* p = &F<float>(st, 0x3C);
                    float dx = p[0] - hp[0], dy = p[1] - hp[1], dz = p[2] - hp[2];
                    float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (d < 0.5f || d > 180.0f)
                        continue;
                    stars.push_back({std::fabs(d - pref_d), d, st});
                }
            }
        }
    }
    std::sort(stars.begin(), stars.end(), [](const StarCand& a, const StarCand& b) { return a.sort_key < b.sort_key; });

    // Inspect planets of up to 24 best-distance stars
    std::vector<CandidatePlanet> cands;
    size_t limit = std::min<size_t>(stars.size(), 24);
    for (size_t i = 0; i < limit; i++) {
        void* st = stars[i].star;
        uint32_t sid = F<uint32_t>(st, 0x70);
        int32_t stype = F<int32_t>(st, 0x48);
        std::string sname = str16((uint8_t*)st + 0x60);
        for (void* pr : planets_of(st)) {
            if (!readable(pr, 0x1A4))
                continue;
            CandidatePlanet cp;
            cp.star_id = sid;
            cp.planet_id = F<uint32_t>(pr, 0x184);
            cp.star_type = stype;
            cp.planet_type = F<int32_t>(pr, 0x28);
            cp.tech_level = F<int32_t>(pr, 0x194);
            cp.is_moon = (F<uint32_t>(pr, 0x2C) & 0x4u) != 0;
            cp.star_owned = F<uint32_t>(st, 0x54) != 0xFFFFFFFFu;
            cp.star_is_home = (F<uint32_t>(st, 0x5C) & 7u) != 0;
            cp.dist_from_host = stars[i].dist;
            cp.atmo = F<float>(pr, 0xB0);
            cp.temp = F<float>(pr, 0xB4);
            cp.water = F<float>(pr, 0xB8);
            cp.star_name = sname;
            cp.planet_name = str16((uint8_t*)pr + 0x18);
            cands.push_back(std::move(cp));
        }
    }

    int idx = pick_emigration_planet(cands, ref, rivals::base_distance(), reserved);
    if (idx < 0)
        return false;
    const auto& pick = cands[(size_t)idx];
    out = EmigrationRecord{};
    out.player_id = guest_id;
    out.orig_home_star = orig_star_id;
    out.orig_home_planet = orig_planet_id;
    out.new_home_star = pick.star_id;
    out.new_home_planet = pick.planet_id;
    out.target_cities = ref.cities;
    out.old_planet_name = str16((uint8_t*)orig_rec + 0x18);
    if (out.old_planet_name.empty())
        out.old_planet_name = str16((uint8_t*)orig_star + 0x60);
    out.new_planet_name = !pick.planet_name.empty() ? pick.planet_name : pick.star_name;
    out.applied = false;
    return out.valid();
}

static void host_ensure_guest_emigrations()
{
    if (!spaceext::is_host() || !active() || !spaceext::connected())
        return;
    void* emp = player_empire();
    if (!readable(emp, 0xB8))
        return;
    uint32_t hs = F<uint32_t>(emp, 0xB0);
    uint32_t hp = F<uint32_t>(emp, 0xB4);
    if (!hs || hs == 0xFFFFFFFFu || !hp || hp == 0xFFFFFFFFu)
        return;
    g_homes.set(0, hs, hp);

    for (const auto& p : spaceext::ctx().sess->players()) {
        if (p.id == 0 || !rivals::valid_player(p.id))
            continue;
        std::string key = spaceext::kept_key(p.id);
        auto it = g_emig_by_id.find(p.id);
        if (it == g_emig_by_id.end()) {
            // Check persisted store first
            if (!key.empty()) {
                if (auto sit = g_emig_store.by_key.find(key); sit != g_emig_store.by_key.end() && sit->second.valid()) {
                    EmigrationRecord r = sit->second;
                    r.player_id = p.id;
                    r.applied = false;
                    g_emig_by_id[p.id] = r;
                    it = g_emig_by_id.find(p.id);
                }
            }
        }
        if (it == g_emig_by_id.end()) {
            EmigrationRecord r;
            if (host_pick_emigration_for(p.id, r)) {
                g_emig_by_id[p.id] = r;
                if (!key.empty())
                    g_emig_store.by_key[key] = r;
                g_homes.set(p.id, r.new_home_star, r.new_home_planet);
                send_emig_msg(255, EMIG_ASSIGN, r);
                send_homes_sync();
                if (H && M)
                    H->log(M, SK_INFO, "space_rivals: assigned guest %u emigration -> star %u planet %08X '%s' (%u cities)",
                           p.id, r.new_home_star, r.new_home_planet, r.new_planet_name.c_str(), r.target_cities);
            }
        } else {
            g_homes.set(p.id, it->second.new_home_star, it->second.new_home_planet);
            if (!it->second.applied)
                send_emig_msg(p.id, EMIG_ASSIGN, it->second);
        }
    }
}

// ---- R5: Guest applies its emigrated homeworld (runs outside frame) ----
static int popcount8_sr(uint8_t v)
{
    int n = 0;
    for (; v; v &= (uint8_t)(v - 1))
        n++;
    return n;
}

static void recompute_civ_city_totals(void* civ)
{
    if (!readable(civ, 0x48))
        return;
    int turrets = 0, buildings = 0;
    for (void* c : ptr_vec((uint8_t*)civ + 0x3C, 64)) {
        if (!readable(c, 0x2C))
            continue;
        int t = popcount8_sr(F<uint8_t>(c, 0x28));
        turrets += t;
        buildings += std::max(0, F<int32_t>(c, 0x24) - t);
    }
    F<int32_t>(civ, 0x20) = turrets;
    F<int32_t>(civ, 0x24) = buildings;
}

static void sanitize_city_pose(void* c, size_t idx)
{
    if (!readable(c, 0x20))
        return;
    float x = F<float>(c, 0x00), y = F<float>(c, 0x04), z = F<float>(c, 0x08);
    bool bad_pos = !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || (x * x + y * y + z * z < 100.0f);
    if (bad_pos) {
        // FillPlanetDataForTechLevel on unvisited planets leaves cCityData::pos at (0,0,0), which causes
        // 0/0 NaN cube-map projection faults at PlanetRecord_EcoLevel+0x858 (0x00BBD808) on landing (BUG-1555).
        float angle = (float)idx * 2.0943951f; // ~120 deg spacing
        float lat = (idx % 2 == 0) ? 0.35f : -0.35f;
        float rad = 160.0f;
        x = rad * std::cos(lat) * std::cos(angle);
        y = rad * std::cos(lat) * std::sin(angle);
        z = rad * std::sin(lat);
        F<float>(c, 0x00) = x;
        F<float>(c, 0x04) = y;
        F<float>(c, 0x08) = z;
    }
    float qx = F<float>(c, 0x0C), qy = F<float>(c, 0x10), qz = F<float>(c, 0x14), qw = F<float>(c, 0x18);
    if (g_city_orient_fix) {
        // BUG-1582: align city local +Z to outward radial surface normal so cities never tilt onto their sides.
        if (bad_pos || !city_quat_upright(x, y, z, qx, qy, qz, qw)) {
            float q[4];
            city_surface_quat(x, y, z, q);
            F<float>(c, 0x0C) = q[0];
            F<float>(c, 0x10) = q[1];
            F<float>(c, 0x14) = q[2];
            F<float>(c, 0x18) = q[3];
        }
    } else {
        float qn = qx * qx + qy * qy + qz * qz + qw * qw;
        if (!std::isfinite(qn) || qn < 0.01f) {
            F<float>(c, 0x0C) = 0.0f;
            F<float>(c, 0x10) = 0.0f;
            F<float>(c, 0x14) = 0.0f;
            F<float>(c, 0x18) = 1.0f;
        }
    }
}

static void sanitize_planet_city_poses(void* rec)
{
    if (!g_city_orient_fix || !readable(rec, 0x188) || active_planet_id() == F<uint32_t>(rec, 0x184))
        return;
    for (void* civ : ptr_vec((uint8_t*)rec + 0x15C, 32)) {
        if (!readable(civ, 0x48))
            continue;
        auto cities = ptr_vec((uint8_t*)civ + 0x3C, 64);
        for (size_t i = 0; i < cities.size(); i++)
            sanitize_city_pose(cities[i], i);
    }
}

static void ensure_city_count(void* rec, uint32_t pol, uint16_t want)
{
    if (!readable(rec, 0x188) || active_planet_id() == F<uint32_t>(rec, 0x184))
        return; // never mutate cCivData while live cCity objects point to its cCityData entries
    void* civ = my_civ_on(rec, pol);
    if (!civ)
        return;
    want = clamp_emig_cities((int)want);
    auto cities = ptr_vec((uint8_t*)civ + 0x3C, 64);
    for (size_t i = 0; i < cities.size() && i < want; i++)
        sanitize_city_pose(cities[i], i);
    if (cities.size() > want) {
        // cCityData (0xC4, docs/re/space.md#space-colony-cities) is a plain struct with no vtable (pos at +0x00),
        // so never vcall Release on it: shrink end pointer, clear tail slots, and recompute civ turret/building sums.
        void** b = F<void**>(civ, 0x3C);
        void** e = F<void**>(civ, 0x40);
        if (b && e > b && (size_t)(e - b) == cities.size()) {
            for (size_t i = want; i < cities.size(); i++)
                b[i] = nullptr;
            F<void**>(civ, 0x40) = b + want;
            recompute_civ_city_totals(civ);
        }
        return;
    }
    if (cities.size() == want || !a_GameAlloc || !a_CityName || !a_CityCtor || !a_AddCityData)
        return;
    void* tmpl = cities.empty() ? nullptr : cities[0];
    for (size_t i = cities.size(); i < want; i++) {
        void* mem = cdecl_call<void*>(a_GameAlloc, 0xC4u, (void*)a_CityName, 0, 0, 0, 0);
        if (!mem)
            break;
        void* c = thiscall<void*>(a_CityCtor, mem);
        if (!readable(c, 0xC4))
            break;
        // Distribute cities evenly around the planet sphere
        float angle = (float)i * 2.0943951f; // ~120 deg spacing
        float lat = (i % 2 == 0) ? 0.35f : -0.35f;
        float rad = 160.0f;
        float cx = rad * std::cos(lat) * std::cos(angle);
        float cy = rad * std::cos(lat) * std::sin(angle);
        float cz = rad * std::sin(lat);
        F<float>(c, 0x00) = cx;
        F<float>(c, 0x04) = cy;
        F<float>(c, 0x08) = cz;
        if (g_city_orient_fix) {
            float q[4];
            city_surface_quat(cx, cy, cz, q);
            F<float>(c, 0x0C) = q[0];
            F<float>(c, 0x10) = q[1];
            F<float>(c, 0x14) = q[2];
            F<float>(c, 0x18) = q[3];
        } else {
            F<float>(c, 0x0C) = 0.0f;
            F<float>(c, 0x10) = 0.0f;
            F<float>(c, 0x14) = 0.0f;
            F<float>(c, 0x18) = 1.0f;
        }
        F<int32_t>(c, 0x20) = tmpl && readable(tmpl, 0x38) ? std::max(4, F<int32_t>(tmpl, 0x20)) : 8;
        F<int32_t>(c, 0x24) = tmpl && readable(tmpl, 0x38) ? std::max(1, F<int32_t>(tmpl, 0x24)) : 4;
        F<int32_t>(c, 0x2C) = tmpl && readable(tmpl, 0x38) ? std::max(60, F<int32_t>(tmpl, 0x2C)) : 120;
        F<float>(c, 0x30) = 5.0f;
        F<uint32_t>(c, 0xB8) = tmpl && readable(tmpl, 0xBC) ? F<uint32_t>(tmpl, 0xB8) : 0x280852F1u;
        thiscall<void>(a_AddCityData, civ, c);
    }
}

// ---- BUG-1582: Emigrated guest Space tutorial / starter drives / Colony Pack unlock ----
struct ToolKey3 {
    uint32_t v[3] = {0, 0, 0};
};

static ToolKey3 sr_tool_key_for(void* inv, uint32_t hash)
{
    ToolKey3 tk{{hash, 0u, 0u}};
    if (!readable(inv, 0x10))
        return tk;
    int n = vcall<int>(inv, 0x5C / 4);
    for (int i = 0; i < n && i < 256; i++) {
        uint8_t* it = vcall<uint8_t*>(inv, 0x60 / 4, i);
        if (!it || F<int>(it, 0x14) != 2)
            continue;
        const uint32_t* k = vcall<const uint32_t*>(it, 0x18 / 4);
        if (k) {
            tk.v[1] = k[1];
            tk.v[2] = k[2];
            break;
        }
    }
    return tk;
}

static bool sr_has_tool(void* inv, uint32_t hash)
{
    if (!readable(inv, 0x10))
        return false;
    ToolKey3 tk = sr_tool_key_for(inv, hash);
    return vcall<bool>(inv, 0x74 / 4, tk.v);
}

template <class Pred>
static bool sr_has_tool_matching(void* inv, Pred pred)
{
    if (!readable(inv, 0x10))
        return false;
    int n = vcall<int>(inv, 0x5C / 4);
    for (int i = 0; i < n && i < 256; i++) {
        uint8_t* it = vcall<uint8_t*>(inv, 0x60 / 4, i);
        if (!it || F<int>(it, 0x14) != 2)
            continue;
        const uint32_t* k = vcall<const uint32_t*>(it, 0x18 / 4);
        if (k && pred(k[0]))
            return true;
    }
    return false;
}

static bool sr_grant_tool(void* inv, uint32_t hash)
{
    if (!readable(inv, 0x10) || !a_AddItemByKey)
        return false;
    ToolKey3 tk = sr_tool_key_for(inv, hash);
    if (vcall<bool>(inv, 0x74 / 4, tk.v))
        return false;
    thiscall<void>(a_AddItemByKey, inv, tk.v);
    return vcall<bool>(inv, 0x74 / 4, tk.v);
}

static void apply_emig_unlocks()
{
    if (!g_emig_unlock_fix || !active() || spaceext::is_host() || !g_emig_sm.home_created)
        return;
    uint32_t epoch = spaceext::world_epoch();
    uint8_t* mm = a_MissionMgr ? cdecl_call<uint8_t*>(a_MissionMgr) : nullptr;
    if (!readable(mm, 0x20))
        mm = nullptr;
    void* sg = a_SpaceGame ? cdecl_call<void*>(a_SpaceGame) : nullptr;
    void* inv = readable(sg, 0x44) ? F<void*>(sg, 0x40) : nullptr;
    if (!readable(inv, 0x10))
        inv = nullptr;

    bool first_comm = mm && mm[0x16] != 0;
    bool flight_school = mm && mm[0x17] != 0;
    bool mission201 = mm && mm[0x18] != 0;
    bool has_interplanetary = inv && sr_has_tool(inv, spacelogic::kToolInterplanetaryDrive);
    bool has_interstellar = inv && sr_has_tool_matching(inv, spacelogic::is_interstellar_tool);
    bool has_scan = inv && sr_has_tool(inv, spacelogic::kToolScan);
    bool has_laser = inv && sr_has_tool_matching(inv, spacelogic::is_laser_tool);
    bool has_abduct = inv && sr_has_tool(inv, spacelogic::kToolAbduct);
    bool has_drop_cargo = inv && sr_has_tool(inv, spacelogic::kToolDropCargo);
    bool has_health = inv && sr_has_tool_matching(inv, spacelogic::is_health_tool);
    bool has_energy = inv && sr_has_tool_matching(inv, spacelogic::is_energy_tool);
    bool has_colony = inv && sr_has_tool(inv, kToolPlaceColony);

    EmigUnlockPlan plan = emigration_unlock_plan(
        g_emig_unlock_fix, active(), spaceext::is_host(), g_emig_sm.home_created, spaceext::hold_grants(),
        mm != nullptr, inv != nullptr, first_comm, flight_school, mission201, has_interplanetary, has_interstellar,
        has_scan, has_laser, has_abduct, has_drop_cargo, has_health, has_energy, has_colony,
        g_emig_colony_pack_epoch == epoch);
    if (!plan.any()) {
        if (mm && inv && !spaceext::hold_grants())
            g_emig_unlock_epoch = epoch;
        return;
    }

    uint32_t applied = 0;
    if (mm) {
        if (plan.set_first_comm) {
            mm[0x16] = 1;
            applied++;
        }
        if (plan.set_flight_school) {
            if (a_SetFlightSchool)
                thiscall<void>(a_SetFlightSchool, mm, (uint32_t)1);
            else
                mm[0x17] = 1;
            applied++;
        }
        if (plan.set_mission201) {
            if (a_SetMission201)
                thiscall<void>(a_SetMission201, mm, (uint32_t)1);
            else
                mm[0x18] = 1;
            applied++;
        }
    }
    if (inv && !spaceext::hold_grants()) {
        if (plan.grant_interplanetary && sr_grant_tool(inv, spacelogic::kToolInterplanetaryDrive))
            applied++;
        if (plan.grant_interstellar && sr_grant_tool(inv, spacelogic::kToolInterstellarDrive))
            applied++;
        if (plan.grant_scan && sr_grant_tool(inv, spacelogic::kToolScan))
            applied++;
        if (plan.grant_laser && sr_grant_tool(inv, spacelogic::kToolLaser1))
            applied++;
        if (plan.grant_abduct && sr_grant_tool(inv, spacelogic::kToolAbduct))
            applied++;
        if (plan.grant_drop_cargo && sr_grant_tool(inv, spacelogic::kToolDropCargo))
            applied++;
        if (plan.grant_health && sr_grant_tool(inv, spacelogic::kToolHealth1))
            applied++;
        if (plan.grant_energy && sr_grant_tool(inv, spacelogic::kToolEnergy1))
            applied++;
        if (plan.grant_place_colony && sr_grant_tool(inv, kToolPlaceColony)) {
            g_emig_colony_pack_epoch = epoch;
            applied++;
        } else if (has_colony) {
            g_emig_colony_pack_epoch = epoch;
        }
        if (mm)
            g_emig_unlock_epoch = epoch;
    }
    if (applied > 0) {
        g_emig_unlocks_applied += applied;
        if (H && M)
            H->log(M, SK_INFO,
                   "space_rivals: unlocked emigrated guest travel/starter kit (+%u items, colony_pack=%d, BUG-1582)",
                   applied, (int)(g_emig_colony_pack_epoch == epoch));
    }
}

static void apply_emig_unlocks_task(void*)
{
    g_emig_unlock_pending = false;
    apply_emig_unlocks();
}

// ---- BUG-1582: Sweep leftover Civ Rivals vehicles & unify rival Civ cities on Host's homeworld ----
static void host_cleanup_homeworld_civ_leftovers_task(void*)
{
    g_home_cleanup_pending = false;
    if (!g_home_cleanup_fix || !active() || !spaceext::is_host() || world::cinematic_playing())
        return;
    if (context() != 0)
        return;
    void* emp = player_empire();
    if (!readable(emp, 0xB8))
        return;
    uint32_t hp = F<uint32_t>(emp, 0xB4);
    uint32_t ap = active_planet_id();
    if (!hp || hp == 0xFFFFFFFFu || ap != hp)
        return;
    uint32_t mp = my_pol();
    if (!mp || mp == 0xFFFFFFFFu)
        return;
    void* nm = a_NounMgr ? cdecl_call<void*>(a_NounMgr) : nullptr;
    if (!nm)
        return;

    uint32_t veh_removed = 0;
    if (a_GetAllVehicles && (a_VehicleRemove || a_DestroyInstance)) {
        void* vvec = thiscall<void*>(a_GetAllVehicles, nm);
        auto vehs = ptr_vec(vvec, 256);
        for (void* v : vehs) {
            if (!readable(v, 0x34))
                continue;
            uint32_t vp = F<uint32_t>(v, 0x30);
            if (!should_cleanup_civ_vehicle(vp, mp))
                continue;
            if (a_VehicleRemove)
                thiscall<void>(a_VehicleRemove, v, (uint32_t)0);
            else if (a_DestroyInstance)
                thiscall<void>(a_DestroyInstance, nm, v);
            veh_removed++;
        }
    }

    uint32_t cities_unified = 0;
    // cCity::ChangeOwner (0x00BE8A20) unconditionally dereferences oldCiv = [city+0x590] and
    // newCiv = GetCivilizationByPoliticalID(nm, mp) inside cCivilization::AddCity.
    void* host_civ = a_CivByPol ? thiscall<void*>(a_CivByPol, nm, mp) : nullptr;
    if (a_GetAllCities && a_CityChangeOwner && host_civ) {
        callguard::Hold guard(g_emigrating_guard);
        void* cvec = thiscall<void*>(a_GetAllCities, nm);
        auto cities = ptr_vec(cvec, 64);
        for (void* c : cities) {
            if (!readable(c, 0x594))
                continue;
            uint32_t cp = F<uint32_t>(c, 0x30);
            if (!should_unify_civ_city(cp, mp))
                continue;
            if (!F<void*>(c, 0x590))
                continue;
            // Set city+0x2E2 so ChangeOwner (0x00BE8E72) runs City_SetPartsOwner (0x00BE4C80)
            // and switches buildings and turrets away from the rival polity.
            F<uint8_t>(c, 0x2E2) = 1;
            thiscall<void>(a_CityChangeOwner, c, mp, 0, 0);
            if (a_CitySetPlayerOwned)
                thiscall<void>(a_CitySetPlayerOwned, c, (uint32_t)1);
            cities_unified++;
        }
    }

    // Ensure homeworld cPlanetRecord has a host cCivData without creating duplicate pol == mp entries.
    // In Space mode, ChangeOwner (0x00BE8AAB -> 0x00BD9130) already moves cCityData into the host cCivData,
    // leaving emptied foreign entries behind; overwriting those with mp would duplicate pol == mp.
    if (void* rec = planet_record(hp); readable(rec, 0x168)) {
        auto civs = ptr_vec((uint8_t*)rec + 0x15C, 32);
        bool has_host_civ_data = false;
        void* adopt_cand = nullptr;
        size_t adopt_cities = 0;
        for (void* cd : civs) {
            if (!readable(cd, 0x48))
                continue;
            uint32_t cd_pol = F<uint32_t>(cd, 0x14);
            if (cd_pol == mp) {
                has_host_civ_data = true;
                break;
            }
            if (should_unify_civ_city(cd_pol, mp)) {
                size_t nc = ptr_vec((uint8_t*)cd + 0x3C, 64).size();
                if (nc > 0 && (!adopt_cand || nc > adopt_cities)) {
                    adopt_cand = cd;
                    adopt_cities = nc;
                }
            }
        }
        if (!has_host_civ_data && adopt_cand)
            F<uint32_t>(adopt_cand, 0x14) = mp;
    }

    g_civ_vehicles_swept += veh_removed;
    g_civ_cities_unified += cities_unified;
    double now = now_s();
    if (veh_removed || cities_unified) {
        if (H && M)
            H->log(M, SK_INFO,
                   "space_rivals: cleaned up host homeworld Civ leftovers (vehicles_removed=%u cities_unified=%u, BUG-1582)",
                   veh_removed, cities_unified);
    } else if (g_home_cleanup_start > 0.0 && now - g_home_cleanup_start >= 4.0) {
        g_home_cleanup_done = true;
    }
}

static void clear_loaded_planet_pol(void* rec)
{
    void* sm = star_mgr();
    if (!sm || !rec)
        return;
    for (void* p : ptr_vec((uint8_t*)sm + 0x50, 64))
        if (readable(p, 0x140) && F<void*>(p, 0x13C) == rec && readable((uint8_t*)p + 0xD4, 0x34))
            vcall<void>((uint8_t*)p + 0xD4, 0x48 / 4, 0xFFFFFFFFu); // cGameData::SetPoliticalID
}

static void rebuild_near_stars()
{
    if (!a_NearRebuild || !a_NearVt || !a_SpaceGame || !spaceext::in_space())
        return;
    void* sg = cdecl_call<void*>(a_SpaceGame);
    void* ns = readable(sg, 0x34) ? F<void*>(sg, 0x30) : nullptr;
    if (readable(ns, 0x84) && F<uintptr_t>(ns, 0) == a_NearVt)
        thiscall<void>(a_NearRebuild, ns);
}

static bool relinquish_host_home_now()
{
    if (!g_my_emig.valid() || !g_my_emig.orig_home_star || !g_my_emig.orig_home_planet)
        return true; // nothing to relinquish
    void* emp = player_empire();
    void* orig_star = spacecoop::star_record(g_my_emig.orig_home_star);
    void* orig_rec = planet_record(g_my_emig.orig_home_planet);
    if (!emp || !orig_star || !orig_rec)
        return false;
    if (context() == 0 && active_planet_id() == g_my_emig.orig_home_planet)
        return false; // deferred until liftoff off host's planet surface
    callguard::Hold emig_guard(g_emigrating_guard);
    erase_from_vec((uint8_t*)emp + 0x88, orig_star);
    remove_colony_from_spd(orig_rec);
    F<uint32_t>(orig_star, 0x54) = 0xFFFFFFFFu;
    F<uint32_t>(orig_star, 0x5C) &= ~0x7u;
    if (g_relinquish_clean_fix) {
        // BUG-1567: lower tech 5 -> 1 on orig_star and its tech-5 planets directly (without calling
        // cPlanetRecord_SetTechLevel(1), which would destroy cCivData needed by space_allies::add_planet's
        // keep_host_home path when space.allies is enabled), clear loaded cPlanet pol, and rebuild near-stars.
        for (void* pr : planets_of(orig_star)) {
            if (!readable(pr, 0x198))
                continue;
            if (F<uint32_t>(pr, 0x194) == 5u) {
                if (pr != orig_rec)
                    remove_colony_from_spd(pr);
                F<uint32_t>(pr, 0x194) = kRelinquishedTechLevel;
                clear_loaded_planet_pol(pr);
            }
        }
        if (F<uint32_t>(orig_star, 0x4C) == 5u)
            F<uint32_t>(orig_star, 0x4C) = kRelinquishedTechLevel;
        rebuild_near_stars();
    }
    sanitize_planet_city_poses(orig_rec);
    g_relinquished_host_home_star = g_my_emig.orig_home_star;
    g_relinquished_host_home_planet = g_my_emig.orig_home_planet;
    g_homes.set(0, g_my_emig.orig_home_star, g_my_emig.orig_home_planet);
    g_emig_sm.host_relinquished = true;
    if (H && M)
        H->log(M, SK_INFO,
               "space_rivals: guest relinquished host home star %u planet %08X for rival mirror (clean=%d star_tech=%u planet_tech=%u)",
               g_my_emig.orig_home_star, g_my_emig.orig_home_planet, (int)g_relinquish_clean_fix,
               F<uint32_t>(orig_star, 0x4C), F<uint32_t>(orig_rec, 0x194));
    return true;
}

static void teleport_to_new_home_now()
{
    if (!g_my_emig.valid() || !g_emig_sm.home_created || !g_emig_sm.host_relinquished || g_emig_sm.teleported)
        return;
    uint32_t act_sid = active_star_id();
    int ctx = context();
    if (act_sid == g_my_emig.orig_home_star) {
        if (ctx == 0)
            return; // still landed; retry after liftoff
        if (ctx == 2)
            spacecoop::teleport_in_galaxy(g_my_emig.new_home_star);
        else
            spacecoop::travel_to_star(g_my_emig.new_home_star);
        g_emig_sm.teleported = true;
        if (H && M)
            H->log(M, SK_INFO, "space_rivals: guest jumped from host star %u to emigrated home star %u (ctx=%d)",
                   g_my_emig.orig_home_star, g_my_emig.new_home_star, ctx);
    } else if (act_sid != 0) {
        g_emig_sm.teleported = true;
    }
}

static void apply_guest_emigration_task(void*)
{
    g_my_emig_pending = false;
    if (!active() || spaceext::is_host() || !g_my_emig.valid())
        return;
    if (own_world::space_other())
        return;
    void* emp = player_empire();
    if (!readable(emp, 0xD4))
        return;
    uint32_t epoch = spaceext::world_epoch();
    uint32_t cur_hp = F<uint32_t>(emp, 0xB4);

    // Phase 1: Create / re-apply guest's emigrated homeworld (on first arrival or rejoin reload).
    if (g_emig_sm.needs_reapply(epoch, g_my_emig.new_home_planet, cur_hp)) {
        void* new_star = spacecoop::star_record(g_my_emig.new_home_star);
        void* new_rec = planet_record(g_my_emig.new_home_planet);
        if (!new_star || !new_rec || !a_AddStar || !a_PlanetTech || !a_FillPlanet || !a_AddPlanet || !a_SetHomePlanet)
            return;

        callguard::Hold emig_guard(g_emigrating_guard);
        uint32_t mp = my_pol();

        // Ensure habitable T3 homeworld scores if the candidate started at lower T-score.
        if (F<int32_t>(new_rec, 0x28) < 5) {
            F<int32_t>(new_rec, 0x28) = 5;
            F<float>(new_rec, 0xB0) = 0.5f;
            F<float>(new_rec, 0xB4) = 0.5f;
            F<float>(new_rec, 0xB8) = 0.5f;
        }

        // Claim the new star & colonize the new homeworld at tech 5 with target_cities.
        F<uint32_t>(new_star, 0x54) = mp;
        thiscall<void>(a_AddStar, emp, new_star);
        if (F<int32_t>(new_rec, 0x194) != 5) {
            cdecl_call<void>(a_PlanetTech, new_rec, 5);
            cdecl_call<void>(a_FillPlanet, new_rec, 5);
        }
        auto civs = ptr_vec((uint8_t*)new_rec + 0x15C, 32);
        for (void* cd : civs)
            if (readable(cd, 0x18))
                F<uint32_t>(cd, 0x14) = mp;
        ensure_city_count(new_rec, mp, g_my_emig.target_cities);
        thiscall<void>(a_AddPlanet, emp, new_rec);
        thiscall<void>(a_SetHomePlanet, emp, new_rec);
        F<uint32_t>(new_star, 0x5C) |= 0x05u;

        g_homes.set(spaceext::my_id(), g_my_emig.new_home_star, g_my_emig.new_home_planet);
        g_homes.set(0, g_my_emig.orig_home_star, g_my_emig.orig_home_planet);

        g_emig_sm.mark_home_created(epoch, g_my_emig.new_home_planet);
        g_my_emig.applied = true;

        std::string old_n = g_my_emig.old_planet_name;
        std::string new_n = str16((uint8_t*)new_rec + 0x18);
        if (new_n.empty())
            new_n = g_my_emig.new_planet_name;
        g_last_emig_notice = format_emigration_notice(old_n, new_n, spaceext::russian());
        spaceext::notice(g_last_emig_notice);

        send_emig_msg(0, EMIG_ACK, g_my_emig);
        if (H && M)
            H->log(M, SK_INFO, "space_rivals: emigrated homeworld to star %u planet %08X '%s' (%u cities)",
                   g_my_emig.new_home_star, g_my_emig.new_home_planet, new_n.c_str(),
                   (unsigned)count_cities_on(new_rec, mp));
    }

    // Phase 2: Relinquish host's original homeworld once off host's planet surface (deferred on Civ -> Space start).
    if (g_emig_sm.home_created && !g_emig_sm.host_relinquished) {
        if (!relinquish_host_home_now())
            return; // still on host planet surface; tick() will retry after liftoff
    }

    // Phase 3: Jump away from host's star to our new homeworld once relinquished and lifted off (BUG-1555).
    if (g_emig_sm.home_created && g_emig_sm.host_relinquished && !g_emig_sm.teleported)
        teleport_to_new_home_now();

    // BUG-1582: unlock tutorial travel gates, starter drives/tools, and starter Colony Pack for emigrated guest.
    if (g_emig_sm.home_created)
        apply_emig_unlocks();
}

// ---- Space NPC Empire Influence (TurnAgainst / Protect / SmoothOver) ----
static bool apply_local_npc_shift(uint32_t npc_pol, bool hostile, const std::string& who_notice)
{
    if (!active() || !a_ApplyRel || !rel_mgr() || !is_npc_empire_pol(npc_pol))
        return false;
    void* emp = empire_of(npc_pol);
    if (!emp)
        return false;
    callguard::Hold hold(g_applying_influence);
    if (a_MakeContact)
        thiscall<void>(a_MakeContact, emp, 0);
    uint32_t mp = my_pol();
    float before = a_CalcRel ? thiscall<float>(a_CalcRel, rel_mgr(), npc_pol, mp, 0) : 0.0f;
    uint32_t ev = hostile ? hostile_influence_event(g_inf_seq++) : friendly_influence_event(g_inf_seq++);
    thiscall<float>(a_ApplyRel, rel_mgr(), npc_pol, mp, ev, 1.0f);
    float after = a_CalcRel ? thiscall<float>(a_CalcRel, rel_mgr(), npc_pol, mp, 0) : before;
    // If that event ID was already capped, apply the next distinct event ID so the shift takes effect.
    if (std::fabs(after - before) < 0.5f) {
        uint32_t ev2 = hostile ? hostile_influence_event(g_inf_seq++) : friendly_influence_event(g_inf_seq++);
        thiscall<float>(a_ApplyRel, rel_mgr(), npc_pol, mp, ev2, 1.0f);
        after = a_CalcRel ? thiscall<float>(a_CalcRel, rel_mgr(), npc_pol, mp, 0) : before;
    }
    g_inf_applied++;
    if (!who_notice.empty())
        spaceext::notice(who_notice);
    if (H && M)
        H->log(M, SK_INFO, "space_rivals: NPC influence %s on %08X '%s': rel %.1f -> %.1f",
               hostile ? "TurnAgainst" : "SmoothOver", npc_pol, str16((uint8_t*)emp + 0x3C).c_str(), before, after);
    return true;
}

static bool on_npc_influence(rivals::Stage stage, rivals::InfluenceAction action, uint8_t actor, uint32_t npc_id,
                             uint8_t rival_id)
{
    if (!g_space_rivals_fix || stage != rivals::Stage::Space)
        return false;
    if (!is_npc_empire_pol(npc_id))
        return false;
    std::string ename = str16((uint8_t*)empire_of(npc_id) + 0x3C);
    if (ename.empty())
        ename = "NPC";

    if (action == rivals::InfluenceAction::Protect)
        return true; // tracked & enforced by rivals.cpp

    if (action == rivals::InfluenceAction::TurnAgainst) {
        if (!rivals::valid_player(rival_id) || rival_id == actor)
            return false;
        if (rival_id == 0) {
            std::string msg = spaceext::name_of(actor) +
                              spaceext::tr(" turned ", " настроил(а) ") + ename +
                              spaceext::tr(" against you!", " против вас!");
            return apply_local_npc_shift(npc_id, true, msg);
        }
        net::Writer w;
        w.u8((uint8_t)NPCREL_TURN_AGAINST);
        w.u8(actor);
        w.u32(npc_id);
        write_i32(w, -1);
        write_str64(w, ename);
        return rivals::send_faction_to(rival_id, rivals::Stage::Space, wire::rivals_faction::SK_NPC_RELS, w);
    }

    if (action == rivals::InfluenceAction::SmoothOver) {
        if (actor == 0) {
            std::string msg = spaceext::tr("Smoothed over relations with ", "Отношения улучшены: ") + ename;
            return apply_local_npc_shift(npc_id, false, msg);
        }
        net::Writer w;
        w.u8((uint8_t)NPCREL_SMOOTH_OVER);
        w.u8(actor);
        w.u32(npc_id);
        write_i32(w, 1);
        write_str64(w, ename);
        return rivals::send_faction_to(actor, rivals::Stage::Space, wire::rivals_faction::SK_NPC_RELS, w);
    }
    return false;
}

static bool on_buy_influence_space(uint8_t actor, int32_t price, int32_t /*gain*/)
{
    if (!g_space_rivals_fix || !active())
        return false;
    int32_t cost = influence_buy_space_cost(price);
    if (cost <= 0)
        return true;
    if (actor == 0) {
        void* emp = player_empire();
        if (!readable(emp, 0xD4) || !a_AddMoney)
            return false;
        int32_t cur = F<int32_t>(emp, 0xD0);
        if (cur < cost)
            return false;
        thiscall<void>(a_AddMoney, emp, -cost);
        return true;
    }
    int32_t cur = 0;
    if (auto it = g_guest_money.find(actor); it != g_guest_money.end())
        cur = it->second;
    else if (const spacemap::Roster* r = spacemap::roster_of(actor))
        cur = r->money;
    if (cur < cost)
        return false;
    g_guest_money[actor] = cur - cost;
    net::Writer w;
    w.u8((uint8_t)OP_CHARGE_MONEY);
    write_i32(w, cost);
    return rivals::send_faction_to(actor, rivals::Stage::Space, wire::rivals_faction::SK_STAGE_CUSTOM, w);
}

void on_npc_diplomacy_event(uint32_t npc_pol, uint32_t ev_id, float scale)
{
    if (!active() || g_applying_influence || scale <= 0.0f || !is_npc_empire_pol(npc_pol))
        return;
    std::string reason;
    int32_t gain = influence_gain_for_event(ev_id, &reason);
    if (gain <= 0)
        return;
    double now = now_s();
    auto it = g_inf_earn_cd.find(npc_pol);
    if (it != g_inf_earn_cd.end() && now - it->second < 5.0)
        return;
    g_inf_earn_cd[npc_pol] = now;
    if (spaceext::is_host()) {
        rivals::grant_influence(0, gain, reason);
    } else {
        net::Writer w;
        w.u8((uint8_t)NPCREL_EARN_INF);
        w.u8(spaceext::my_id());
        w.u32(npc_pol);
        write_i32(w, gain);
        write_str64(w, reason);
        rivals::send_faction_to(0, rivals::Stage::Space, wire::rivals_faction::SK_NPC_RELS, w);
    }
}

// ---- Chronicle Galaxy Core Finale ----
static void display_finale_summary()
{
    std::string names[rivals::kMaxPlayers];
    bool present[rivals::kMaxPlayers] = {};
    for (int i = 0; i < rivals::kMaxPlayers; i++)
        names[i] = spaceext::name_of((uint8_t)i);
    if (spaceext::connected()) {
        for (const auto& p : spaceext::ctx().sess->players())
            if (rivals::valid_player(p.id)) {
                present[p.id] = true;
                names[p.id] = p.name;
            }
    }
    g_finale_summary = g_finale.format_summary(names, present, spaceext::russian());
    if (!g_finale_summary.empty()) {
        spaceext::notice(g_finale_summary);
        if (H && M)
            H->log(M, SK_INFO, "space_rivals: %s", g_finale_summary.c_str());
    }
}

void on_core_entered(uint8_t player_id)
{
    if (!active() || !rivals::valid_player(player_id))
        return;
    if (spaceext::is_host()) {
        if (!g_finale.claim_first(player_id))
            return;
        rivals::add_chronicle_points(player_id, kPtsCoreFinale, "galaxy core finale");
        g_finale.snapshot_chronicle(rivals::chronicle_state());
        display_finale_summary();
        send_finale_summary();
    } else if (player_id == spaceext::my_id()) {
        net::Writer w;
        w.u8((uint8_t)OP_CORE_ENTERED);
        w.u8(player_id);
        rivals::send_faction_to(0, rivals::Stage::Space, wire::rivals_faction::SK_STAGE_CUSTOM, w);
    }
}

// ---- Space Race Stats Collection ----
static SpaceRaceStats read_local_stats()
{
    SpaceRaceStats s;
    void* emp = player_empire();
    if (!readable(emp, 0xD4))
        return s;
    s.money = F<int32_t>(emp, 0xD0);
    auto stars = ptr_vec((uint8_t*)emp + 0x88, 1024);
    s.stars = (uint16_t)stars.size();
    if (const spacemap::Roster* r = spacemap::roster_of(spaceext::my_id()))
        s.colonies = (uint16_t)r->colonies.size();
    if (s.colonies == 0 && s.stars > 0)
        s.colonies = 1;
    s.allied_npcs = count_allied_npcs();
    s.dist_to_core = calc_dist_to_core();
    return s;
}

static void update_race_progress(double now)
{
    if (!active() || !spaceext::connected())
        return;
    if (now < g_next_progress_send)
        return;
    g_next_progress_send = now + 2.0;

    SpaceRaceStats s = read_local_stats();
    uint8_t me = spaceext::my_id();
    g_race_stats[me] = s;

    bool changed = !g_have_sent_stats || s.money != g_last_sent_stats.money ||
                   s.colonies != g_last_sent_stats.colonies || s.stars != g_last_sent_stats.stars ||
                   s.allied_npcs != g_last_sent_stats.allied_npcs ||
                   std::fabs(s.dist_to_core - g_last_sent_stats.dist_to_core) >= 1.0f;

    if (spaceext::is_host()) {
        if (changed) {
            g_last_sent_stats = s;
            g_have_sent_stats = true;
            rivals::set_player_progress(0, build_player_progress(0, s));
        }
    } else if (changed) {
        g_last_sent_stats = s;
        g_have_sent_stats = true;
        void* emp = player_empire();
        uint32_t hs = readable(emp, 0xB8) ? F<uint32_t>(emp, 0xB0) : 0;
        uint32_t hp = readable(emp, 0xB8) ? F<uint32_t>(emp, 0xB4) : 0;
        net::Writer w;
        write_i32(w, s.money);
        w.u16(s.colonies);
        w.u16(s.stars);
        w.u16(s.allied_npcs);
        w.f32(s.dist_to_core);
        w.u32(hs == 0xFFFFFFFFu ? 0u : hs);
        w.u32(hp == 0xFFFFFFFFu ? 0u : hp);
        rivals::send_faction_to(0, rivals::Stage::Space, wire::rivals_faction::SK_PROGRESS, w);
    }
}

// ---- MSG_RIVALS_FACTION (Stage::Space) Dispatcher ----
static void on_faction_msg(uint8_t from, uint8_t subkind, net::Reader& r)
{
    if (!g_space_rivals_fix)
        return;
    using namespace wire::rivals_faction;

    if (subkind == SK_EMIGRATE) {
        uint8_t kind = r.u8();
        uint8_t pid = r.u8();
        EmigrationRecord rec;
        rec.player_id = pid;
        rec.orig_home_star = r.u32();
        rec.orig_home_planet = r.u32();
        rec.new_home_star = r.u32();
        rec.new_home_planet = r.u32();
        rec.target_cities = clamp_emig_cities((int)r.u16());
        rec.old_planet_name = read_str64(r);
        rec.new_planet_name = read_str64(r);
        if (!r.ok())
            return;

        if (kind == EMIG_ASSIGN && from == 0 && !spaceext::is_host()) {
            if (rec.orig_home_star && rec.orig_home_planet)
                g_homes.set(0, rec.orig_home_star, rec.orig_home_planet);
            if (rec.valid())
                g_homes.set(pid, rec.new_home_star, rec.new_home_planet);
            if (pid == spaceext::my_id() && rec.valid()) {
                g_my_emig = rec;
                if (!g_my_emig_pending) {
                    g_my_emig_pending = true;
                    H->run_outside_frame(apply_guest_emigration_task, nullptr);
                }
            }
        } else if (kind == EMIG_ACK && spaceext::is_host() && from == pid) {
            if (auto it = g_emig_by_id.find(from); it != g_emig_by_id.end())
                it->second.applied = true;
            if (rec.valid())
                g_homes.set(from, rec.new_home_star, rec.new_home_planet);
            send_homes_sync();
        } else if (kind == EMIG_REQ && spaceext::is_host() && rivals::valid_player(from) && from > 0) {
            host_ensure_guest_emigrations();
        }
        return;
    }

    if (subkind == SK_NPC_RELS) {
        uint8_t op = r.u8();
        uint8_t actor = r.u8();
        uint32_t npc_pol = r.u32();
        int32_t val = read_i32(r);
        std::string txt = read_str64(r);
        if (!r.ok())
            return;
        if ((op == NPCREL_TURN_AGAINST || op == NPCREL_SMOOTH_OVER) && from == 0 && !spaceext::is_host()) {
            bool hostile = (op == NPCREL_TURN_AGAINST);
            std::string ename = txt.empty() ? "NPC" : txt;
            std::string notice = hostile ? (spaceext::name_of(actor) + spaceext::tr(" turned ", " настроил(а) ") +
                                            ename + spaceext::tr(" against you!", " против вас!"))
                                         : (spaceext::tr("Smoothed over relations with ", "Отношения улучшены: ") + ename);
            struct ShiftTask {
                uint32_t pol;
                bool hostile;
                std::string notice;
            };
            auto* t = new ShiftTask{npc_pol, hostile, std::move(notice)};
            H->run_outside_frame(
                [](void* u) {
                    auto* st = (ShiftTask*)u;
                    apply_local_npc_shift(st->pol, st->hostile, st->notice);
                    delete st;
                },
                t);
        } else if (op == NPCREL_EARN_INF && spaceext::is_host() && rivals::valid_player(from) && from > 0 && val > 0 &&
                   val <= 100) {
            rivals::grant_influence(from, val, txt.empty() ? "space" : txt);
        }
        return;
    }

    if (subkind == SK_PROGRESS) {
        if (!spaceext::is_host() || !rivals::valid_player(from) || from == 0)
            return;
        SpaceRaceStats s;
        s.money = read_i32(r);
        s.colonies = r.u16();
        s.stars = r.u16();
        s.allied_npcs = r.u16();
        s.dist_to_core = r.f32();
        uint32_t hs = r.u32();
        uint32_t hp = r.u32();
        if (!r.ok())
            return;
        g_race_stats[from] = s;
        g_guest_money[from] = s.money;
        if (hs && hp)
            g_homes.set(from, hs, hp);
        rivals::set_player_progress(from, build_player_progress(from, s));
        return;
    }

    if (subkind == SK_STAGE_CUSTOM) {
        uint8_t op = r.u8();
        if (!r.ok())
            return;
        if (op == OP_CHARGE_MONEY && from == 0 && !spaceext::is_host()) {
            int32_t price = read_i32(r);
            if (!r.ok() || price <= 0)
                return;
            H->run_outside_frame(
                [](void* u) {
                    int32_t pr = (int32_t)(intptr_t)u;
                    void* emp = player_empire();
                    if (readable(emp, 0xD4) && a_AddMoney) {
                        int32_t cur = F<int32_t>(emp, 0xD0);
                        int32_t deduct = std::min(cur > 0 ? cur : 0, pr);
                        if (deduct > 0)
                            thiscall<void>(a_AddMoney, emp, -deduct);
                    }
                },
                (void*)(intptr_t)price);
        } else if (op == OP_GRANT_MONEY && from == 0 && !spaceext::is_host()) {
            int32_t amt = read_i32(r);
            if (!r.ok() || amt <= 0 || amt > 99999999)
                return;
            int64_t sum = (int64_t)g_queued_draft_money + amt;
            g_queued_draft_money = sum > 99999999LL ? 99999999 : (int32_t)sum;
            if (H && M)
                H->log(M, SK_INFO, "space_rivals: queued +%d Space Sporebucks from Civ draft payout", (int)amt);
        } else if (op == OP_FINALE_SUMMARY && from == 0 && !spaceext::is_host()) {
            uint8_t first = r.u8();
            uint8_t win = r.u8();
            int32_t bonus = read_i32(r);
            int32_t totals[rivals::kMaxPlayers] = {};
            for (int i = 0; i < rivals::kMaxPlayers; i++)
                totals[i] = read_i32(r);
            if (!r.ok())
                return;
            g_finale.awarded = true;
            g_finale.first_player = first;
            g_finale.winner = win;
            g_finale.bonus = bonus;
            for (int i = 0; i < rivals::kMaxPlayers; i++)
                g_finale.totals[i] = totals[i];
            display_finale_summary();
        } else if (op == OP_HOMES_SYNC && from == 0 && !spaceext::is_host()) {
            uint8_t n = r.u8();
            if (!r.ok() || n > 4)
                return;
            for (uint8_t i = 0; i < n; i++) {
                uint8_t pid = r.u8();
                uint32_t sid = r.u32();
                uint32_t plid = r.u32();
                if (!r.ok())
                    return;
                g_homes.set(pid, sid, plid);
            }
        } else if (op == OP_CORE_ENTERED && spaceext::is_host()) {
            uint8_t pid = r.u8();
            if (!r.ok() || pid != from)
                return;
            on_core_entered(from);
        }
    }
}

// ---- Per-world persistence (store.cpp: "space.rivals.emig" v1) ----
static constexpr const char* kStoreFeature = "space.rivals.emig";

static void disk_load_if_opened()
{
    if (!g_space_rivals_fix || !spaceext::is_host())
        return;
    pstore::Store* w = store::world();
    uint32_t ser = store::serial();
    if (!w || ser == g_emig_store_serial)
        return;
    g_emig_store_serial = ser;
    const pstore::Record* rec = w->get_world(kStoreFeature);
    if (!rec || rec->ver != 1) {
        if (g_emig_store_world != w->world) {
            g_emig_store.by_key.clear();
            g_emig_store_world = w->world;
        }
        return;
    }
    EmigrationStore loaded;
    if (loaded.decode(rec->data)) {
        g_emig_store = std::move(loaded);
        g_emig_store_world = w->world;
        if (H && M)
            H->log(M, SK_INFO, "space_rivals: loaded %u persisted guest emigration(s) for %s",
                   (unsigned)g_emig_store.by_key.size(), w->world.c_str());
    }
}

static void disk_save(pstore::Store& s, int64_t now)
{
    if (!g_space_rivals_fix || !g_emig_store_serial || g_emig_store_serial != store::serial() || &s != store::world())
        return;
    if (g_emig_store.by_key.empty())
        return;
    std::vector<std::string> owners;
    for (const auto& [k, r] : g_emig_store.by_key)
        owners.push_back(k);
    store::mark_owners(s, "space.rivals.emig.owner", owners, now);
    s.put_world(kStoreFeature, 1, g_emig_store.encode());
}

static void disk_adopt(const std::string& from, const std::string& to)
{
    if (!g_space_rivals_fix)
        return;
    g_emig_store.adopt_key(from, to);
}

static bool g_store_reg = store::add({kStoreFeature, disk_save, disk_adopt, [] { disk_load_if_opened(); }});

// ---- Civ -> Space Draft Payout Delivery (BUG-1555) ----
static void apply_queued_draft_money_task(void*)
{
    g_draft_money_pending = false;
    if (!active() || spaceext::is_host() || g_queued_draft_money <= 0)
        return;
    void* emp = player_empire();
    if (!readable(emp, 0xD4) || !a_AddMoney)
        return;
    int32_t amt = g_queued_draft_money;
    g_queued_draft_money = 0;
    thiscall<void>(a_AddMoney, emp, amt);
    g_draft_payouts++;
    char b[160];
    snprintf(b, sizeof b,
             spaceext::russian() ? "Выплата драфта Цивилизации: +%d спорлингов!"
                                 : "Civ draft payout: +%d Sporebucks!",
             (int)amt);
    spaceext::notice(b);
    if (H && M)
        H->log(M, SK_INFO, "space_rivals: applied +%d Space Sporebucks Civ draft payout", (int)amt);
}

static void host_deliver_civ_draft_payouts()
{
    if (!active() || !spaceext::is_host() || !spaceext::rivals() || !spaceext::connected())
        return;
    const auto& dr = rivals::draft_state();
    // Host payout (wait until spaceext::rivals() so initial split does not halve winner's bonus).
    if (dr.pending_payout[0] > 0 &&
        (dr.pending_payout_stage[0] == rivals::Stage::None || dr.pending_payout_stage[0] == rivals::Stage::Civ)) {
        void* emp = player_empire();
        if (readable(emp, 0xD4) && a_AddMoney) {
            int32_t raw = rivals::take_pending_draft_payout_for(rivals::Stage::Civ, 0);
            int32_t amt = space_draft_payout_money(raw);
            if (amt > 0) {
                thiscall<void>(a_AddMoney, emp, amt);
                g_draft_payouts++;
                char b[160];
                snprintf(b, sizeof b,
                         spaceext::russian() ? "Выплата драфта Цивилизации: +%d спорлингов!"
                                             : "Civ draft payout: +%d Sporebucks!",
                         (int)amt);
                spaceext::notice(b);
                if (H && M)
                    H->log(M, SK_INFO, "space_rivals: delivered Civ draft payout raw=%d (+%d Sporebucks) to host",
                           (int)raw, (int)amt);
            }
        }
    }
    // Guest payouts (deliver once guest is connected and present in Space).
    for (const auto& p : spaceext::ctx().sess->players()) {
        if (p.id == 0 || !rivals::valid_player(p.id))
            continue;
        if (dr.pending_payout[p.id] <= 0)
            continue;
        if (dr.pending_payout_stage[p.id] != rivals::Stage::None &&
            dr.pending_payout_stage[p.id] != rivals::Stage::Civ)
            continue;
        if (!rivals::peer_reads_faction(p.id))
            continue;
        bool guest_ready = (g_emig_by_id.count(p.id) && g_emig_by_id[p.id].applied) || spacemap::roster_of(p.id) != nullptr;
        if (!guest_ready)
            continue;
        int32_t raw = rivals::take_pending_draft_payout_for(rivals::Stage::Civ, p.id);
        int32_t amt = space_draft_payout_money(raw);
        if (amt <= 0)
            continue;
        net::Writer w;
        w.u8((uint8_t)OP_GRANT_MONEY);
        write_i32(w, amt);
        if (rivals::send_faction_to(p.id, rivals::Stage::Space, wire::rivals_faction::SK_STAGE_CUSTOM, w)) {
            g_draft_payouts++;
            if (H && M)
                H->log(M, SK_INFO, "space_rivals: sent Civ draft payout raw=%d (+%d Sporebucks) to player %u",
                       (int)raw, (int)amt, (unsigned)p.id);
        }
    }
}

// ---- Tick & Session Lifecycle ----
static void tick(double now)
{
    if (!g_space_rivals_fix || !spaceext::connected())
        return;
    if (spaceext::is_host())
        disk_load_if_opened();
    if (!active())
        return;

    refresh_homes_from_rosters();

    // Ensure Space rivalry mode stays synced with rivals.h Space stage mode.
    if (spaceext::is_host() && !spaceext::rivals() && player_empire())
        rivals::sync_space_rivalry();

    // Deliver pending Civ -> Space draft payouts once Space Rivals is active.
    if (spaceext::is_host()) {
        host_deliver_civ_draft_payouts();
    } else if (g_queued_draft_money > 0 && !g_draft_money_pending && spaceext::rivals() && player_empire() &&
               (!g_my_emig.valid() || g_emig_sm.home_created)) {
        g_draft_money_pending = true;
        H->run_outside_frame(apply_queued_draft_money_task, nullptr);
    }

    // Host assigns & syncs guest emigrations and protected homeworlds periodically.
    if (spaceext::is_host() && now >= g_next_host_sync) {
        g_next_host_sync = now + 2.5;
        host_ensure_guest_emigrations();
        send_homes_sync();
    }

    // BUG-1582: Host Civ -> Space homeworld cleanup (remove leftover foreign Civ vehicles and unify rival Civ cities).
    if (g_home_cleanup_fix && spaceext::is_host() && !world::cinematic_playing() && !g_home_cleanup_pending &&
        context() == 0) {
        uint32_t epoch = spaceext::world_epoch();
        if (epoch != g_home_cleanup_epoch) {
            g_home_cleanup_epoch = epoch;
            g_home_cleanup_start = 0.0;
            g_home_cleanup_last = 0.0;
            g_home_cleanup_done = false;
        }
        void* emp = player_empire();
        uint32_t hp = readable(emp, 0xB8) ? F<uint32_t>(emp, 0xB4) : 0;
        if (!g_home_cleanup_done && hp && hp != 0xFFFFFFFFu && active_planet_id() == hp) {
            if (g_home_cleanup_start <= 0.0)
                g_home_cleanup_start = now;
            if (now - g_home_cleanup_start > 20.0) {
                g_home_cleanup_done = true;
            } else if (now - g_home_cleanup_last >= 1.0) {
                g_home_cleanup_last = now;
                g_home_cleanup_pending = true;
                H->run_outside_frame(host_cleanup_homeworld_civ_leftovers_task, nullptr);
            }
        }
    }

    // Guest: drive 3-phase emigration state machine (CreateHome -> RelinquishHost -> TeleportHome),
    // ensuring deferred post-liftoff retry runs both relinquishing and jumping to the new homeworld (BUG-1555).
    if (!spaceext::is_host() && g_my_emig.valid() && !g_my_emig_pending) {
        uint32_t epoch = spaceext::world_epoch();
        void* emp = player_empire();
        uint32_t cur_hp = readable(emp, 0xB8) ? F<uint32_t>(emp, 0xB4) : 0;
        if (g_emig_sm.needs_task(epoch, g_my_emig, cur_hp, context(), active_planet_id(), active_star_id())) {
            g_my_emig_pending = true;
            H->run_outside_frame(apply_guest_emigration_task, nullptr);
        } else if (g_emig_unlock_fix && g_emig_sm.home_created && g_emig_unlock_epoch != epoch &&
                   !g_emig_unlock_pending && !spaceext::hold_grants()) {
            g_emig_unlock_pending = true;
            H->run_outside_frame(apply_emig_unlocks_task, nullptr);
        }
    }

    // Passive influence trickle from allied NPC empires (11.4: small amount over time while allied).
    if (g_last_trickle <= 0.0)
        g_last_trickle = now;
    if (now - g_last_trickle >= kInfAllyTrickleSecs) {
        g_last_trickle = now;
        uint16_t allies = count_allied_npcs();
        if (allies > 0) {
            int32_t gain = (int32_t)std::min<int>(allies * kInfAllyTrickle, 20);
            if (spaceext::is_host()) {
                rivals::grant_influence(0, gain, "allied empires");
            } else {
                net::Writer w;
                w.u8((uint8_t)NPCREL_EARN_INF);
                w.u8(spaceext::my_id());
                w.u32(0);
                write_i32(w, gain);
                write_str64(w, "allied empires");
                rivals::send_faction_to(0, rivals::Stage::Space, wire::rivals_faction::SK_NPC_RELS, w);
            }
        }
    }

    update_race_progress(now);
}

static void reset_world_state()
{
    g_home_cleanup_epoch = 0;
    g_home_cleanup_start = 0.0;
    g_home_cleanup_last = 0.0;
    g_home_cleanup_done = false;
    g_home_cleanup_pending = false;
    g_emig_unlock_epoch = 0;
    g_emig_colony_pack_epoch = 0;
    g_emig_unlock_pending = false;
}

LIFECYCLE_HOOK("space_rivals.world", lifecycle::kWorldChange, -35, [](const lifecycle::Ctx&) { reset_world_state(); },
               false);

// lifecycle-exempt: a Space feature (space_ext.h), run by spaceext::session_end_all from the spacecoop.session hook
static void on_session_end()
{
    g_emig_by_id.clear();
    g_guest_civ_cities.clear();
    g_my_emig = EmigrationRecord{};
    g_my_emig_pending = false;
    g_emig_sm.reset();
    g_relinquished_host_home_planet = 0;
    g_relinquished_host_home_star = 0;
    g_last_emig_notice.clear();
    g_homes.clear();
    g_inf_earn_cd.clear();
    g_guest_money.clear();
    g_queued_draft_money = 0;
    g_draft_money_pending = false;
    g_race_stats.clear();
    g_have_sent_stats = false;
    g_last_trickle = 0.0;
    g_finale.reset();
    g_finale_summary.clear();
    reset_world_state();
}

// ---- UI rows (F9 Rivals / Race page & Space page) ----
static std::vector<rivals::UiRow> extra_rivals_ui_rows()
{
    std::vector<rivals::UiRow> rows;
    if (!active())
        return rows;
    bool ru = spaceext::russian();
    uint32_t kGold = 0xFFF2D479, kGreen = 0xFF7BE882, kCyan = 0xFF80D8FF;
    char buf[220];

    // Homeworld status (R5 + R7)
    if (void* emp = player_empire(); readable(emp, 0xB8)) {
        uint32_t hp = F<uint32_t>(emp, 0xB4);
        void* hrec = hp != 0xFFFFFFFFu ? planet_record(hp) : nullptr;
        std::string hname = hrec ? str16((uint8_t*)hrec + 0x18) : "";
        snprintf(buf, sizeof buf, ru ? "Родная планета: %s (защищена)" : "Homeworld: %s (protected)",
                 hname.empty() ? "-" : hname.c_str());
        rows.push_back({buf, "", nullptr, kCyan});
    }

    // Per-player Space race stats (11.3.5: money, colonies, stars, NPC allies, distance to core)
    rows.push_back({ru ? "Космос (гонка):" : "Space Race stats:", "", nullptr, kGold});
    const auto& race = rivals::race_state();
    for (const auto& p : spaceext::ctx().sess->players()) {
        if (!rivals::valid_player(p.id))
            continue;
        const auto& pr = race.players[p.id];
        snprintf(buf, sizeof buf,
                 ru ? "  %s: $%.0f | кол=%.0f зв=%u союз=%u ядро=%.0f"
                    : "  %s: $%.0f | col=%.0f stars=%u allies=%u core=%.0f pc",
                 p.name.c_str(), pr.secondary, pr.primary, (unsigned)pr.stat_a, (unsigned)pr.stat_b, pr.extra);
        rows.push_back({buf, "", nullptr, 0});
    }

    // Chronicle Galaxy Core Finale summary (11.7)
    if (g_finale.awarded && !g_finale_summary.empty()) {
        rows.push_back({g_finale_summary, "", nullptr, kGreen});
    }
    return rows;
}

// Live discovered Space NPC empires for the F9 Influence UI (TurnAgainst / Protect / SmoothOver, BUG-1555).
static std::vector<rivals::NpcUiEntry> space_npc_ui_list(uint8_t viewer)
{
    std::vector<rivals::NpcUiEntry> out;
    if (!active() || !spaceext::connected())
        return out;
    // Each Space player lives in its own galaxy; host does not hold a remote peer's local NPC relationships.
    if (viewer != spaceext::my_id())
        return out;
    uint32_t me = my_pol();
    void* rm = rel_mgr();
    if (me == 0xFFFFFFFFu)
        return out;

    std::map<uint32_t, SpaceNpcSnapshot> by_pol;
    if (rm) {
        walk_map((uint8_t*)rm + 0x24, [&](void* node) {
            uint32_t a = F<uint32_t>(node, 0x10);
            uint32_t b = F<uint32_t>(node, 0x14);
            uint32_t npc = (a == me) ? b : ((b == me) ? a : 0);
            if (!npc || !is_npc_empire_pol(npc))
                return;
            void* e = empire_of(npc);
            if (!e)
                return;
            uint32_t flags = F<uint32_t>(node, 0x1C);
            float rel = a_CalcRel ? thiscall<float>(a_CalcRel, rm, npc, me, 0) : 0.0f;
            SpaceNpcSnapshot s;
            s.pol_id = npc;
            s.name = str16((uint8_t*)e + 0x3C);
            s.rel = std::isfinite(rel) ? rel : 0.0f;
            s.flag_war = (flags & 1u) != 0;
            s.flag_ally = (flags & 2u) != 0;
            by_pol[npc] = std::move(s);
        });
    }

    // Also include met/contacted empires from cPlayer (+0x11D8) that do not yet have a relationship record.
    if (void* nm = a_NounMgr ? cdecl_call<void*>(a_NounMgr) : nullptr) {
        void* pl = readable(nm, 0x78) ? F<void*>(nm, 0x74) : nullptr;
        if (pl && readable((uint8_t*)pl + 0x11D8, 8)) {
            uint32_t* b = F<uint32_t*>(pl, 0x11D8);
            uint32_t* e = F<uint32_t*>(pl, 0x11DC);
            if (b && e > b && e - b <= 40000 && readable(b, (size_t)(e - b) * 4)) {
                for (uint32_t* q = b; q + 1 < e; q += 2) {
                    if (!(q[1] & 0x22u))
                        continue; // 0x20 = met, 0x02 = contacted
                    uint32_t npc = q[0];
                    if (by_pol.count(npc) || !is_npc_empire_pol(npc))
                        continue;
                    void* emp = empire_of(npc);
                    if (!emp)
                        continue;
                    float rel = (rm && a_CalcRel) ? thiscall<float>(a_CalcRel, rm, npc, me, 0) : 0.0f;
                    SpaceNpcSnapshot s;
                    s.pol_id = npc;
                    s.name = str16((uint8_t*)emp + 0x3C);
                    s.rel = std::isfinite(rel) ? rel : 0.0f;
                    by_pol[npc] = std::move(s);
                }
            }
        }
    }

    std::vector<SpaceNpcSnapshot> snaps;
    snaps.reserve(by_pol.size());
    for (auto& [pol, s] : by_pol)
        snaps.push_back(std::move(s));

    bool has_rival = spaceext::ctx().sess && spaceext::ctx().sess->players().size() >= 2;
    auto built = build_space_npc_ui_list(std::move(snaps), has_rival);
    out.reserve(built.size());
    for (auto& it : built)
        out.push_back({it.npc_id, std::move(it.label), it.allied, it.hostile, it.can_turn, it.can_protect, it.can_smooth});
    return out;
}

static std::vector<spaceext::Row> space_page_rows()
{
    std::vector<spaceext::Row> v;
    if (!active())
        return v;
    if (!g_last_emig_notice.empty())
        v.push_back({g_last_emig_notice, "", nullptr, 0xFF80D8FF});
    if (g_finale.awarded && !g_finale_summary.empty())
        v.push_back({g_finale_summary, "", nullptr, 0xFF7BE882});
    return v;
}

// ---- Status & Debug Console (`coop.space rivals ...`) ----
static std::string status()
{
    void* emp = player_empire();
    uint32_t hs = readable(emp, 0xB8) ? F<uint32_t>(emp, 0xB0) : 0;
    uint32_t hp = readable(emp, 0xB8) ? F<uint32_t>(emp, 0xB4) : 0;
    uint16_t hcities = (hp && hp != 0xFFFFFFFFu) ? count_cities_on(planet_record(hp), my_pol()) : 0;
    char buf[640];
    snprintf(buf, sizeof buf,
             "space_rivals: fix=%d relinq_fix=%d active=%d hooks=%d home_star=%u home_planet=%08X home_cities=%u "
             "emigrated=%d relinq_host=%08X teleported=%d homes=%u buster_blocked=%u capture_blocked=%u "
             "city_hp_restored=%u civ_veh_swept=%u civ_cities_unified=%u emig_unlocks=%u "
             "inf_applied=%u draft_payouts=%u finale_awarded=%d finale_first=%u finale_winner=%u",
             (int)g_space_rivals_fix, (int)g_relinquish_clean_fix, (int)active(), (int)g_hooks_ok,
             hs == 0xFFFFFFFFu ? 0u : hs, hp == 0xFFFFFFFFu ? 0u : hp, (unsigned)hcities,
             (int)(g_emig_sm.applied_planet != 0), g_relinquished_host_home_planet, (int)g_emig_sm.teleported,
             (unsigned)g_homes.homes.size(), g_buster_blocked, g_capture_blocked, g_city_hp_restored,
             g_civ_vehicles_swept, g_civ_cities_unified, g_emig_unlocks_applied, g_inf_applied, g_draft_payouts,
             (int)g_finale.awarded, (unsigned)g_finale.first_player, (unsigned)g_finale.winner);
    return buf;
}

static std::string debug(const std::vector<std::string>& a)
{
    if (a.empty() || a[0] == "status") {
        std::string out = status() + "\n";
        if (!g_last_emig_notice.empty())
            out += "emig_notice=" + g_last_emig_notice + "\n";
        if (!g_finale_summary.empty())
            out += "finale_summary=" + g_finale_summary + "\n";
        return out;
    }
    if (a[0] == "homes") {
        refresh_homes_from_rosters();
        std::string out;
        char b[128];
        for (const auto& [pid, h] : g_homes.homes) {
            snprintf(b, sizeof b, "home player=%u star=%u planet=%08X\n", (unsigned)pid, h.star_id, h.planet_id);
            out += b;
        }
        return out.empty() ? "no homes\n" : out;
    }
    if (a[0] == "emigrate") {
        if (!g_space_rivals_fix)
            return "rivals.space is off\n";
        if (a.size() >= 2) {
            uint16_t c = clamp_emig_cities(atoi(a[1].c_str()));
            if (a.size() >= 3) {
                uint8_t pid = (uint8_t)atoi(a[2].c_str());
                if (!rivals::valid_player(pid) || pid == 0)
                    return "error: invalid guest pid (expected 1..3)\n";
                set_guest_civ_cities(pid, c);
            } else {
                for (uint8_t pid = 1; pid < rivals::kMaxPlayers; pid++)
                    set_guest_civ_cities(pid, c);
            }
        }
        if (spaceext::is_host()) {
            g_emig_by_id.clear();
            g_emig_store.by_key.clear();
            host_ensure_guest_emigrations();
            return "emigration assigned (" + std::to_string(g_emig_by_id.size()) + " guests)\n";
        }
        EmigrationRecord dummy{};
        send_emig_msg(0, EMIG_REQ, dummy);
        return "emigration requested\n";
    }
    if (a[0] == "test_buster") {
        if (!spaceext::in_space() || !player_empire())
            return "not in space\n";
        // Tests R7 Planet Buster protection through BOTH real hooks: cToolManager_OnUseTool and cSimPlanetHighLOD_DestroyPlanet.
        uint32_t ap = active_planet_id();
        bool on_prot = is_protected_homeworld_planet(ap);
        uint32_t before = g_buster_blocked;
        // 1. Test real cSimPlanetHighLOD_DestroyPlanet hook if active planet is protected (never blow up unprotected planet in test!).
        void* sg = a_SpaceGame ? cdecl_call<void*>(a_SpaceGame) : nullptr;
        void* hlod = readable(sg, 0x58) ? F<void*>(sg, 0x54) : nullptr;
        bool tool_blocked = false;
        // 2. Test real cToolManager_OnUseTool hook using a Planet Buster tool object if present or via live inventory.
        void* inv = readable(sg, 0x44) ? F<void*>(sg, 0x40) : nullptr;
        void* tm = a_ToolMgr ? cdecl_call<void*>(a_ToolMgr) : nullptr;
        if (inv && tm && a_GetTool && a_OnUseTool && on_prot) {
            uint32_t key[3] = {kToolPlanetBuster, 0, 0};
            void* tool = thiscall<void*>(a_GetTool, inv, (const void*)key);
            if (!tool && a_AddItemByKey) {
                thiscall<void>(a_AddItemByKey, inv, (const void*)key);
                tool = thiscall<void*>(a_GetTool, inv, (const void*)key);
            }
            if (tool) {
                float pos[3] = {0, 0, 0};
                bool fired = thiscall<bool>(a_OnUseTool, tm, tool, (const float*)pos, 0);
                tool_blocked = !fired;
            }
        }
        if (hlod && a_DestroyPlanet && on_prot) {
            thiscall<void>(a_DestroyPlanet, hlod);
        }
        char b[200];
        snprintf(b, sizeof b, "test_buster: active_planet=%08X protected=%d tool_blocked=%d blocked_delta=%u total=%u\n",
                 ap, (int)on_prot, (int)tool_blocked, g_buster_blocked - before, g_buster_blocked);
        return b;
    }
    if (a[0] == "test_capture" && a.size() >= 2) {
        if (!spaceext::in_space() || !player_empire())
            return "not in space\n";
        // Tests R7 homeworld capture protection through the real hooked cEmpire::CaptureSystem(star, my_pol).
        uint32_t sid = (uint32_t)strtoul(a[1].c_str(), nullptr, 0);
        void* star = spacecoop::star_record(sid);
        if (!star || !a_CaptureSystem)
            return "no star\n";
        uint32_t owner_before = F<uint32_t>(star, 0x54);
        uint32_t before = g_capture_blocked;
        cdecl_call<void>(a_CaptureSystem, star, my_pol());
        uint32_t owner_after = F<uint32_t>(star, 0x54);
        char b[200];
        snprintf(b, sizeof b, "test_capture: star=%u owner %08X -> %08X blocked_delta=%u total=%u\n", sid, owner_before,
                 owner_after, g_capture_blocked - before, g_capture_blocked);
        return b;
    }
    if (a[0] == "npc") {
        // List up to 8 real NPC empires with their pol IDs and current relation to this player.
        void* sm = star_mgr();
        if (!sm)
            return "no star manager\n";
        std::string out;
        char b[160];
        int shown = 0;
        walk_map((uint8_t*)sm + 0x150, [&](void* node) {
            if (shown >= 8)
                return;
            uint32_t pol = F<uint32_t>(node, 0x10);
            void* e = F<void*>(node, 0x14);
            if (!e || !is_npc_empire_pol(pol))
                return;
            float rel = (rel_mgr() && a_CalcRel) ? thiscall<float>(a_CalcRel, rel_mgr(), pol, my_pol(), 0) : 0.0f;
            snprintf(b, sizeof b, "npc %08X '%s' rel=%.2f\n", pol, str16((uint8_t*)e + 0x3C).c_str(), rel);
            out += b;
            shown++;
        });
        return out.empty() ? "no npc empires\n" : out;
    }
    if (a[0] == "race") {
        std::string out;
        char b[160];
        if (spaceext::is_host()) {
            g_race_stats[0] = read_local_stats();
            for (const auto& [pid, s] : g_race_stats) {
                snprintf(b, sizeof b, "p%u money=%d colonies=%u stars=%u allies=%u dist=%.1f\n", (unsigned)pid,
                         (int)s.money, (unsigned)s.colonies, (unsigned)s.stars, (unsigned)s.allied_npcs,
                         s.dist_to_core);
                out += b;
            }
        } else {
            SpaceRaceStats s = read_local_stats();
            snprintf(b, sizeof b, "p%u money=%d colonies=%u stars=%u allies=%u dist=%.1f\n",
                     (unsigned)spaceext::my_id(), (int)s.money, (unsigned)s.colonies, (unsigned)s.stars,
                     (unsigned)s.allied_npcs, s.dist_to_core);
            out += b;
        }
        return out.empty() ? "no race stats\n" : out;
    }
    if (a[0] == "star" && a.size() >= 2) {
        uint32_t sid = (uint32_t)strtoul(a[1].c_str(), nullptr, 0);
        void* star = spacecoop::star_record(sid);
        if (!readable(star, 0x74))
            return "no star\n";
        uint32_t owner = F<uint32_t>(star, 0x54);
        uint32_t stech = F<uint32_t>(star, 0x4C);
        uint32_t sflags = F<uint32_t>(star, 0x5C);
        uint32_t max_ptech = 0;
        uint32_t p0_id = 0;
        size_t civs = 0;
        for (void* pr : planets_of(star)) {
            if (!readable(pr, 0x198))
                continue;
            uint32_t pt = F<uint32_t>(pr, 0x194);
            size_t cn = ptr_vec((uint8_t*)pr + 0x15C, 64).size();
            if (pt >= max_ptech && (cn > 0 || p0_id == 0)) {
                max_ptech = pt;
                p0_id = F<uint32_t>(pr, 0x184);
                civs = cn;
            }
        }
        char b[220];
        snprintf(b, sizeof b,
                 "star=%u owner=%08X star_tech=%u flags=%08X planet=%08X planet_tech=%u civs=%u relinq_fix=%d\n",
                 sid, owner, stech, sflags, p0_id, max_ptech, (unsigned)civs, (int)g_relinquish_clean_fix);
        return b;
    }
    if (a[0] == "test_core") {
        uint8_t pid = a.size() >= 2 ? (uint8_t)atoi(a[1].c_str()) : spaceext::my_id();
        on_core_entered(pid);
        return "test_core: awarded=" + std::to_string((int)g_finale.awarded) + " first=" +
               std::to_string((unsigned)g_finale.first_player) + " winner=" +
               std::to_string((unsigned)g_finale.winner) + "\n";
    }
    return "usage: coop.space rivals [status | homes | emigrate [cities [pid]] | test_buster | test_capture <star> | npc | race | test_core [id]]\n";
}

// ---- Initialization ----
static bool init(const SkHost* h, SkMod* m)
{
    H = h;
    M = m;
    auto S = [](const char* n) { return H->sym(n); };
    a_StarMgr = S("Simulator::cStarManager::Get");
    a_GetEmpire = S("Simulator::cStarManager::GetEmpire");
    a_RequirePlanets = S("Simulator::cStarManager::RequirePlanetsForStar");
    a_GetPlanets = S("Simulator::cStarRecord::GetPlanetRecords");
    a_PlayerEmpire = S("Simulator::GetPlayerEmpire");
    a_PlayerEmpireID = S("Simulator::GetPlayerEmpireID");
    a_ActivePlanetRec = S("Simulator::GetActivePlanetRecord");
    a_ActiveStarRec = S("Simulator::GetActiveStarRecord");
    a_AddMoney = S("Simulator::cEmpire::AddMoney");
    a_AddStar = S("Simulator::cEmpire::AddStarOwnership");
    a_AddPlanet = S("SporeCoop::cEmpire_AddPlanet");
    a_SetHomePlanet = S("SporeCoop::cEmpire_SetHomePlanet");
    a_PlanetTech = S("SporeCoop::cPlanetRecord_SetTechLevel");
    a_FillPlanet = S("SporeCoop::cPlanetRecord_FillPlanetDataForTechLevel");
    a_GameAlloc = S("SporeCoop::GameAlloc");
    a_CityName = S("SporeCoop::cCityData_AllocName");
    a_CityCtor = S("SporeCoop::cCityData_ctor");
    a_AddCityData = S("SporeCoop::cCivData_AddCityData");
    a_Context = S("SporeCoop::Space_GetCurrentContext");
    a_SpacePlayerData = S("Simulator::SpacePlayerData::ptr");
    a_SpaceGame = S("Simulator::cSimulatorSpaceGame::Get");
    a_PlayerUFOGet = S("Simulator::cSimulatorPlayerUFO::Get");
    a_RelMgr = S("Simulator::cRelationshipManager::Get");
    a_ApplyRel = S("Simulator::cRelationshipManager::ApplyRelationship");
    a_CalcRel = S("Simulator::cRelationshipManager::CalculateRelationship");
    a_MakeContact = S("SporeCoop::cEmpire_MakeContact");
    a_ToolMgr = S("Simulator::cToolManager::Get");
    a_GetTool = S("Simulator::cPlayerInventory::GetTool");
    a_AddItemByKey = S("Simulator::cPlayerInventory::AddItemByKey");

    a_OnUseTool = S("SporeCoop::cToolManager_OnUseTool");
    a_DestroyPlanet = S("SporeCoop::cSimPlanetHighLOD_DestroyPlanet");
    a_CaptureSystem = S("Simulator::cEmpire::CaptureSystem");
    a_TransferStar = S("SporeCoop::TransferStarOwnership");
    a_SetCaptureProgress = S("Simulator::cCity::SetCaptureProgress");
    a_DestroyCity = S("Simulator::cSimPlanetHighLOD::DestroyCity");
    a_NounMgr = S("Simulator::cGameNounManager::Get");
    a_NearRebuild = S("SporeCoop::SpaceNearStars_Rebuild");
    a_NearVt = S("SporeCoop::SpaceNearStars_vftable");
    a_GetAllCities = S("Simulator::cGameNounManager::GetAllCities");
    a_GetAllVehicles = S("Simulator::cGameNounManager::GetAllVehicles");
    a_DestroyInstance = S("Simulator::cGameNounManager::DestroyInstance");
    a_VehicleRemove = S("SporeCoop::Vehicle_Remove");
    a_CityChangeOwner = S("Simulator::cCity::ChangeOwner");
    a_CitySetPlayerOwned = S("SporeCoop::City_SetIsPlayerOwned");
    a_CivByPol = S("Simulator::cGameNounManager::GetCivilizationByPoliticalID");
    a_CombatantSetHealth = S("Simulator::cCombatant::SetHealth");
    a_MissionMgr = S("Simulator::cMissionManager::Get");
    a_SetFlightSchool = S("SporeCoop::MissionManager_SetFlightSchoolFinished");
    a_SetMission201 = S("SporeCoop::MissionManager_SetMission201Finished");

    bool ok = true;
    if (a_OnUseTool)
        ok = H->hook_create(M, (void*)a_OnUseTool, (void*)&h_OnUseTool, (void**)&o_OnUseTool) == 0 && ok;
    if (a_DestroyPlanet)
        ok = H->hook_create(M, (void*)a_DestroyPlanet, (void*)&h_DestroyPlanet, (void**)&o_DestroyPlanet) == 0 && ok;
    if (a_CaptureSystem)
        ok = H->hook_create(M, (void*)a_CaptureSystem, (void*)&h_CaptureSystem, (void**)&o_CaptureSystem) == 0 && ok;
    if (a_TransferStar)
        ok = H->hook_create(M, (void*)a_TransferStar, (void*)&h_TransferStar, (void**)&o_TransferStar) == 0 && ok;
    if (a_SetCaptureProgress)
        ok = H->hook_create(M, (void*)a_SetCaptureProgress, (void*)&h_SetCaptureProgress,
                            (void**)&o_SetCaptureProgress) == 0 &&
             ok;
    if (a_DestroyCity)
        ok = H->hook_create(M, (void*)a_DestroyCity, (void*)&h_DestroyCity, (void**)&o_DestroyCity) == 0 && ok;
    g_hooks_ok = ok && o_OnUseTool && o_DestroyPlanet && o_CaptureSystem && o_TransferStar && o_SetCaptureProgress &&
                 o_DestroyCity;

    // Register MSG_RIVALS_FACTION (81) handler, NPC influence callback, and StageHooks for Stage::Space.
    rivals::register_faction_handler(rivals::Stage::Space, on_faction_msg);
    rivals::register_npc_influence_callback(rivals::Stage::Space, on_npc_influence);
    rivals::StageHooks hooks;
    hooks.on_buy_influence = on_buy_influence_space;
    hooks.on_extra_ui_rows = extra_rivals_ui_rows;
    hooks.on_npc_ui_list = space_npc_ui_list;
    rivals::register_stage_hooks(rivals::Stage::Space, std::move(hooks));

    H->log(M, g_hooks_ok ? SK_INFO : SK_WARN, "space_rivals: initialized (fix=%d hooks=%d)", (int)g_space_rivals_fix,
           (int)g_hooks_ok);
    return true;
}

static bool reg = spaceext::add({.name = "rivals",
                                 .kind_lo = 0,
                                 .kind_hi = 0,
                                 .init = init,
                                 .tick = tick,
                                 .on_message = nullptr,
                                 .on_session_end = on_session_end,
                                 .rows = space_page_rows,
                                 .status = status,
                                 .debug = debug});

} // namespace spacerivals
