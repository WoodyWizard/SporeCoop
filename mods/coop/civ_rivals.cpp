// Civilization Rivals mode runtime implementation (WP-R5 of 0.8.0, BUG-1549, docs/11-rivals.md 11.3.4, 11.4-11.9).
#include "civ_rivals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "call_guard.h"
#include "civ_internal.h"
#include "fixes.h"
#include "game.h"
#include "history.h"
#include "history_logic.h"
#include "i18n.h"
#include "lifecycle.h"
#include "modes.h"
#include "rivals.h"
#include "savestrip.h"
#include "space_rivals.h"
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

// BUG-1563: guarantee that a guest's taken-over NPC nation & capital city specialty matches the player's Tribe
// consequence card when no naturally matching free NPC exists on the planet (host + guest sync).
static bool g_fix_civ_spec_convert = true;
COOP_FIX("rivals.civ_spec_convert", "BUG-1563", g_fix_civ_spec_convert,
         "civ: convert taken-over guest nation and capital city specialty to match the player's Tribe card");

// BUG-1590: allow a Civ Rivals guest to choose or switch their starting nation specialty (Military / Religious /
// Economic) via F9 -> Rivals (or Tribe card mid-game pick / console) while they still own only their starting capital.
static bool g_fix_civ_spec_pick = true;
COOP_FIX("rivals.civ_spec_pick", "BUG-1590", g_fix_civ_spec_pick,
         "civ: allow Civ Rivals guests to choose their starting nation specialty (Military/Religious/Economic) before capturing a second city");

// BUG-1563: validate and deduct guest superweapon costs directly on mWealth (+0x98) in Civ Rivals so guest
// superweapons cannot bypass insufficient funds or trigger NPC budget refill on mIsPlayer == 0 civilizations.
static bool g_fix_civ_sw_spend = true;
COOP_FIX("rivals.civ_sw_spend", "BUG-1563", g_fix_civ_sw_spend,
         "civ: validate and deduct guest superweapon mWealth cost directly in Civ Rivals");

// BUG-1581: protect guest superweapon targeting/firing by borrowing +0x470 tools and +0x490 timers on guest civs.
static bool g_fix_civ_guest_sw = true;
COOP_FIX("rivals.civ_guest_sw", "BUG-1581", g_fix_civ_guest_sw,
         "civ: borrow superweapon tool & timer vectors for guest NPC-origin civilizations so guest superweapons never crash");

// BUG-1581: persist player_key -> pol_id across disconnects/rejoins (and host store) so reconnecting guests keep their nation.
static bool g_fix_civ_rejoin_nation = true;
COOP_FIX("rivals.civ_rejoin_nation", "BUG-1581", g_fix_civ_rejoin_nation,
         "civ: remember each player's rival nation across disconnects and reloads so rejoining guests reclaim their original nation");

// BUG-1581: prefer large (non-small / highest building-slot) free NPC starting cities for guests.
static bool g_fix_civ_large_start = true;
COOP_FIX("rivals.civ_large_start", "BUG-1581", g_fix_civ_large_start,
         "civ: prefer large starting cities for guests in Civ Rivals instead of small coastal cities");

// BUG-1581: credit vanilla diplomatic Sporebucks gifts (event 0x0526E4FB) to the recipient rival player's mWealth.
static bool g_fix_civ_diplo_gift = true;
COOP_FIX("rivals.civ_diplo_gift", "BUG-1581", g_fix_civ_diplo_gift,
         "civ: credit vanilla diplomatic gifts of Sporebucks to the recipient player's wealth in Civ Rivals");

// BUG-1581: prevent NPCs from ruining a protected player capital into a permanent softlock.
static bool g_fix_civ_capital_softlock = true;
COOP_FIX("rivals.civ_capital_softlock", "BUG-1581", g_fix_civ_capital_softlock,
         "civ: repel NPC sieges on protected player capitals so capitals are not softlocked in permanent ruins");

// ---- Game addresses ----
static uintptr_t a_GetPlayerCiv = 0;
static uintptr_t a_GetPlayerPol = 0;
static uintptr_t a_CityIsPlayerOwned = 0;
static uintptr_t a_CitySetPlayerOwned = 0;
static uintptr_t a_CitySetSpecialty = 0;
static uintptr_t a_CivRefreshUnlocks = 0;
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
constexpr uintptr_t kCivSwTools = 0x470;  // eastl::vector<intrusive_ptr<cSpaceToolData>> (12 entries on player civ, empty on NPC)
constexpr uintptr_t kCivSwTimers = 0x490; // eastl::vector<cGameTime> (12 entries, stride 0x20)
constexpr uintptr_t kCivDomesticBudget = 0x4AC;
constexpr uintptr_t kCivExpansionBudget = 0x4B0;
constexpr uintptr_t kCivIsPlayer = 0x89;
constexpr uintptr_t kCivSelectable = 0xD4;
constexpr uintptr_t kCityIsPlayer = 0x33C;
constexpr uintptr_t kCitySmallFlag = 0x519; // cCity mbSmallCity (verified at 0x00BF21C3)
constexpr uintptr_t kCitySpatialOff = 0x120;

// ---- Hook originals ----
using GetPlayerCiv_t = void*(SK_THISCALL*)(void*);
using GetPlayerPol_t = uint32_t(SK_THISCALL*)(void*);
using CityIsPlayer_t = bool(SK_THISCALL*)(void*);
using CivAI_t = void(SK_THISCALL*)(void*);
using CanTargetSW_t = bool(SK_CDECL*)(int, const float*);
using LaunchGlobalSW_t = void(SK_THISCALL*)(void*);
using TradeDelivery_t = float(SK_THISCALL*)(void*, uint32_t, float, void*);
using CivDtor_t = void(SK_THISCALL*)(void*);
using CivScalarDtor_t = void*(SK_THISCALL*)(void*, uint32_t);

static GetPlayerCiv_t o_GetPlayerCiv = nullptr;
static GetPlayerPol_t o_GetPlayerPol = nullptr;
static CityIsPlayer_t o_CityIsPlayer = nullptr;
static CivAI_t o_CivAI = nullptr;
static CanTargetSW_t o_CanTargetSW = nullptr;
static LaunchGlobalSW_t o_LaunchGlobalSW = nullptr;
static TradeDelivery_t o_TradeDelivery = nullptr;
static CivDtor_t o_CivDtor = nullptr;
static CivScalarDtor_t o_CivScalarDtor = nullptr;
static bool g_civ_dtor_hook_tried = false;

// ---- Runtime state ----
static bool g_inited = false;
static bool g_bypass_swap = false;
CALL_GUARD("civ_rivals.bypass_swap", g_bypass_swap);
static bool g_in_save = false;

static std::array<PlayerNation, kMaxPlayers> g_nations{};
static std::string g_slot_key[kMaxPlayers]; // host: stable player key currently assigned to each slot
static NationMemory g_saved_nations;        // host: player_key -> original nation across disconnects/reloads (BUG-1581)
static uint32_t g_saved_store_serial = 0;
static NpcRelationTable g_npc_rels;
static std::map<uint32_t, float> g_guest_npc_rels; // guest's received per-player NPC relations
static bool g_swap_applied = false;
static bool g_spec_hint_shown = false;
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
static uint16_t g_last_city_counts[kMaxPlayers] = {};

// Guest persistent borrow of orig_player_civ() +0x470 tools and per-civ isolated +0x490 timers while perspective swap
// is active (BUG-1581). Always un-aliased in h_CivDtor before ~cCivilization frees +0x490/+0x470.
struct GuestSwBorrow {
    void* civ = nullptr;
    void* source_civ = nullptr;
    uint32_t saved_tools[3] = {};
    uint32_t saved_timers[3] = {};
    uint32_t borrowed_tools_begin = 0;
    uint32_t borrowed_timers_begin = 0;
    bool tools = false;
    bool timers = false;
};
static GuestSwBorrow g_guest_sw_borrow;

// Per-civilization isolated cGameTime[12] cooldown buffer (stride 0x20, 384 bytes) for any NPC-origin civ whose
// native +0x490 vector is empty, so host and guest cooldowns never alias or cross-contaminate.
struct IsolatedSwTimers {
    alignas(16) uint8_t buf[12 * 32] = {};
};
static std::unordered_map<void*, std::unique_ptr<IsolatedSwTimers>> g_isolated_sw_timers;
static std::vector<std::pair<SwToolsBorrow*, void*>> g_active_sw_borrows; // (guard, source_civ)

// Recent SpendMoney observation for pairing with vanilla diplomacy gift ApplyRelationship (0x0526E4FB, BUG-1581).
struct LocalSpend {
    void* civ = nullptr;
    float amount = 0.0f;
    double at = -100.0;
};
static LocalSpend g_last_local_spend;

// Host-side guest design sync tracking (BUG-1581: keep guest's custom vehicle/building designs in guest civ's culture map).
struct AppliedDesign {
    uint32_t orig[3] = {};
    uint32_t applied[3] = {};
};
static std::map<std::pair<uint8_t, uint32_t>, AppliedDesign> g_applied_designs; // (pid, design_id) -> keys
static bool g_designs_dirty = true;
static double g_last_design_sync = -100.0;

// ---- Diagnostics counters ----
static struct {
    uint32_t takeovers = 0;
    uint32_t reclaimed = 0;
    uint32_t ai_blocked = 0;
    uint32_t swap_hits = 0;
    uint32_t cap_blocked = 0;
    uint32_t conv_blocked = 0;
    uint32_t sw_blocked = 0;
    uint32_t sw_guest_borrows = 0;
    uint32_t pvp_allowed = 0;
    uint32_t pvp_blocked = 0;
    uint32_t war_captures = 0;
    uint32_t gifts_sent = 0;
    uint32_t gifts_done = 0;
    uint32_t gifts_rejected = 0;
    uint32_t spice_gifts = 0;
    uint32_t diplo_gifts = 0;
    uint32_t inf_bought = 0;
    uint32_t inf_turned = 0;
    uint32_t inf_protected = 0;
    uint32_t inf_smoothed = 0;
    uint32_t inf_trade = 0;
    uint32_t inf_diplomacy = 0;
    uint32_t rel_readbacks = 0;
    uint32_t draft_payouts = 0;
    uint32_t assign_tx = 0;
    uint32_t assign_rx = 0;
    uint32_t npcrel_tx = 0;
    uint32_t npcrel_rx = 0;
    uint32_t draft_applied = 0;
    uint32_t allied_trades = 0;
    uint32_t spec_converted = 0;
    uint32_t spec_picked = 0;
    uint32_t sw_spent = 0;
    uint32_t sw_refused_funds = 0;
    uint32_t designs_synced = 0;
} g_st;

// ---- Helpers ----
static bool sess_connected() { return C.sess && C.sess->connected(); }
static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->connected() && C.sess->role() == net::Role::Client; }
static uint8_t my_id() { return C.sess ? C.sess->my_id() : 0; }
static double now_s() { return H ? H->time_seconds() : 0.0; }

bool enabled() { return g_fix_rivals_civ; }
bool spec_convert_enabled() { return g_fix_civ_spec_convert; }
bool sw_spend_enabled() { return g_fix_civ_sw_spend; }

bool active()
{
    return g_fix_rivals_civ && rivals::stage_is_rivals(rivals::Stage::Civ) && game::mode_id() == modes::kCiv &&
           sess_connected();
}

static void apply_city_specialty(void* city, void* civ, int target_spec)
{
    if (!city || !valid_native_specialty(target_spec))
        return;
    if (a_CitySetSpecialty)
        thiscall<void>(a_CitySetSpecialty, city, target_spec, 1);
    if (skg::readable(city, civ::kCitySpecialty + 4))
        F<int>(city, civ::kCitySpecialty) = target_spec;
    if (skg::readable(city, 0x29C + 4))
        F<int>(city, 0x29C) = target_spec;
    if (civ && a_CivRefreshUnlocks)
        thiscall<void>(a_CivRefreshUnlocks, civ);
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

void* my_civ()
{
    if (!active())
        return nullptr;
    return civ_of_player(my_id());
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

bool is_reserved_nation(void* civ)
{
    if (!civ || !active() || !g_fix_civ_rejoin_nation)
        return false;
    uint32_t pol = civ::pol_of(civ);
    if (!pol || pol == 0xFFFFFFFFu)
        return false;
    int pid = player_of_pol(pol);
    if (pid >= 1 && pid < kMaxPlayers)
        return true;
    return is_host() && g_saved_nations.is_reserved_pol(pol);
}

bool is_reserved_city(void* city)
{
    if (!city || !active() || !g_fix_civ_rejoin_nation)
        return false;
    uint32_t pol = civ::pol_of(city);
    if (!pol || pol == 0xFFFFFFFFu)
        return false;
    int pid = player_of_pol(pol);
    if (pid >= 1 && pid < kMaxPlayers)
        return true;
    return is_host() && g_saved_nations.is_reserved_pol(pol);
}

bool capital_softlock_fix_enabled() { return g_fix_civ_capital_softlock; }

static std::string player_stable_key(uint8_t pid)
{
    std::string k = store::key_of(pid);
    if (!k.empty())
        return k;
    if (C.sess) {
        std::string nm = C.sess->name_of(pid);
        if (!nm.empty())
            return "name:" + nm;
    }
    return "";
}

static int city_building_slot_count(void* city)
{
    if (!city || !skg::readable(city, civ::kCitySlots + 8))
        return 0;
    uint8_t* sb = F<uint8_t*>(city, civ::kCitySlots);
    uint8_t* se = F<uint8_t*>(city, civ::kCitySlots + 4);
    if (!sb || se <= sb)
        return 0;
    ptrdiff_t bytes = se - sb;
    if (bytes < 0 || bytes > 64 * 0x18)
        return 0;
    return (int)(bytes / 0x18);
}

static bool city_is_small(void* city)
{
    if (!city || !skg::readable(city, kCitySmallFlag + 1))
        return false;
    return F<uint8_t>(city, kCitySmallFlag) != 0;
}

static const char* civ_color_name(void* c)
{
    if (!c || !skg::readable(c, 0x64))
        return "";
    uint32_t cid = F<uint32_t>(c, 0x38);
    float r = F<float>(c, 0x58), g = F<float>(c, 0x5C), b = F<float>(c, 0x60);
    if ((cid >> 8) == 0x053DBC && std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && r >= 0.0f &&
        r <= 1.001f && g >= 0.0f && g <= 1.001f && b >= 0.0f && b <= 1.001f && (r + g + b) > 0.01f)
        return classify_civ_color(r, g, b);
    return "";
}

// BUG-1581: ensure a civilization has non-zero vehicle design keys for every locomotion (land/sea/air) of `specialty`.
// RolloverCivCity (0x00E2BA81) greys out the tank/ship/plane purchase button whenever GetDesignKey(VehicleDesignId) is {0,0,0}.
static void ensure_civ_vehicle_designs(void* civ, uint8_t pid, int specialty)
{
    if (!civ || !valid_native_specialty(specialty) || !civ::a_DesignKey || !civ::a_DesignId || !a_CivSetDesign)
        return;
    for (int loco = 0; loco <= 2; loco++) {
        uint32_t did = (uint32_t)skg::cdecl_call<int>(civ::a_DesignId, specialty, loco);
        if (!did)
            continue;
        uint32_t* cur = thiscall<uint32_t*>(civ::a_DesignKey, civ, (int)did);
        if (cur && (cur[0] || cur[1] || cur[2]))
            continue; // already has a design
        uint32_t pick[3] = {0, 0, 0};
        // 1. Prefer the player's own design from species line if registered here
        creations::Key sk = species::design(pid, did);
        if (!sk.instance && is_guest() && pid == my_id())
            sk = species::own_design(did);
        if (sk.instance) {
            pick[0] = sk.instance;
            pick[1] = sk.type;
            pick[2] = sk.group;
        }
        // 2. Fallback: copy a valid design of the same VehicleDesignId from host/other live civs on the planet
        if (!pick[0]) {
            if (void* orig = orig_player_civ(); orig && orig != civ) {
                uint32_t* hk = thiscall<uint32_t*>(civ::a_DesignKey, orig, (int)did);
                if (hk && hk[0] && hk[0] != 0xFFFFFFFFu) {
                    pick[0] = hk[0];
                    pick[1] = hk[1];
                    pick[2] = hk[2];
                }
            }
        }
        if (!pick[0]) {
            for (void* other : all_civs()) {
                if (!other || other == civ)
                    continue;
                uint32_t* ok = thiscall<uint32_t*>(civ::a_DesignKey, other, (int)did);
                if (ok && ok[0] && ok[0] != 0xFFFFFFFFu) {
                    pick[0] = ok[0];
                    pick[1] = ok[1];
                    pick[2] = ok[2];
                    break;
                }
            }
        }
        if (pick[0]) {
            thiscall<void>(a_CivSetDesign, civ, did, pick);
            if (H && M)
                H->log(M, SK_INFO,
                       "civ_rivals: populated missing vehicle design 0x%08X -> %08X for player %u (spec=%d loco=%d, BUG-1581)",
                       (unsigned)did, (unsigned)pick[0], (unsigned)pid, specialty, loco);
        }
    }
}

// BUG-1581: apply each guest's own vehicle/building designs (from species_line) into the guest's rival cCivilization
// culture map on the host so host_buy spawns the guest's actual creations instead of reverting to generic NPC vehicles.
static void host_sync_guest_designs(double now)
{
    if (!is_host() || !active() || g_in_save || !a_CivSetDesign || !civ::a_DesignKey)
        return;
    if (!g_designs_dirty && now - g_last_design_sync < 1.0)
        return;
    g_designs_dirty = false;
    g_last_design_sync = now;
    for (uint8_t pid = 1; pid < kMaxPlayers; pid++) {
        if (!g_nations[pid].assigned)
            continue;
        void* gc = civ_by_pol(g_nations[pid].pol_id);
        if (!gc)
            continue;
        for (uint32_t did : species::design_ids(pid)) {
            creations::Key sk = species::design(pid, did);
            if (!sk.instance)
                continue;
            auto map_key = std::make_pair(pid, did);
            auto it = g_applied_designs.find(map_key);
            uint32_t* cur = thiscall<uint32_t*>(civ::a_DesignKey, gc, (int)did);
            if (it != g_applied_designs.end() && it->second.applied[0] == sk.instance && cur && cur[0] == sk.instance)
                continue;
            AppliedDesign rec;
            if (it != g_applied_designs.end()) {
                rec = it->second;
            } else if (cur) {
                rec.orig[0] = cur[0];
                rec.orig[1] = cur[1];
                rec.orig[2] = cur[2];
            }
            rec.applied[0] = sk.instance;
            rec.applied[1] = sk.type;
            rec.applied[2] = sk.group;
            thiscall<void>(a_CivSetDesign, gc, did, rec.applied);
            g_applied_designs[map_key] = rec;
            g_st.designs_synced++;
            if (H && M)
                H->log(M, SK_INFO,
                       "civ_rivals: synced guest %u design 0x%08X -> %08X into rival nation pol=0x%08X (BUG-1581)",
                       (unsigned)pid, (unsigned)did, (unsigned)sk.instance, (unsigned)g_nations[pid].pol_id);
        }
    }
}

static void save_guest_designs_strip(bool start)
{
    if (!is_host() || g_applied_designs.empty() || !civ::nm() || !civ::a_DesignKey)
        return;
    for (const auto& [k, rec] : g_applied_designs) {
        uint8_t pid = k.first;
        uint32_t did = k.second;
        if (pid >= kMaxPlayers || !g_nations[pid].assigned)
            continue;
        void* gc = civ_by_pol(g_nations[pid].pol_id);
        if (!gc)
            continue;
        uint32_t* slot = thiscall<uint32_t*>(civ::a_DesignKey, gc, (int)did);
        if (!slot || !skg::readable(slot, 12))
            continue;
        if (start && slot[0] == rec.applied[0]) {
            memcpy(slot, rec.orig, 12);
        } else if (!start && slot[0] == rec.orig[0] && slot[1] == rec.orig[1]) {
            memcpy(slot, rec.applied, 12);
        }
    }
}

// BUG-1581: restore guest-borrowed superweapon tools (+0x470) & isolated timers (+0x490) before save, world change,
// or cCivilization teardown. Never gate on mode_id() == kCiv: ~cCivilization runs synchronously during mode transitions
// and exit, where mode_id() may already have changed.
static void restore_guest_sw_tools()
{
    void* civ = g_guest_sw_borrow.civ;
    if (!civ)
        return;
    if (skg::readable(civ, kCivSwTimers + 12)) {
        if (g_guest_sw_borrow.tools && F<uint32_t>(civ, kCivSwTools) == g_guest_sw_borrow.borrowed_tools_begin) {
            F<uint32_t>(civ, kCivSwTools) = g_guest_sw_borrow.saved_tools[0];
            F<uint32_t>(civ, kCivSwTools + 4) = g_guest_sw_borrow.saved_tools[1];
            F<uint32_t>(civ, kCivSwTools + 8) = g_guest_sw_borrow.saved_tools[2];
        }
        if (g_guest_sw_borrow.timers && F<uint32_t>(civ, kCivSwTimers) == g_guest_sw_borrow.borrowed_timers_begin) {
            F<uint32_t>(civ, kCivSwTimers) = g_guest_sw_borrow.saved_timers[0];
            F<uint32_t>(civ, kCivSwTimers + 4) = g_guest_sw_borrow.saved_timers[1];
            F<uint32_t>(civ, kCivSwTimers + 8) = g_guest_sw_borrow.saved_timers[2];
        }
    }
    g_guest_sw_borrow = GuestSwBorrow{};
}

static void unalias_sw_on_dtor(void* civ)
{
    if (!civ)
        return;
    if (g_guest_sw_borrow.civ && (civ == g_guest_sw_borrow.civ || civ == g_guest_sw_borrow.source_civ))
        restore_guest_sw_tools();
    for (auto& [b, src] : g_active_sw_borrows) {
        if (!b || !b->civ)
            continue;
        if (b->civ == civ || src == civ) {
            if (skg::readable(b->civ, kCivSwTimers + 12)) {
                if (b->active_timers_borrow) {
                    F<uint32_t>(b->civ, kCivSwTimers) = b->saved_timers[0];
                    F<uint32_t>(b->civ, kCivSwTimers + 4) = b->saved_timers[1];
                    F<uint32_t>(b->civ, kCivSwTimers + 8) = b->saved_timers[2];
                }
                if (b->active_borrow) {
                    F<uint32_t>(b->civ, kCivSwTools) = b->saved[0];
                    F<uint32_t>(b->civ, kCivSwTools + 4) = b->saved[1];
                    F<uint32_t>(b->civ, kCivSwTools + 8) = b->saved[2];
                }
            }
            b->civ = nullptr;
        }
    }
    g_isolated_sw_timers.erase(civ);
}

static void SK_THISCALL h_CivDtor(void* civ)
{
    unalias_sw_on_dtor(civ);
    o_CivDtor(civ);
}

static void* SK_THISCALL h_CivScalarDtor(void* civ, uint32_t flags)
{
    unalias_sw_on_dtor(civ);
    return o_CivScalarDtor(civ, flags);
}

// Resolve cCivilization::~cCivilization (GOG 2024 0x00BF3830) via the live vtable slot 2 deleting destructor thunk
// (0x00BF4470: push esi; mov esi, ecx; call 0x00BF3830), falling back to hooking slot 2 directly.
static bool ensure_civ_dtor_hook(void* civ)
{
    if (o_CivDtor || o_CivScalarDtor)
        return true;
    if (g_civ_dtor_hook_tried || !H || !M || !civ || !skg::readable(civ, sizeof(uintptr_t)))
        return false;
    uintptr_t vt = F<uintptr_t>(civ, 0);
    if (!vt || !skg::readable((void*)vt, 3 * sizeof(uintptr_t)))
        return false;
    uintptr_t thunk = ((const uintptr_t*)vt)[2];
    if (!thunk || !skg::readable((void*)thunk, 8))
        return false;
    g_civ_dtor_hook_tried = true;
    const uint8_t* p = (const uint8_t*)thunk;
    if (p[0] == 0x56 && p[1] == 0x8B && p[2] == 0xF1 && p[3] == 0xE8) {
        int32_t rel = 0;
        memcpy(&rel, p + 4, 4);
        uintptr_t dtor = thunk + 8 + (uintptr_t)(intptr_t)rel;
        if (skg::readable((void*)dtor, 8) &&
            H->hook_create(M, (void*)dtor, (void*)h_CivDtor, (void**)&o_CivDtor) == 0 && o_CivDtor) {
            H->log(M, SK_INFO, "civ_rivals: hooked cCivilization::~cCivilization at %p via vtable thunk %p",
                   (void*)dtor, (void*)thunk);
            return true;
        }
    }
    if (H->hook_create(M, (void*)thunk, (void*)h_CivScalarDtor, (void**)&o_CivScalarDtor) == 0 && o_CivScalarDtor) {
        H->log(M, SK_INFO, "civ_rivals: hooked cCivilization deleting dtor thunk at %p", (void*)thunk);
        return true;
    }
    H->log(M, SK_WARN, "civ_rivals: failed to hook cCivilization dtor from vtable %p", (void*)vt);
    return false;
}

// Allocate or reuse a per-civ isolated cGameTime[12] buffer initialized from an existing cGameTime template
// (preserving vtable at +0x00 and clock function pointer at +0x1C with idle state: running=0, elapsed=0, start=0).
static uint32_t get_isolated_sw_timers(void* target_civ, void* template_civ)
{
    if (!target_civ || !template_civ)
        return 0;
    auto it = g_isolated_sw_timers.find(target_civ);
    if (it != g_isolated_sw_timers.end() && it->second)
        return (uint32_t)(uintptr_t)it->second->buf;
    if (!skg::readable(template_civ, kCivSwTimers + 12))
        return 0;
    uint32_t hm0 = F<uint32_t>(template_civ, kCivSwTimers);
    uint32_t hm1 = F<uint32_t>(template_civ, kCivSwTimers + 4);
    if (!sw_vec_valid(hm0, hm1, kSwTimersMinBytes) || !skg::readable((void*)(uintptr_t)hm0, 32))
        return 0;
    uint8_t tmpl[32];
    memcpy(tmpl, (const void*)(uintptr_t)hm0, 32);
    // Reset accumulated (+0x08..+0x0F), start (+0x10..+0x17), running (+0x18), and padding (+0x04..+0x07, +0x19..+0x1B).
    memset(tmpl + 4, 0, 24);
    auto entry = std::make_unique<IsolatedSwTimers>();
    for (int i = 0; i < 12; i++)
        memcpy(entry->buf + i * 32, tmpl, 32);
    uint32_t addr = (uint32_t)(uintptr_t)entry->buf;
    g_isolated_sw_timers[target_civ] = std::move(entry);
    return addr;
}

static void ensure_guest_sw_tools(void* my_civ)
{
    if (!g_fix_civ_guest_sw || !my_civ || !is_guest() || !active() || g_in_save)
        return;
    void* orig_pc = orig_player_civ();
    if (!orig_pc || orig_pc == my_civ)
        return;
    if (g_guest_sw_borrow.civ && (g_guest_sw_borrow.civ != my_civ || g_guest_sw_borrow.source_civ != orig_pc))
        restore_guest_sw_tools();
    if (!skg::readable(my_civ, kCivSwTimers + 12) || !skg::readable(orig_pc, kCivSwTimers + 12))
        return;
    // Never alias +0x470/+0x490 persistently without ~cCivilization hooked to un-alias before free/Release.
    if (!ensure_civ_dtor_hook(orig_pc))
        return;
    if (!g_guest_sw_borrow.tools) {
        uint32_t t0 = F<uint32_t>(my_civ, kCivSwTools);
        uint32_t t1 = F<uint32_t>(my_civ, kCivSwTools + 4);
        uint32_t h0 = F<uint32_t>(orig_pc, kCivSwTools);
        uint32_t h1 = F<uint32_t>(orig_pc, kCivSwTools + 4);
        uint32_t h2 = F<uint32_t>(orig_pc, kCivSwTools + 8);
        if (sw_borrow_needed(false, t0, t1, h0, h1, kSwToolsMinBytes)) {
            g_guest_sw_borrow.civ = my_civ;
            g_guest_sw_borrow.source_civ = orig_pc;
            g_guest_sw_borrow.saved_tools[0] = t0;
            g_guest_sw_borrow.saved_tools[1] = t1;
            g_guest_sw_borrow.saved_tools[2] = F<uint32_t>(my_civ, kCivSwTools + 8);
            g_guest_sw_borrow.borrowed_tools_begin = h0;
            F<uint32_t>(my_civ, kCivSwTools) = h0;
            F<uint32_t>(my_civ, kCivSwTools + 4) = h1;
            F<uint32_t>(my_civ, kCivSwTools + 8) = h2;
            g_guest_sw_borrow.tools = true;
            g_st.sw_guest_borrows++;
            if (H && M)
                H->log(M, SK_INFO, "civ_rivals: borrowed superweapon tools (+0x470) onto guest nation %p (BUG-1581)",
                       my_civ);
        }
    }
    if (!g_guest_sw_borrow.timers) {
        uint32_t t0 = F<uint32_t>(my_civ, kCivSwTimers);
        uint32_t t1 = F<uint32_t>(my_civ, kCivSwTimers + 4);
        uint32_t h0 = F<uint32_t>(orig_pc, kCivSwTimers);
        uint32_t h1 = F<uint32_t>(orig_pc, kCivSwTimers + 4);
        if (sw_borrow_needed(false, t0, t1, h0, h1, kSwTimersMinBytes)) {
            if (uint32_t iso = get_isolated_sw_timers(my_civ, orig_pc)) {
                g_guest_sw_borrow.civ = my_civ;
                g_guest_sw_borrow.source_civ = orig_pc;
                g_guest_sw_borrow.saved_timers[0] = t0;
                g_guest_sw_borrow.saved_timers[1] = t1;
                g_guest_sw_borrow.saved_timers[2] = F<uint32_t>(my_civ, kCivSwTimers + 8);
                g_guest_sw_borrow.borrowed_timers_begin = iso;
                F<uint32_t>(my_civ, kCivSwTimers) = iso;
                F<uint32_t>(my_civ, kCivSwTimers + 4) = iso + (uint32_t)kSwTimersMinBytes;
                F<uint32_t>(my_civ, kCivSwTimers + 8) = iso + (uint32_t)kSwTimersMinBytes;
                g_guest_sw_borrow.timers = true;
            }
        }
    }
}

void on_city_specialty_changed(void* city, void* civ, int new_spec)
{
    if (!active() || !city || !valid_native_specialty(new_spec))
        return;
    if (civ && a_CivRefreshUnlocks)
        thiscall<void>(a_CivRefreshUnlocks, civ);
    int pid = civ ? player_of_pol(civ::pol_of(civ)) : player_of_pol(civ::pol_of(city));
    if (pid >= 0 && pid < kMaxPlayers)
        ensure_civ_vehicle_designs(civ ? civ : civ_of_player((uint8_t)pid), (uint8_t)pid, new_spec);
    if (is_host())
        g_assign_dirty = true;
}

void note_local_spend(void* civ, float amount)
{
    if (!active() || !civ || civ != my_civ() || !std::isfinite(amount) || amount <= 0.0f)
        return;
    g_last_local_spend = {civ, amount, now_s()};
}

bool is_player_capital(void* city, uint8_t* owner_out)
{
    if (!city || !active())
        return false;
    return is_capital_city_pos(to_vec3(civ::city_pos(city)), g_nations, owner_out);
}

int my_capital_specialty()
{
    if (!active())
        return -1;
    uint8_t me = my_id();
    if (me < kMaxPlayers && g_nations[me].assigned) {
        if (valid_native_specialty(g_nations[me].cap_specialty))
            return g_nations[me].cap_specialty;
        if (void* mc = civ_by_pol(g_nations[me].pol_id)) {
            auto cs = civ::vec_of((uint8_t*)mc + civ::kCivCities);
            if (!cs.empty() && skg::readable(cs[0], civ::kCitySpecialty + 4)) {
                int sp = F<int>(cs[0], civ::kCitySpecialty);
                if (valid_native_specialty(sp))
                    return sp;
            }
        }
    }
    return -1;
}

int foreign_city_count(void* civ)
{
    if (!civ || !civ::nm())
        return 1;
    int total = (int)civ::all_cities().size();
    int own = (int)civ::vec_of((uint8_t*)civ + civ::kCivCities).size();
    return std::max(1, total - own);
}

// ---- Perspective swap restore & apply (R1) ----
static void restore_perspective()
{
    restore_guest_sw_tools();
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
    // BUG-1581: borrow +0x470 superweapon tools & +0x490 timers onto guest's NPC-origin civ so superweapon
    // targeting/HUD/firing never dereferences a null vector at 0x00BF18CA.
    ensure_guest_sw_tools(my_civ);

    // Sync authoritative wealth for guest's nation (zero NPC budgets so GetMoney == mWealth).
    // BUG-1581: NEVER overwrite mWealth while the city planner is open or a planner spend is in flight,
    // otherwise every frame snaps mWealth back to pre-spend value ("money didn't spend" in planner!).
    bool hold_wealth = civ::planner_city() != nullptr;
    if (!hold_wealth) {
        for (void* mc : civ::vec_of((uint8_t*)my_civ + civ::kCivCities))
            if (civ::planner_send_holds(mc)) {
                hold_wealth = true;
                break;
            }
    }
    const auto& me = g_nations[my_id()];
    if (!hold_wealth && std::isfinite(me.wealth) && me.wealth >= 0.0f) {
        F<float>(my_civ, kCivWealth) = me.wealth;
        F<float>(my_civ, kCivDomesticBudget) = 0.0f;
        F<float>(my_civ, kCivExpansionBudget) = 0.0f;
    }
    // Update city mbIsPlayerCity flags & ensure vehicle designs exist for every owned city specialty (BUG-1581)
    for (void* city : civ::all_cities()) {
        if (!city)
            continue;
        bool own = civ::pol_of(city) == my_pol;
        if (a_CitySetPlayerOwned)
            thiscall<void>(a_CitySetPlayerOwned, city, own);
        else if (skg::readable(city, kCityIsPlayer + 1))
            F<uint8_t>(city, kCityIsPlayer) = own ? 1 : 0;
        if (own && skg::readable(city, civ::kCitySpecialty + 4))
            ensure_civ_vehicle_designs(my_civ, my_id(), F<int>(city, civ::kCitySpecialty));
    }
    // BUG-1563: replicate converted capital specialties from SK_ASSIGN onto the guest's local cities
    if (g_fix_civ_spec_convert) {
        for (int i = 0; i < kMaxPlayers; i++) {
            if (!g_nations[i].assigned || !valid_native_specialty(g_nations[i].cap_specialty))
                continue;
            void* nc = civ_by_pol(g_nations[i].pol_id);
            void* cap_city = nullptr;
            if (nc) {
                auto ncs = civ::vec_of((uint8_t*)nc + civ::kCivCities);
                if (!ncs.empty())
                    cap_city = ncs[0];
            }
            if (!cap_city) {
                net::Vec3 cp{g_nations[i].cap_pos.x, g_nations[i].cap_pos.y, g_nations[i].cap_pos.z};
                cap_city = civ::local_city_near(cp);
            }
            if (!cap_city || !skg::readable(cap_city, civ::kCitySpecialty + 4))
                continue;
            int want = g_nations[i].cap_specialty;
            int cur_spec = F<int>(cap_city, civ::kCitySpecialty);
            int cur_cap = skg::readable(cap_city, 0x29C + 4) ? F<int>(cap_city, 0x29C) : want;
            if (cur_spec != want || cur_cap != want) {
                apply_city_specialty(cap_city, nc, want);
                if (i == my_id())
                    ensure_civ_vehicle_designs(my_civ, my_id(), want);
                if (cur_spec != want) {
                    g_st.spec_converted++;
                    if (H && M)
                        H->log(M, SK_INFO,
                               "civ_rivals: synced converted capital specialty %d -> %d (%s) for player %d on guest (BUG-1563)",
                               cur_spec, want, civ_specialty_name(want), i);
                }
            }
        }
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
        if (g_fix_civ_spec_pick && !g_spec_hint_shown && can_pick_my_specialty() && C.notice) {
            g_spec_hint_shown = true;
            int sp = my_capital_specialty();
            const char* sp_nm = i18n::tr(civ_specialty_name(sp), civ_specialty_name_ru(sp));
            char nb[220];
            snprintf(nb, sizeof nb, "%s: %s. %s",
                     i18n::tr("Starting specialty", "Стартовая специализация"),
                     sp_nm[0] ? sp_nm : i18n::tr("Unknown", "Неизвестно"),
                     i18n::tr("Choose Military / Religious / Economic in F9 -> Rivals (before 2nd city)",
                              "Выбрать Военную / Религиозную / Торговую можно в F9 -> Соперники (до 2-го города)"));
            C.notice(nb);
        }
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
    for (int i = 0; i < kMaxPlayers; i++) {
        g_nations[i] = PlayerNation{(uint8_t)i, 0, {}, 0.0f, false};
        g_slot_key[i].clear();
        g_last_city_counts[i] = 0;
    }
    g_saved_nations.clear();
    g_saved_store_serial = 0;
    g_npc_rels.clear();
    g_guest_npc_rels.clear();
    g_spec_hint_shown = false;
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
    g_applied_designs.clear();
    g_designs_dirty = true;
    g_last_local_spend = LocalSpend{};
    g_isolated_sw_timers.clear();
}

static void on_lifecycle_event(const lifecycle::Ctx& c)
{
    if (c.event == lifecycle::kPlayerLeave) {
        if (c.player >= 1 && c.player < kMaxPlayers && g_nations[c.player].assigned) {
            if (is_host() && game::mode_id() == modes::kCiv) {
                if (void* gc = civ_by_pol(g_nations[c.player].pol_id); gc && skg::readable(gc, kCivIsPlayer + 1))
                    F<uint8_t>(gc, kCivIsPlayer) = 0;
            }
            // BUG-1581: persist player_key -> pol_id across disconnect/reconnect instead of forgetting the nation!
            if (g_fix_civ_rejoin_nation) {
                std::string pkey = !g_slot_key[c.player].empty() ? g_slot_key[c.player] : player_stable_key(c.player);
                if (!pkey.empty() && g_nations[c.player].pol_id) {
                    g_saved_nations.remember(pkey, g_nations[c.player].pol_id, g_nations[c.player].cap_pos,
                                             g_nations[c.player].cap_specialty);
                    if (H && M)
                        H->log(M, SK_INFO,
                               "civ_rivals: preserved nation pol=0x%08X for disconnected player %u (key=%s, BUG-1581)",
                               (unsigned)g_nations[c.player].pol_id, (unsigned)c.player, pkey.c_str());
                }
            }
            g_nations[c.player] = PlayerNation{c.player, 0, {}, 0.0f, false};
            g_slot_key[c.player].clear();
            g_species_applied.erase(c.player);
            for (auto it = g_applied_designs.begin(); it != g_applied_designs.end();)
                it = (it->first.first == c.player) ? g_applied_designs.erase(it) : std::next(it);
            g_assign_dirty = true;
        }
        return;
    }
    reset_state();
}

LIFECYCLE_HOOK("civ_rivals.lifecycle", lifecycle::kSessionEnd | lifecycle::kWorldChange | lifecycle::kPlayerLeave, -20,
               on_lifecycle_event, false);

// BUG-1581: store persistence for player_key -> rival nation pol_id across host saves/reloads.
static void load_saved_nations_from_store()
{
    if (!g_fix_civ_rejoin_nation || !is_host())
        return;
    pstore::Store* w = store::world();
    if (!w || store::serial() == g_saved_store_serial)
        return;
    g_saved_store_serial = store::serial();
    if (const pstore::Record* r = w->get_world("civ.rivals_nations"); r && r->ver == 1 && !r->data.empty()) {
        net::Reader rd(r->data.data(), r->data.size());
        NationMemory loaded;
        if (read_saved_nations(rd, loaded)) {
            for (const auto& [k, sv] : loaded.by_key)
                if (!g_saved_nations.has(k))
                    g_saved_nations.by_key[k] = sv;
            if (H && M)
                H->log(M, SK_INFO, "civ_rivals: loaded %u persisted rival nation claim(s) from world store (BUG-1581)",
                       (unsigned)loaded.by_key.size());
        }
    }
}

static void save_nations_to_store(pstore::Store& s, int64_t now)
{
    if (!g_fix_civ_rejoin_nation || &s != store::world() || game::mode_id() != modes::kCiv)
        return;
    if (g_saved_nations.by_key.empty() && !s.get_world("civ.rivals_nations"))
        return;
    net::Writer w;
    write_saved_nations(w, g_saved_nations);
    s.put_world("civ.rivals_nations", 1, w.data);
    std::vector<std::string> keys;
    for (const auto& [k, sv] : g_saved_nations.by_key) {
        (void)sv;
        keys.push_back(k);
    }
    store::mark_owners(s, "civ.rivals_nation", keys, now);
}

static bool g_rivals_store_reg = store::add(
    {"civ.rivals_nations", save_nations_to_store,
     [](const std::string& from, const std::string& to) {
         g_saved_nations.adopt(from, to);
         for (int i = 0; i < kMaxPlayers; i++)
             if (g_slot_key[i] == from)
                 g_slot_key[i] = to;
     },
     [] { load_saved_nations_from_store(); }});

static bool g_rivals_designs_reg = species::on_change("civ_rivals.designs", [](uint8_t) { g_designs_dirty = true; });

static void on_save_start()
{
    g_in_save = true;
    restore_perspective();
    save_guest_designs_strip(true);
}

static void on_save_end()
{
    g_in_save = false;
    save_guest_designs_strip(false);
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
        uint32_t pol = civ::pol_of(civ);
        int pid = player_of_pol(pol);
        if (is_host() && ((pid >= 1 && pid < kMaxPlayers) ||
                          (g_fix_civ_rejoin_nation && g_saved_nations.is_reserved_pol(pol)))) {
            // R2: guest-assigned (or temporarily disconnected) nation on host: skip NPC AI planner/diplomacy/growth/budgets,
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
    if (active() && is_guest() && g_fix_civ_guest_sw) {
        void* mc = civ_of_player(my_id());
        SwToolsBorrow guard(mc);
        return o_CanTargetSW(sw_type, target);
    }
    return o_CanTargetSW(sw_type, target);
}

static void host_broadcast_assign(double now, int only_peer);

// BUG-1581: vanilla Civ diplomacy gift (0x00DCA95C -> SpendMoney + ApplyRelationship(target, sender, 0x0526E4FB, 1.0f))
// never called AddMoney on the target civilization. Called from tribe_rivals.cpp's shared ApplyRelationship hook
// to credit the recipient rival player's mWealth directly (or relay to host if initiated by guest).
void on_rel_apply(uint32_t polA, uint32_t polB, uint32_t event_id, float /*value*/)
{
    if (!g_fix_civ_diplo_gift || !active() || event_id != kEventCivDiplomaticGift)
        return;
    double now = now_s();
    if (g_last_local_spend.amount <= 0.0f || !g_last_local_spend.civ || now - g_last_local_spend.at > 0.15)
        return;
    // Native 0x00DCA956..0x00DCA988 calls SpendMoney(sender) then ApplyRelationship(target=polA, sender=polB, ...).
    int pa = player_of_pol(polA);
    int pb = player_of_pol(polB);
    uint8_t me = my_id();
    if (pb != (int)me || pa < 0 || pa >= kMaxPlayers || pa == (int)me)
        return;
    void* sender_civ = civ_by_pol(polB);
    if (!sender_civ || g_last_local_spend.civ != sender_civ)
        return;
    float amt = g_last_local_spend.amount;
    int target_pid = pa;
    g_last_local_spend = LocalSpend{}; // consume so it cannot double-credit
    if (is_host()) {
        host_credit_player((uint8_t)target_pid, amt);
        g_st.diplo_gifts++;
        host_broadcast_assign(now, -1);
        if (H && M)
            H->log(M, SK_INFO,
                   "civ_rivals: credited vanilla diplomatic gift of %.0f Sporebucks from host to player %d (BUG-1581)",
                   amt, target_pid);
        if (C.notice && C.sess) {
            char b[160];
            snprintf(b, sizeof b, "%s gifted %.0f Sporebucks to %s", C.sess->name_of(0).c_str(), amt,
                     C.sess->name_of((uint8_t)target_pid).c_str());
            C.notice(b);
        }
    } else if (is_guest()) {
        net::Writer w;
        write_diplo_gift(w, {(uint8_t)target_pid, amt});
        if (rivals::send_faction_to(0, rivals::Stage::Civ, kSkDiploGift, w))
            g_st.gifts_sent++;
    }
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
    // Refresh live wealths and capital specialties before sending
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!g_nations[i].assigned)
            continue;
        if (void* c = civ_by_pol(g_nations[i].pol_id)) {
            if (skg::readable(c, kCivWealth + 4))
                g_nations[i].wealth = F<float>(c, kCivWealth);
            auto cs = civ::vec_of((uint8_t*)c + civ::kCivCities);
            if (!cs.empty() && skg::readable(cs[0], civ::kCitySpecialty + 4)) {
                int sp = F<int>(cs[0], civ::kCitySpecialty);
                if (valid_native_specialty(sp))
                    g_nations[i].cap_specialty = (int8_t)sp;
            }
        }
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
    load_saved_nations_from_store();
    auto host_cities = civ::vec_of((uint8_t*)host_civ + civ::kCivCities);
    if (host_cities.empty())
        return;

    uint32_t host_pol = civ::pol_of(host_civ);
    Vec3 host_cap = to_vec3(civ::city_pos(host_cities[0]));
    int host_spec = skg::readable(host_cities[0], civ::kCitySpecialty + 4) ? F<int>(host_cities[0], civ::kCitySpecialty) : -1;
    int8_t host_spec8 = valid_native_specialty(host_spec) ? (int8_t)host_spec : (int8_t)-1;
    if (!g_nations[0].assigned || g_nations[0].pol_id != host_pol) {
        g_nations[0] = {0, host_pol, host_cap, F<float>(host_civ, kCivWealth), true, host_spec8};
        g_slot_key[0] = "host";
        g_assign_dirty = true;
    } else {
        g_nations[0].wealth = F<float>(host_civ, kCivWealth);
        g_nations[0].cap_specialty = host_spec8;
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

    // Build candidate list from free NPC civilizations with at least 1 city (including city size metadata, BUG-1581)
    std::vector<BaseCandidate> cands;
    std::map<uint32_t, int> cand_native_spec;
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
        int slots = city_building_slot_count(cs[0]);
        bool small = city_is_small(cs[0]);
        cands.push_back({pol, to_vec3(civ::city_pos(cs[0])), (int)cs.size(), slots, small});
        cand_native_spec[pol] =
            skg::readable(cs[0], civ::kCitySpecialty + 4) ? F<int>(cs[0], civ::kCitySpecialty) : -1;
    }

    bool need_npc_spawn = false;
    for (const auto& pl : C.sess->players()) {
        if (pl.id == 0 || pl.id >= kMaxPlayers || g_nations[pl.id].assigned)
            continue;
        std::string pkey = player_stable_key(pl.id);
        uint32_t pick = 0;
        bool reclaimed = false;
        // BUG-1581: first reclaim this player's persisted rival nation if they disconnected & rejoined!
        if (g_fix_civ_rejoin_nation && !pkey.empty()) {
            pick = g_saved_nations.find_reclaim(pkey, cands, taken);
            if (pick)
                reclaimed = true;
        }

        // BUG-1557 / BUG-1563: determine the guest's desired Civ specialty from its Tribe consequence card
        // (Aggressive->Mil 0, Friendly->Rel 1, Industrious->Eco 2). Prefer a free NPC nation matching it when the
        // guest has its own Rivals Tribe path; if no free NPC matches (or all players finished Tribe with the same
        // card), convert the taken-over capital city specialty to match (rivals.civ_spec_convert).
        int want_native = -1;
        uint32_t tcard = 0;
        if (!reclaimed) {
            bool own_tribe = history::rivals_own_history_enabled() &&
                             (rivals::stage_is_rivals(rivals::Stage::Tribe) || history::player_has_own_tribe(pl.id));
            if (history::rivals_own_history_enabled())
                history::host_sync_now();
            tcard = history::player_card(pl.id, history::kTribe);
            if (!tcard && !own_tribe)
                tcard = history::player_card(0, history::kTribe);
            int pref_h = history::civ_spec_for_tribe_card(tcard);
            if (pref_h >= 0)
                want_native = history::history_spec_to_native_city((uint8_t)(pref_h + 1));

            // Exclude nations reserved for OTHER disconnected players when unreserved NPCs exist (BUG-1581)
            std::vector<BaseCandidate> pool;
            if (g_fix_civ_rejoin_nation) {
                for (const auto& bc : cands)
                    if (!g_saved_nations.is_reserved_for_other(bc.pol_id, pkey))
                        pool.push_back(bc);
            }
            if (pool.empty())
                pool = cands;

            if (own_tribe && want_native >= 0) {
                std::vector<BaseCandidate> matched;
                for (const auto& bc : pool) {
                    auto it = cand_native_spec.find(bc.pol_id);
                    if (it != cand_native_spec.end() && it->second == want_native)
                        matched.push_back(bc);
                }
                if (!matched.empty())
                    pick = select_base_nation(host_cap, matched, rivals::base_distance(), taken, g_fix_civ_large_start);
            }
            if (!pick)
                pick = select_base_nation(host_cap, pool, rivals::base_distance(), taken, g_fix_civ_large_start);
        }
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
        int cap_native = skg::readable(gcities[0], civ::kCitySpecialty + 4) ? F<int>(gcities[0], civ::kCitySpecialty) : -1;
        if (!reclaimed && g_fix_civ_spec_convert && needs_specialty_conversion(cap_native, want_native)) {
            int old_native = cap_native;
            apply_city_specialty(gcities[0], gc, want_native);
            cap_native = want_native;
            g_st.spec_converted++;
            if (H && M)
                H->log(M, SK_INFO,
                       "civ_rivals: converted player %u capital city specialty %d -> %d (%s) to match Tribe card 0x%08X (BUG-1563)",
                       (unsigned)pl.id, old_native, want_native, civ_specialty_name(want_native), (unsigned)tcard);
        }
        uint8_t hist_spec = history::native_city_spec_to_history(cap_native);
        if (hist_spec != history::kCivSpecNone)
            history::set_civ_start_specialty(pl.id, hist_spec);

        // R2 takeover: fold NPC domestic/expansion budgets back into mWealth and enforce baseline floor on fresh takeover
        // (preserve exact mWealth on rejoin reclaim!). Keep +0x89 (mIsPlayer) == 0 on host to avoid recursive diplomacy crash.
        float w = F<float>(gc, kCivWealth) + F<float>(gc, kCivDomesticBudget) + F<float>(gc, kCivExpansionBudget);
        if (!std::isfinite(w) || (!reclaimed && w < kStartingWealthFloor))
            w = kStartingWealthFloor;
        else if (w < 0.0f)
            w = 0.0f;
        F<float>(gc, kCivWealth) = w;
        F<float>(gc, kCivDomesticBudget) = 0.0f;
        F<float>(gc, kCivExpansionBudget) = 0.0f;
        F<uint8_t>(gc, kCivIsPlayer) = 0;

        int8_t cap_spec8 = valid_native_specialty(cap_native) ? (int8_t)cap_native : (int8_t)-1;
        g_nations[pl.id] = {pl.id, pick, cap, w, true, cap_spec8};
        g_slot_key[pl.id] = pkey;
        if (g_fix_civ_rejoin_nation && !pkey.empty())
            g_saved_nations.remember(pkey, pick, cap, cap_spec8);
        taken.push_back(pick);
        g_npc_rels.remove_npc(pick); // no longer an NPC nation
        host_apply_guest_species(pl.id, gc);
        ensure_civ_vehicle_designs(gc, pl.id, cap_native);
        g_designs_dirty = true;
        if (reclaimed)
            g_st.reclaimed++;
        else
            g_st.takeovers++;
        g_assign_dirty = true;
        g_npcrel_dirty = true;

        if (H && M)
            H->log(M, SK_INFO,
                   "civ_rivals: player %u (%s) %s NPC nation pol=0x%08X (dist=%s, d=%.1f m, small=%d, slots=%d, wealth=%.0f, %s)",
                   (unsigned)pl.id, pl.name.c_str(), reclaimed ? "reclaimed" : "took over", (unsigned)pick,
                   rivals::distance_name(rivals::base_distance()), dist3(host_cap, cap), (int)city_is_small(gcities[0]),
                   city_building_slot_count(gcities[0]), w, reclaimed ? "BUG-1581" : "BUG-1549");
        if (C.notice) {
            std::string msg = format_rival_takeover_notice(pl.name, civ_color_name(gc), cap_native, reclaimed);
            C.notice(msg);
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

    // 2. Bidirectionally sync remaining NPC nations between cRelationshipManager and g_npc_rels (BUG-1555).
    // Never overwrite real in-game diplomacy (gifts, insults, war declarations, alliances): read native deltas
    // back into g_npc_rels, and only push to native when Rivals influence/actions marked a slot dirty.
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
            g_npcrel_dirty = true;
        }
        for (int p = 0; p < kMaxPlayers; p++) {
            if (!g_nations[p].assigned)
                continue;
            float native_val = rel_get(pol, g_nations[p].pol_id);
            NativeSyncResult sr = g_npc_rels.sync_native_slot(pol, (uint8_t)p, native_val);
            if (sr.action == NativeSyncAction::PushNative) {
                rel_set(pol, g_nations[p].pol_id, sr.target_native);
                g_npc_rels.confirm_native_push(pol, (uint8_t)p, rel_get(pol, g_nations[p].pol_id));
            } else if (sr.action == NativeSyncAction::ReadInit) {
                g_npcrel_dirty = true;
            } else if (sr.action == NativeSyncAction::ReadBack) {
                g_st.rel_readbacks++;
                g_npcrel_dirty = true;
                if (sr.influence_award > 0) {
                    rivals::grant_influence((uint8_t)p, sr.influence_award,
                                            sr.newly_allied ? "npc alliance" : "npc diplomacy");
                    g_st.inf_diplomacy++;
                }
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

static void host_push_guest_civ_cities()
{
    if (!active() || !is_host())
        return;
    for (uint8_t pid = 1; pid < kMaxPlayers; pid++) {
        if (!g_nations[pid].assigned)
            continue;
        void* c = civ_by_pol(g_nations[pid].pol_id);
        if (!c)
            continue;
        int cnt = (int)civ::vec_of((uint8_t*)c + civ::kCivCities).size();
        uint16_t clamped = clamp_space_handoff_cities(cnt);
        g_last_city_counts[pid] = clamped;
        spacerivals::set_guest_civ_cities(pid, clamped);
    }
}

static void host_deliver_tribe_draft_payouts()
{
    if (!active() || !is_host())
        return;
    const auto& dr = rivals::draft_state();
    for (uint8_t pid = 0; pid < kMaxPlayers; pid++) {
        if (dr.pending_payout[pid] <= 0)
            continue;
        if (dr.pending_payout_stage[pid] != rivals::Stage::None &&
            dr.pending_payout_stage[pid] != rivals::Stage::Tribe)
            continue;
        if (!g_nations[pid].assigned || !civ_of_player(pid))
            continue; // wait until player's Civ nation exists on host
        int32_t raw = rivals::take_pending_draft_payout_for(rivals::Stage::Tribe, pid);
        if (raw <= 0)
            continue;
        float amt = civ_draft_payout_money(raw);
        host_credit_player(pid, amt);
        g_st.draft_payouts++;
        if (H && M)
            H->log(M, SK_INFO, "civ_rivals: delivered Tribe draft payout raw=%d (+%.0f Sporebucks) to player %u",
                   (int)raw, amt, (unsigned)pid);
        if (C.notice && C.sess) {
            char b[160];
            snprintf(b, sizeof b, "Tribe draft payout: %s received +%.0f Sporebucks",
                     C.sess->name_of(pid).c_str(), amt);
            C.notice(b);
        }
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

    // Keep Space Rivals updated with each guest's live Civ city count (clamped 1..3) for Civ -> Space handoff.
    for (uint8_t pid = 1; pid < kMaxPlayers; pid++) {
        if (!cc.present[pid])
            continue;
        uint16_t clamped = clamp_space_handoff_cities(cc.total_cities[pid]);
        g_last_city_counts[pid] = clamped;
        spacerivals::set_guest_civ_cities(pid, clamped);
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

float civ_money(void* civ)
{
    if (!civ || !skg::readable(civ, kCivWealth + 4))
        return 0.0f;
    float w = F<float>(civ, kCivWealth);
    return std::isfinite(w) && w > 0.0f ? w : 0.0f;
}

bool host_spend_civ_money(void* civ, float cost)
{
    if (!active() || !is_host() || !civ || !skg::readable(civ, kCivExpansionBudget + 4))
        return false;
    spend_direct_wealth(F<float>(civ, kCivWealth), F<float>(civ, kCivDomesticBudget),
                        F<float>(civ, kCivExpansionBudget), cost);
    int pid = player_of_pol(civ::pol_of(civ));
    if (pid >= 0 && pid < kMaxPlayers)
        g_nations[pid].wealth = F<float>(civ, kCivWealth);
    g_assign_dirty = true;
    return true;
}

bool host_charge_player(uint8_t pid, float amount)
{
    if (!active() || !is_host() || !std::isfinite(amount) || amount <= 0.0f)
        return false;
    void* c = civ_of_player(pid);
    if (!c || !skg::readable(c, kCivExpansionBudget + 4))
        return false;
    if (!deduct_direct_wealth(F<float>(c, kCivWealth), F<float>(c, kCivDomesticBudget),
                              F<float>(c, kCivExpansionBudget), amount))
        return false;
    g_nations[pid].wealth = F<float>(c, kCivWealth);
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

void host_broadcast_assign_now()
{
    if (is_host() && active())
        host_broadcast_assign(now_s());
}

void host_finalize_sw_spend(uint8_t from, void* civ, float before_wealth, float cost)
{
    if (!active() || !is_host() || !civ || !skg::readable(civ, kCivExpansionBudget + 4))
        return;
    float target = finalize_superweapon_wealth(before_wealth, cost, F<float>(civ, kCivDomesticBudget),
                                               F<float>(civ, kCivExpansionBudget));
    F<float>(civ, kCivWealth) = target;
    if (from < kMaxPlayers && g_nations[from].assigned)
        g_nations[from].wealth = target;
    g_st.sw_spent++;
    host_broadcast_assign(now_s());
    if (H && M)
        H->log(M, SK_INFO,
               "civ_rivals: guest %u superweapon spend %.0f -> %.0f (cost=%.0f, sw_spent=%u, BUG-1563)",
               (unsigned)from, before_wealth, target, cost, g_st.sw_spent);
}

void record_sw_refused_funds() { g_st.sw_refused_funds++; }

SwToolsBorrow::SwToolsBorrow(void* target_civ)
{
    if (!target_civ || !active())
        return;
    if (!is_host() && !g_fix_civ_guest_sw)
        return;
    void* host_civ = orig_player_civ();
    if (!host_civ || host_civ == target_civ)
        return;
    if (!skg::readable(target_civ, kCivSwTimers + 12) || !skg::readable(host_civ, kCivSwTimers + 12))
        return;
    ensure_civ_dtor_hook(host_civ);
    civ = target_civ;
    uint32_t t0 = F<uint32_t>(target_civ, kCivSwTools);
    uint32_t t1 = F<uint32_t>(target_civ, kCivSwTools + 4);
    uint32_t h0 = F<uint32_t>(host_civ, kCivSwTools);
    uint32_t h1 = F<uint32_t>(host_civ, kCivSwTools + 4);
    uint32_t h2 = F<uint32_t>(host_civ, kCivSwTools + 8);
    if (sw_borrow_needed(false, t0, t1, h0, h1, kSwToolsMinBytes)) {
        saved[0] = t0;
        saved[1] = t1;
        saved[2] = F<uint32_t>(target_civ, kCivSwTools + 8);
        mark = game::swap_open(game::swap_undo_u32, (uint8_t*)target_civ + kCivSwTools, saved[0], "civ_rivals: sw tools[0]");
        game::swap_open(game::swap_undo_u32, (uint8_t*)target_civ + kCivSwTools + 4, saved[1], "civ_rivals: sw tools[1]");
        game::swap_open(game::swap_undo_u32, (uint8_t*)target_civ + kCivSwTools + 8, saved[2], "civ_rivals: sw tools[2]");
        F<uint32_t>(target_civ, kCivSwTools) = h0;
        F<uint32_t>(target_civ, kCivSwTools + 4) = h1;
        F<uint32_t>(target_civ, kCivSwTools + 8) = h2;
        active_borrow = true;
    }
    uint32_t tm0 = F<uint32_t>(target_civ, kCivSwTimers);
    uint32_t tm1 = F<uint32_t>(target_civ, kCivSwTimers + 4);
    uint32_t hm0 = F<uint32_t>(host_civ, kCivSwTimers);
    uint32_t hm1 = F<uint32_t>(host_civ, kCivSwTimers + 4);
    if (sw_borrow_needed(false, tm0, tm1, hm0, hm1, kSwTimersMinBytes)) {
        if (uint32_t iso = get_isolated_sw_timers(target_civ, host_civ)) {
            saved_timers[0] = tm0;
            saved_timers[1] = tm1;
            saved_timers[2] = F<uint32_t>(target_civ, kCivSwTimers + 8);
            mark_timers = game::swap_open(game::swap_undo_u32, (uint8_t*)target_civ + kCivSwTimers, saved_timers[0],
                                          "civ_rivals: sw timers[0]");
            game::swap_open(game::swap_undo_u32, (uint8_t*)target_civ + kCivSwTimers + 4, saved_timers[1],
                            "civ_rivals: sw timers[1]");
            game::swap_open(game::swap_undo_u32, (uint8_t*)target_civ + kCivSwTimers + 8, saved_timers[2],
                            "civ_rivals: sw timers[2]");
            F<uint32_t>(target_civ, kCivSwTimers) = iso;
            F<uint32_t>(target_civ, kCivSwTimers + 4) = iso + (uint32_t)kSwTimersMinBytes;
            F<uint32_t>(target_civ, kCivSwTimers + 8) = iso + (uint32_t)kSwTimersMinBytes;
            active_timers_borrow = true;
        }
    }
    if (active_borrow || active_timers_borrow)
        g_active_sw_borrows.push_back({this, host_civ});
}

SwToolsBorrow::~SwToolsBorrow()
{
    if (active_borrow || active_timers_borrow) {
        auto it = std::find_if(g_active_sw_borrows.begin(), g_active_sw_borrows.end(),
                               [this](const auto& p) { return p.first == this; });
        if (it != g_active_sw_borrows.end())
            g_active_sw_borrows.erase(it);
    }
    if (active_timers_borrow) {
        if (civ && skg::readable(civ, kCivSwTimers + 12)) {
            F<uint32_t>(civ, kCivSwTimers) = saved_timers[0];
            F<uint32_t>(civ, kCivSwTimers + 4) = saved_timers[1];
            F<uint32_t>(civ, kCivSwTimers + 8) = saved_timers[2];
        }
        game::swap_close(mark_timers, 3);
    }
    if (active_borrow) {
        if (civ && skg::readable(civ, kCivSwTools + 12)) {
            F<uint32_t>(civ, kCivSwTools) = saved[0];
            F<uint32_t>(civ, kCivSwTools + 4) = saved[1];
            F<uint32_t>(civ, kCivSwTools + 8) = saved[2];
        }
        game::swap_close(mark, 3);
    }
}

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
        // BUG-1557: record guest's Civ outcome specialty (cCity +0x29C capture type: 0=Mil, 1=Rel, 2=Eco)
        if (new_pid > 0) {
            int cap_type = skg::readable(city, 0x29C + 4) ? F<int>(city, 0x29C) : -1;
            if (cap_type < 0 || cap_type > 2) {
                void* nc = civ_of_player((uint8_t)new_pid);
                auto ncs = nc ? civ::vec_of((uint8_t*)nc + civ::kCivCities) : std::vector<void*>{};
                if (!ncs.empty() && skg::readable(ncs[0], civ::kCitySpecialty + 4))
                    cap_type = F<int>(ncs[0], civ::kCitySpecialty);
                else if (skg::readable(city, civ::kCitySpecialty + 4))
                    cap_type = F<int>(city, civ::kCitySpecialty);
            }
            uint8_t hist_spec = history::native_city_spec_to_history(cap_type);
            if (hist_spec != history::kCivSpecNone)
                history::record_civ_outcome((uint8_t)new_pid, hist_spec);
            if (skg::readable(city, civ::kCitySpecialty + 4))
                ensure_civ_vehicle_designs(civ_of_player((uint8_t)new_pid), (uint8_t)new_pid,
                                           F<int>(city, civ::kCitySpecialty));
        }
        if (C.feed && C.sess)
            C.feed(C.sess->name_of((uint8_t)new_pid) + " captured a city");
    } else {
        civ::g_city_owner.erase(city);
    }
    host_push_guest_civ_cities();
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

// ---- BUG-1590: Guest Civ Rivals starting specialty choice (Military / Religious / Economic) ----
bool can_pick_my_specialty()
{
    if (!g_fix_civ_spec_pick || !active() || !is_guest())
        return false;
    uint8_t me = my_id();
    if (me == 0 || me >= kMaxPlayers || !g_nations[me].assigned)
        return false;
    void* mc = civ_by_pol(g_nations[me].pol_id);
    if (!mc)
        return false;
    int cities = (int)civ::vec_of((uint8_t*)mc + civ::kCivCities).size();
    return can_pick_starting_specialty(true, cities, kCitySpecMilitary);
}

static std::string host_apply_spec_pick(uint8_t pid, int target_spec)
{
    if (!g_fix_civ_spec_pick || !active() || !is_host())
        return "not host in Civ Rivals";
    if (pid == 0 || pid >= kMaxPlayers || !g_nations[pid].assigned)
        return "player nation not assigned";
    if (!valid_native_specialty(target_spec))
        return "invalid specialty";
    void* gc = civ_by_pol(g_nations[pid].pol_id);
    if (!gc)
        return "player nation not found";
    auto cs = civ::vec_of((uint8_t*)gc + civ::kCivCities);
    if (!can_pick_starting_specialty(true, (int)cs.size(), target_spec))
        return i18n::tr("Cannot change starting specialty after capturing another city",
                        "Нельзя сменить стартовую специализацию после захвата второго города");
    int old_spec = skg::readable(cs[0], civ::kCitySpecialty + 4) ? F<int>(cs[0], civ::kCitySpecialty) : -1;
    apply_city_specialty(cs[0], gc, target_spec);
    ensure_civ_vehicle_designs(gc, pid, target_spec);
    g_nations[pid].cap_specialty = (int8_t)target_spec;
    std::string pkey = !g_slot_key[pid].empty() ? g_slot_key[pid] : player_stable_key(pid);
    if (g_fix_civ_rejoin_nation && !pkey.empty())
        g_saved_nations.remember(pkey, g_nations[pid].pol_id, g_nations[pid].cap_pos, (int8_t)target_spec);
    uint8_t hist_spec = history::native_city_spec_to_history(target_spec);
    if (hist_spec != history::kCivSpecNone)
        history::set_civ_start_specialty(pid, hist_spec);
    g_st.spec_picked++;
    if (old_spec != target_spec)
        g_st.spec_converted++;
    g_designs_dirty = true;
    host_broadcast_assign(now_s());
    if (H && M)
        H->log(M, SK_INFO, "civ_rivals: player %u (%s) picked starting specialty %d -> %d (%s, BUG-1590)",
               (unsigned)pid, C.sess ? C.sess->name_of(pid).c_str() : "?", old_spec, target_spec,
               civ_specialty_name(target_spec));
    if (C.notice && C.sess) {
        char b[160];
        snprintf(b, sizeof b, "%s: %s (%s)", C.sess->name_of(pid).c_str(),
                 i18n::tr("starting specialty", "стартовая специализация"),
                 i18n::tr(civ_specialty_name(target_spec), civ_specialty_name_ru(target_spec)));
        C.notice(b);
    }
    return "";
}

std::string request_spec_pick(int target_spec)
{
    if (!g_fix_civ_spec_pick)
        return "disabled by rivals.civ_spec_pick";
    if (!active())
        return "Civ Rivals not active";
    if (!valid_native_specialty(target_spec))
        return "invalid specialty";
    if (!is_guest())
        return "guest only";
    if (!can_pick_my_specialty())
        return i18n::tr("Cannot change starting specialty after capturing another city",
                        "Нельзя сменить стартовую специализацию после захвата второго города");
    net::Writer w;
    write_spec_pick(w, {(uint8_t)target_spec});
    if (!rivals::send_faction_to(0, rivals::Stage::Civ, kSkSpecPick, w))
        return "host does not support MSG_RIVALS_FACTION";
    return "";
}

bool on_tribe_card_picked(uint8_t pid, uint32_t tribe_card)
{
    if (!g_fix_civ_spec_pick || !active() || !is_host() || pid == 0 || pid >= kMaxPlayers)
        return false;
    int pref_h = history::civ_spec_for_tribe_card(tribe_card);
    if (pref_h < 0)
        return false;
    int want_native = history::history_spec_to_native_city((uint8_t)(pref_h + 1));
    if (!valid_native_specialty(want_native))
        return false;
    return host_apply_spec_pick(pid, want_native).empty();
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
    if (subkind == kSkDiploGift) {
        if (!g_fix_civ_diplo_gift || !is_host() || from == 0 || from >= kMaxPlayers)
            return;
        DiploGiftMsg dg;
        if (!read_diplo_gift(r, dg))
            return;
        void* src = civ_of_player(from);
        void* dst = civ_of_player(dg.target_pid);
        if (!src || !dst || !can_apply_diplo_gift(from, dg.target_pid, dg.amount, civ_money(src))) {
            g_st.gifts_rejected++;
            return;
        }
        if (host_charge_player(from, dg.amount)) {
            host_credit_player(dg.target_pid, dg.amount);
            g_st.diplo_gifts++;
            host_broadcast_assign(now_s());
            if (C.notice && C.sess) {
                char b[160];
                snprintf(b, sizeof b, "%s gifted %.0f Sporebucks to %s", C.sess->name_of(from).c_str(), dg.amount,
                         C.sess->name_of(dg.target_pid).c_str());
                C.notice(b);
            }
        }
        return;
    }
    if (subkind == kSkSpecPick) {
        if (!g_fix_civ_spec_pick || !is_host() || from == 0 || from >= kMaxPlayers)
            return;
        SpecPickMsg sp;
        if (!read_spec_pick(r, sp))
            return;
        host_apply_spec_pick(from, (int)sp.specialty);
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
        g_npc_rels.confirm_native_push(npc_id, rival_id, rel_get(npc_id, g_nations[rival_id].pol_id));
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
        g_npc_rels.confirm_native_push(npc_id, actor, rel_get(npc_id, g_nations[actor].pol_id));
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
    host_push_guest_civ_cities();

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
    uint32_t kGold = 0xFFF2D479, kGreen = 0xFF7BE882;
    char buf[192];
    rows.push_back({i18n::tr("Civ Rivals: Nations & Capitals", "Цивилизация Rivals: Нации и столицы"), "", nullptr, kGold});
    uint8_t me = my_id();
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!g_nations[i].assigned)
            continue;
        std::string nm = C.sess->name_of((uint8_t)i);
        bool is_ally = (i != me) && rivals::allied(me, (uint8_t)i);
        const char* sp_nm = i18n::tr(civ_specialty_name(g_nations[i].cap_specialty),
                                     civ_specialty_name_ru(g_nations[i].cap_specialty));
        if (sp_nm[0])
            snprintf(buf, sizeof buf, "  %s [%s]: $%.0f (%s)", nm.c_str(), sp_nm, g_nations[i].wealth,
                     i18n::tr("capital protected", "столица защищена"));
        else
            snprintf(buf, sizeof buf, "  %s: $%.0f (%s)", nm.c_str(), g_nations[i].wealth,
                     i18n::tr("capital protected", "столица защищена"));
        uint8_t tid = (uint8_t)i;
        rows.push_back({buf, is_ally ? i18n::tr("Gift $500", "Дар $500") : "",
                        is_ally ? std::function<void()>([tid] { request_gift(tid, GiftKind::Money, 500.0f); }) : nullptr,
                        i == me ? kGreen : 0});
    }
    return rows;
}

static CivNpcSnapshot snapshot_civ_npc(uint32_t pol, float viewer_rel, bool any_rival_turnable)
{
    CivNpcSnapshot s;
    s.pol_id = pol;
    s.viewer_rel = viewer_rel;
    s.any_rival_turnable = any_rival_turnable;
    if (void* c = civ_by_pol(pol)) {
        auto cs = civ::vec_of((uint8_t*)c + civ::kCivCities);
        s.cities = (int)cs.size();
        if (!cs.empty() && skg::readable(cs[0], civ::kCitySpecialty + 4))
            s.specialty = F<int>(cs[0], civ::kCitySpecialty);
        if (skg::readable(c, 0x64)) {
            uint32_t cid = F<uint32_t>(c, 0x38); // cIdentityColorable::mIDColorID (0x053DBCxx)
            float r = F<float>(c, 0x58), g = F<float>(c, 0x5C), b = F<float>(c, 0x60);
            if ((cid >> 8) == 0x053DBC && std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && r >= 0.0f &&
                r <= 1.001f && g >= 0.0f && g <= 1.001f && b >= 0.0f && b <= 1.001f && (r + g + b) > 0.01f) {
                s.has_color = true;
                s.color_r = r;
                s.color_g = g;
                s.color_b = b;
            }
        }
    }
    return s;
}

static std::vector<rivals::NpcUiEntry> hook_npc_ui_list(uint8_t viewer)
{
    std::vector<rivals::NpcUiEntry> out;
    if (!active() || viewer >= kMaxPlayers)
        return out;
    bool has_rival = false;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (i != viewer && g_nations[i].assigned) {
            has_rival = true;
            break;
        }
    }
    std::vector<CivNpcSnapshot> snaps;
    if (is_host()) {
        for (const auto& [pol, arr] : g_npc_rels.scores) {
            void* c = civ_by_pol(pol);
            if (!c || civ::vec_of((uint8_t*)c + civ::kCivCities).empty())
                continue;
            bool rival_turn = false;
            for (int i = 0; i < kMaxPlayers; i++) {
                if (i != viewer && g_nations[i].assigned && arr[i] > kNpcRelMin + 0.05f) {
                    rival_turn = true;
                    break;
                }
            }
            snaps.push_back(snapshot_civ_npc(pol, arr[viewer], rival_turn));
        }
    } else {
        for (const auto& [pol, sc] : g_guest_npc_rels) {
            void* c = civ_by_pol(pol);
            if (!c || civ::vec_of((uint8_t*)c + civ::kCivCities).empty())
                continue;
            snaps.push_back(snapshot_civ_npc(pol, sc, has_rival));
        }
    }
    auto built = build_civ_npc_ui_list(std::move(snaps), has_rival);
    out.reserve(built.size());
    for (auto& it : built)
        out.push_back({it.npc_id, std::move(it.label), it.allied, it.hostile, it.can_turn, it.can_protect, it.can_smooth});
    return out;
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
    a_CitySetSpecialty = H->sym("SporeCoop::cCity_SetSpecialty");
    a_CivRefreshUnlocks = H->sym("SporeCoop::Civilization_RefreshUnlocks");
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
    hooks.on_npc_ui_list = hook_npc_ui_list;
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
        host_sync_guest_designs(now);
        host_deliver_tribe_draft_payouts();
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
    char b[640];
    if (!a.empty() && a[0] == "gift" && a.size() >= 4) {
        uint8_t to = (uint8_t)atoi(a[1].c_str());
        GiftKind k = (a[2] == "spice") ? GiftKind::Spice : GiftKind::Money;
        float amt = (float)atof(a[3].c_str());
        std::string err = request_gift(to, k, amt);
        return err.empty() ? "ok\n" : ("error: " + err + "\n");
    }
    if (!a.empty() && a[0] == "spec" && a.size() >= 2) {
        if (is_guest()) {
            int sp = parse_civ_specialty_arg(a[1].c_str());
            if (!valid_native_specialty(sp))
                return "usage: coop.civ rivals spec <mil|rel|eco>\n";
            std::string err = request_spec_pick(sp);
            return err.empty() ? "ok\n" : ("error: " + err + "\n");
        }
        if (is_host() && a.size() >= 3) {
            uint8_t pid = (uint8_t)atoi(a[1].c_str());
            int sp = parse_civ_specialty_arg(a[2].c_str());
            if (!valid_native_specialty(sp))
                return "usage: coop.civ rivals spec <pid> <mil|rel|eco>\n";
            std::string err = host_apply_spec_pick(pid, sp);
            return err.empty() ? "ok\n" : ("error: " + err + "\n");
        }
        return "usage: coop.civ rivals spec [pid] <mil|rel|eco>\n";
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
        int cap_type = a.size() >= 4 ? atoi(a[3].c_str()) : 0;
        if (cap_type < 0 || cap_type > 2)
            cap_type = 0;
        void* city = civ::host_city((size_t)ci);
        void* ac = civ_of_player(ap);
        if (!city || !ac)
            return "error: invalid city or player\n";
        uint32_t before = civ::pol_of(city);
        civ::h_Capture(city, civ::pol_of(ac), cap_type, 0, 0, 0);
        uint32_t after = civ::pol_of(city);
        // If capture succeeded synchronously or queued via ChangeOwner, ensure immediate ChangeOwner in instant test
        if (after == before && !block_capture(city, civ::pol_of(ac)) && civ::a_ChangeOwner) {
            if (skg::readable(city, 0x29C + 4))
                F<int>(city, 0x29C) = cap_type;
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
    if (!a.empty() && a[0] == "setspec" && a.size() >= 3) {
        int spec = atoi(a[2].c_str());
        if (!valid_native_specialty(spec))
            return "error: specialty must be 0 (mil), 1 (rel), or 2 (eco)\n";
        if (a[1] == "all_npc") {
            void* orig_pc = orig_player_civ();
            uint32_t orig_pol = orig_pc ? civ::pol_of(orig_pc) : 0;
            int n = 0;
            for (void* city : civ::all_cities()) {
                if (!city)
                    continue;
                uint32_t cpol = civ::pol_of(city);
                if (orig_pol && cpol == orig_pol)
                    continue;
                void* cc = skg::readable(city, civ::kCityCiv + 4) ? F<void*>(city, civ::kCityCiv) : nullptr;
                apply_city_specialty(city, cc, spec);
                n++;
            }
            snprintf(b, sizeof b, "setspec all_npc count=%d spec=%d\n", n, spec);
            return b;
        }
        int ci = atoi(a[1].c_str());
        auto cs = civ::all_cities();
        void* city = is_host() ? civ::host_city((size_t)ci)
                               : (ci >= 0 && ci < (int)civ::g_host_cities.size() ? civ::g_host_cities[ci] : nullptr);
        if (!city && ci >= 0 && ci < (int)cs.size())
            city = cs[ci];
        if (!city)
            return "error: invalid city\n";
        void* cc = skg::readable(city, civ::kCityCiv + 4) ? F<void*>(city, civ::kCityCiv) : nullptr;
        apply_city_specialty(city, cc, spec);
        snprintf(b, sizeof b, "setspec city=%d spec=%d cap29c=%d\n", ci, F<int>(city, civ::kCitySpecialty),
                 skg::readable(city, 0x29C + 4) ? F<int>(city, 0x29C) : -1);
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
             "civ_rivals: fix=%d active=%d assigned=%d saved=%u swap=%d player_pol=0x%08X api_pol=0x%08X player_money=%.0f "
             "takeovers=%u reclaimed=%u ai_blocked=%u swap_hits=%u cap_blocked=%u conv_blocked=%u sw_blocked=%u sw_borrow=%u "
             "pvp_allowed=%u pvp_blocked=%u war_captures=%u gifts=%u/%u spice=%u diplo_gifts=%u "
             "inf_buy=%u inf_turn=%u inf_prot=%u inf_smooth=%u inf_trade=%u inf_diplo=%u rel_readbacks=%u "
             "assign_tx=%u assign_rx=%u npcrel_tx=%u npcrel_rx=%u "
             "race_ended=%d draft_started=%d draft_applied=%u draft_payouts=%u transition_ready=%d "
             "spec_converted=%u spec_picked=%u sw_spent=%u sw_refused_funds=%u designs_synced=%u\n",
             (int)g_fix_rivals_civ, (int)active(), assigned_n, (unsigned)g_saved_nations.by_key.size(), (int)g_swap_applied,
             (unsigned)cur_pol, (unsigned)api_pol, cur_money, g_st.takeovers, g_st.reclaimed, g_st.ai_blocked,
             g_st.swap_hits, g_st.cap_blocked, g_st.conv_blocked, g_st.sw_blocked, g_st.sw_guest_borrows,
             g_st.pvp_allowed, g_st.pvp_blocked, g_st.war_captures, g_st.gifts_done, g_st.gifts_rejected,
             g_st.spice_gifts, g_st.diplo_gifts, g_st.inf_bought, g_st.inf_turned, g_st.inf_protected, g_st.inf_smoothed,
             g_st.inf_trade, g_st.inf_diplomacy, g_st.rel_readbacks, g_st.assign_tx, g_st.assign_rx, g_st.npcrel_tx,
             g_st.npcrel_rx, (int)g_race_ended, (int)g_draft_started, g_st.draft_applied, g_st.draft_payouts,
             (int)g_transition_ready, g_st.spec_converted, g_st.spec_picked, g_st.sw_spent, g_st.sw_refused_funds,
             g_st.designs_synced);
    std::string out = b;

    for (int i = 0; i < kMaxPlayers; i++) {
        if (!g_nations[i].assigned)
            continue;
        void* c = civ_by_pol(g_nations[i].pol_id);
        auto cs = c ? civ::vec_of((uint8_t*)c + civ::kCivCities) : std::vector<void*>{};
        int nc = (int)cs.size();
        int nv = c ? (int)civ::vec_of((uint8_t*)c + 0xB0).size() : 0;
        float mw = (c && skg::readable(c, kCivWealth + 4)) ? F<float>(c, kCivWealth) : g_nations[i].wealth;
        int cap_sp = (!cs.empty() && skg::readable(cs[0], civ::kCitySpecialty + 4)) ? F<int>(cs[0], civ::kCitySpecialty)
                                                                                   : (int)g_nations[i].cap_specialty;
        snprintf(b, sizeof b, "  nation p=%d pol=0x%08X cities=%d vehs=%d wealth=%.0f cap=(%.1f %.1f %.1f) spec=%d\n", i,
                 (unsigned)g_nations[i].pol_id, nc, nv, mw, g_nations[i].cap_pos.x, g_nations[i].cap_pos.y,
                 g_nations[i].cap_pos.z, cap_sp);
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
