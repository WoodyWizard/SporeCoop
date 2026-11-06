#include "civ.h"
#include "census.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "game.h"
#include "shared.h"
#include "stage_epoch.h"
#include "modes.h"
#include "net/ids.h"
#include "net/interp.h"
#include "net/validate.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "creations.h"
#include "founders.h"
#include "world.h"

using skg::thiscall;
using skg::vcall;

namespace civ {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_ok;
static founders::Roster g_founder_nations;
static bool g_auto_founders = true;
static bool g_founder_spawned;
static bool g_founder_control = true;
static bool g_offline_control, g_offline_selection_ready, g_command_refresh;
static std::string g_offline_planet;
static bool g_team_progress = true;
COOP_FIX("civ.team_progress", "BUG-9056", g_team_progress,
         "civ: founder captures and native unlocks advance one team campaign");
COOP_FIX("civ.founder_control", "BUG-9054", g_founder_control,
         "civ: separate founder nations use player control and the shared treasury");

// ---- layout (docs/re/civ.md) ----
// game addresses that are not functions (vtable, return addresses): resolved by name in init()
static uintptr_t a_VehicleVt, a_PlannerAddBuildingRet, a_GrowthRetBudget, a_GrowthRetNew;
constexpr uintptr_t kVSpatial = 0x34, kVPos = 0x38, kVRot = 0x44, kVSelected = 0xA0, kVHp = 0x540, kVLoco = 0xB1C,
                    kVPurpose = 0xB20, kVDead = 0xBCD, kPol = 0x30,
                    kVOrders = 0xB68;
constexpr uintptr_t kCityPos = 0x124, kCitySpecialty = 0x540, kCitySlots = 0x43C, kCityCiv = 0x590,
                    kCityCapture = 0x748, // + 4: capturing political id
                    kCityBuildings = 0x340, kBuildingState = 0x120 + 0x34; // combatant state, 2 = destroyed
constexpr uint8_t kShared = 0xFD; // a city everybody may use (the capital while cities < players)
constexpr uintptr_t kCivCities = 0x9C;
constexpr uintptr_t kNationSpecies = 0x268, kCitySpecies = 0x658;
constexpr uint32_t kMsgActivated = 0x287259F6, kBtnLand = 0x62D6902, kBtnWater = 0x62D6908, kBtnAir = 0x62D690F,
                   kBtnPlanner = 0x62D5618, kBuildingType = 0x0E9CB8BA;

static uintptr_t a_GetAllCivs, a_GetAllCities, a_GetAllVehicles, a_CivByPol, a_PlayerCiv, a_GetMoney, a_SpendMoney,
    a_CreateVehicle, a_SlotsLeft, a_VehicleCost, a_DesignKey, a_DesignId, a_SpawnVehicle, a_AddBuilding,
    a_SetBuildingModel, a_SnapToSlot, a_ChangeOwner, a_Capture, a_SetOrder, a_SetMoveOrder, a_OrderStamp, a_Die,
    a_SetSelected, a_PushEvent, a_BTTick, a_CreateInstance, a_UIMessage, a_CreateBuilding,
    a_NodeClaim, a_NodeUnclaim, a_SetCapture, a_SpawnNextCity, a_ExpansionGet, a_CinematicMgr, a_DestroyBuilding,
    a_CombatantDamage, a_AddMoney;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
// BUG-0151: while the host is away (main menu) the guest's vehicles run their own AI and the guest's
// orders go to its own game (the host sends no states and drops orders outside Civ).
// `coop.civ fix away 0`: the old frozen vehicles (tests).
static bool g_fix_away = true;
COOP_FIX("civ.away", "BUG-0151", g_fix_away, "civ: while the host is away the guest's vehicles run their own AI");
static uint32_t g_away_orders;
static bool local_play() { return g_fix_away && is_guest() && C.host_away && C.host_away(); }
bool active() { return g_ok && C.sess && C.sess->connected() && game::mode_id() == modes::kCiv; }

static void* nm() { return game::noun_manager(); }
static std::vector<void*> vec_of(void* v)
{
    std::vector<void*> r;
    if (!v)
        return r;
    void** b = F<void**>(v, 0);
    void** e = F<void**>(v, 4);
    for (void** it = b; it && it < e && r.size() < 512; ++it)
        if (*it)
            r.push_back(*it);
    return r;
}
static std::vector<void*> all_cities() { return vec_of(thiscall<void*>(a_GetAllCities, nm())); }
static std::vector<void*> all_vehicles()
{
    std::vector<void*> r;
    for (void* v : vec_of(thiscall<void*>(a_GetAllVehicles, nm())))
        if (game::object_alive(v) && !F<uint8_t>(v, kVDead))
            r.push_back(v);
    return r;
}
// The vehicles the game has right now (its own list, nothing filtered), once per frame. A vehicle
// the game destroyed and freed was still driven by guest_tick through our map (AV in civ::tick on a
// freed object, then the guest froze: coop_chaos civ_npc, 2026-10-23); object_alive() reads the
// freed memory and cannot tell.
static std::unordered_set<void*> g_exist;
static uint64_t g_exist_frame = ~0ull;
static bool exists(void* v)
{
    uint64_t f = H->frame_index();
    if (f != g_exist_frame) {
        g_exist.clear();
        for (void* x : vec_of(thiscall<void*>(a_GetAllVehicles, nm())))
            g_exist.insert(x);
        g_exist_frame = f;
    }
    return v && g_exist.count(v);
}
static void* player_civ() { return thiscall<void*>(a_PlayerCiv, nm()); }
static uint32_t pol_of(void* o) { return F<uint32_t>(o, kPol); }
static bool founder_country(void* country)
{
    return g_founder_control && active() && country && g_founder_nations.contains_faction(pol_of(country));
}
static uint8_t founder_owner(uint32_t faction)
{
    if (!g_founder_control || !g_founder_nations.contains_faction(faction)) return 0xFF;
    if (faction == g_founder_nations.home().faction) return 0;
    for (const auto& player : C.sess->players()) if (player.id) return player.id;
    return g_offline_control && g_offline_selection_ready ? 0 : 0xFE;
}
static std::vector<void*> team_cities(void* first)
{
    std::vector<void*> cities;
    if (!g_team_progress || !founder_country(first) || !g_founder_nations.guest().faction) return cities;
    // Keep this country's capital first for native specialty fallback.
    cities = vec_of((uint8_t*)first + kCivCities);
    for (void* city : all_cities())
        if (F<void*>(city, kCityCiv) != first && g_founder_nations.contains_faction(pol_of(city)))
            cities.push_back(city);
    return cities;
}
static net::Vec3 city_pos(void* c)
{
    auto& p = F<float[3]>(c, kCityPos);
    return {p[0], p[1], p[2]};
}
static net::Vec3 veh_pos(void* v)
{
    auto& p = F<float[3]>(v, kVPos);
    return {p[0], p[1], p[2]};
}
static net::Quat veh_rot(void* v)
{
    auto& q = F<float[4]>(v, kVRot);
    return {q[0], q[1], q[2], q[3]};
}

// ---------------- ownership ----------------
static std::unordered_map<void*, uint8_t> g_city_owner; // both: player city -> player id
static std::vector<void*> g_host_cities;                // host: snapshot order; guest: host index -> local
static std::unordered_set<void*> g_city_gifted;         // host: cities given away keep their owner
static void* g_ui_city;                                 // the city whose menu was opened last
static uint32_t g_gifts;

// ---------------- host ----------------
struct HV {
    uint16_t id;
    uint8_t owner;
    uint32_t pol; // the vehicle's political id when it was listed (BUG-0059: a reused address)
    bool gifted = false;
};
// test switches (`coop.civ fix boost|world 0|1`): 0 brings back the code before the fix
static bool g_fix_boost = true; // BUG-0040: boosts only on listed vehicles, cleared with the session
static bool g_fix_world = true; // BUG-0059: every pointer table cleared when the world changes
COOP_FIX_UNSAFE("civ.boost", "BUG-0040", g_fix_boost, "civ: boosts only on listed vehicles, cleared with the session");
COOP_FIX_UNSAFE("civ.world", "BUG-0059", g_fix_world, "civ: every pointer table cleared when the world changes");
// BUG-0186 (b): the guest moves a mirror at most 0.15 s ahead of its last state (was 0.4 s)
static bool g_extrap_fix = true;
COOP_FIX("civ.extrap", "BUG-0186", g_extrap_fix,
         "civ: a mirrored vehicle is moved on at most 0.15 s past its last state (a host hitch no longer throws it ahead)");
constexpr double kVehAhead = 0.15;
static bool g_vel_cap_fix = true;
COOP_FIX("civ.vel_cap", "BUG-0186", g_vel_cap_fix,
         "civ: a vehicle step faster than twice its top speed is taken as a jump, not a speed to extrapolate");
static uint32_t g_world_resets;
static std::unordered_map<void*, HV> g_hv;
static std::unordered_map<uint16_t, void*> g_hv_by_id;
static uint16_t g_next_id = 1;
static double g_last_send;
static uint16_t g_cycle;
static uint32_t g_cmds_done, g_cmds_rejected, g_builds_sent, g_unselected, g_buys_refused;
static int g_build_from = -1; // guest whose build is being executed (not echoed back to it)
static int g_pending_owner = -1; // owner of the vehicle the host is about to spawn for a guest
static std::vector<uint8_t> g_split_for;
// BUG-0031: vehicles of a player who left get a present owner; a capture never names an absent
// player. `coop.civ fix orphans 0`: the old behaviour (tests).
static bool g_fix_orphans = true;
COOP_FIX("civ.orphans", "BUG-0031", g_fix_orphans, "civ: vehicles of a player who left get a present owner");
static uint32_t g_orphans_given;

// ---------------- guest ----------------
struct GV {
    void* v = nullptr;
    uint8_t owner = 0xFE, purpose = 0, loco = 0;
    uint32_t pol = 0;
    net::Vec3 pos;
    net::Quat rot;
    float hp = 0;
    double seen = 0;
    net::VelocityEstimate est; // vehicles snapshots carry no velocity
    bool listed = false;       // seen in the game's vehicle list (a new one joins it a little later)
    // BUG-0186 diagnostics (coop.civ on the guest): how far the vehicle was from where guest_tick put
    // it one frame before (the game moved it: collision, its own locomotion), the largest of that in
    // the last 5 s, and how far it was from where it should be
    net::Vec3 put{};
    bool has_put = false;
    float pushed = 0, pushed_max = 0, err = 0;
    double pushed_at = 0;
};
static std::unordered_map<uint16_t, GV> g_gv;
static std::unordered_map<void*, uint16_t> g_gv_of;

static std::unordered_set<uint16_t> g_cycle_ids;
static std::unordered_set<uint8_t> g_cycle_chunks; // chunks of the current cycle received
static uint32_t g_incomplete;                      // cycles whose cleanup was skipped (a chunk missing)
static uint32_t g_last_chunks, g_max_chunk;        // host: chunks per cycle, largest chunk (bytes)
static size_t g_chunk_budget = net::kMaxPacket - 48; // host: bytes per chunk (coop.civ chunk <bytes>, tests)
static uint16_t g_rx_cycle = 0xFFFF;
static bool g_applying; // executing a host message: the hooks let the game act

// BUG-0186: Vehicle_KeepOnValidGround (0xCA7430, thiscall(v), from the vehicle's movement updates)
// checks the planet cell under a vehicle against its locomotion (land / sea / air flags) and, when the
// cell is not valid, puts it on the nearest valid spot (SetPosition). The host's NPC vehicle stood
// on a spice node the guest's planet flags as invalid ground: the guest's game threw the mirror 13 m
// away every frame and guest_tick put it back (13-15 m off in every play_civ seed 9 / 11 run).
// A mirror stands where the host's vehicle stands: the check is skipped for mirrors on a guest.
using KeepGround_t = void(SK_THISCALL*)(void*);
static KeepGround_t o_KeepGround;
static bool g_mirror_place_fix = true;
COOP_FIX("civ.mirror_place", "BUG-0186", g_mirror_place_fix,
         "civ: a guest's game does not move a mirrored vehicle off ground it finds invalid (the host's vehicle stands there)");
static uint32_t g_mirror_place_skipped;
static void SK_THISCALL h_KeepGround(void* v)
{
    if (g_mirror_place_fix && !g_applying && is_guest() && game::mode_id() == modes::kCiv && g_gv_of.count(v)) {
        g_mirror_place_skipped++;
        return;
    }
    o_KeepGround(v);
}
static uint32_t g_removed_local; // the guest's own vehicles removed (civ_ai_spawn: not the host's departed ones)
static uint32_t g_created, g_removed, g_cmds_sent, g_builds_in, g_captures_in, g_orders_dropped, g_growth_blocked, g_nodes_in;

static uint8_t owner_of_vehicle(void* v)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        auto it = g_hv.find(v);
        return it == g_hv.end() ? 0xFF : it->second.owner;
    }
    auto it = g_gv_of.find(v);
    return it == g_gv_of.end() ? 0xFF : g_gv[it->second].owner;
}

static uint8_t owner_of_city(void* c)
{
    auto it = g_city_owner.find(c);
    return it == g_city_owner.end() ? 0xFF : it->second;
}

using IsOwned_t = bool(SK_THISCALL*)(void*);
static IsOwned_t o_CityOwned, o_VehicleOwned;
static bool SK_THISCALL h_CityOwned(void* spatial)
{
    if (g_founder_control && active()) {
        void* city = (uint8_t*)spatial - 0x120;
        auto owner = founder_owner(pol_of(city));
        if (owner != 0xFF) return owner == C.sess->my_id();
    }
    return o_CityOwned(spatial);
}
static bool SK_THISCALL h_VehicleOwned(void* spatial)
{
    if (g_founder_control && active()) {
        void* vehicle = (uint8_t*)spatial - kVSpatial;
        auto owner = founder_owner(pol_of(vehicle));
        if (owner != 0xFF) {
            if (owner == 0xFE) return false;
            auto assigned = owner_of_vehicle(vehicle);
            return (assigned < 0xFE ? assigned : owner) == C.sess->my_id();
        }
    }
    return o_VehicleOwned(spatial);
}

using PlayerCiv_t = void*(SK_THISCALL*)(void*);
using CivAI_t = void(SK_THISCALL*)(void*);
static PlayerCiv_t o_PlayerCiv;
static CivAI_t o_CivAI;
static uintptr_t a_CivAIPlayerRet;
static thread_local void* g_ai_founder;
static thread_local void* g_capture_founder;
static uintptr_t a_ChangeOwnerPlayerRet;
using TerrainCursor_t = void*(SK_CDECL*)();
static TerrainCursor_t o_TerrainCursor;
static void* g_bound_cursor;
static void* g_bound_candidates;
// Box selection, applying selection and retrieving it all consume the same
// native candidate list. Borrow a combined list only during those calls.
// Nested calls see the outer view. No vehicle pointer survives the call.
struct OfflineCandidates {
    void* cursor;
    void* previous = nullptr;
    std::vector<void*> items;
    size_t rollback = 0;
    struct { void** begin; void** end; void** capacity; } view{};
    explicit OfflineCandidates(void* c) : cursor(c) {
        if (!g_offline_control || !g_offline_selection_ready || !g_founder_control ||
            !active() || !is_host() || C.sess->players().size() != 1 || !g_founder_nations.guest().faction) return;
        void* home = player_civ();
        if (!home || F<void*>(cursor, 0x164) != (uint8_t*)home + 0xD4) return;
        void* partner = thiscall<void*>(a_CivByPol, nm(), g_founder_nations.guest().faction);
        if (!partner || partner == home) return;
        auto extra = vec_of((uint8_t*)partner + 0xD4);
        if (extra.empty()) return;
        previous = (uint8_t*)home + 0xD4;
        items = vec_of(previous);
        items.insert(items.end(), extra.begin(), extra.end());
        view = {items.data(), items.data() + items.size(), items.data() + items.size()};
        rollback = game::swap_open(game::swap_undo_u32, &F<void*>(cursor, 0x164), (uintptr_t)previous,
                                   "Civ offline selection candidates");
        F<void*>(cursor, 0x164) = &view;
    }
    ~OfflineCandidates() {
        if (!previous) return;
        if (F<void*>(cursor, 0x164) == &view) F<void*>(cursor, 0x164) = previous;
        game::swap_close(rollback);
    }
};
using SelectedFn = uintptr_t(SK_THISCALL*)(void*, uintptr_t);
using SelectFn = uintptr_t(SK_THISCALL*)(void*, uintptr_t, uintptr_t);
static SelectedFn o_GetSelected;
static SelectFn o_BoxSelect, o_ApplySelection;
static uintptr_t SK_THISCALL h_GetSelected(void* cursor, uintptr_t out) {
    OfflineCandidates list(cursor); return o_GetSelected(cursor, out);
}
static uintptr_t SK_THISCALL h_BoxSelect(void* cursor, uintptr_t first, uintptr_t second) {
    OfflineCandidates list(cursor); return o_BoxSelect(cursor, first, second);
}
static uintptr_t SK_THISCALL h_ApplySelection(void* cursor, uintptr_t input, uintptr_t flags) {
    OfflineCandidates list(cursor); return o_ApplySelection(cursor, input, flags);
}
static void* SK_CDECL h_TerrainCursor()
{
    void* cursor = o_TerrainCursor();
    if (!cursor) return cursor;
    void* country = nullptr;
    if (g_founder_control && active() && is_guest() && g_founder_nations.guest().faction)
        country = thiscall<void*>(a_CivByPol, nm(), g_founder_nations.guest().faction);
    if (country) {
        // Borrow the native country's live spatial vector. Native insert/remove
        // operations maintain its lifetime; no vehicle pointers are cached here.
        g_bound_cursor = cursor;
        g_bound_candidates = (uint8_t*)country + 0xD4;
        F<void*>(cursor, 0x164) = g_bound_candidates;
    } else if (cursor == g_bound_cursor) {
        // A switch-off/disconnect must return selection to the native country.
        // Never overwrite a list another stage has already installed.
        if (F<void*>(cursor, 0x164) == g_bound_candidates) {
            void* home = g_ok && game::mode_id() == modes::kCiv && nm() ? player_civ() : nullptr;
            F<void*>(cursor, 0x164) = home ? (uint8_t*)home + 0xD4 : nullptr;
        }
        g_bound_cursor = g_bound_candidates = nullptr;
    }
    return cursor;
}
static void* SK_THISCALL h_PlayerCiv(void* manager)
{
    // Only the AI's own player/NPC branch. All other native lookups, save
    // ownership and the shared-money adapter retain the real player country.
    // A separate scoped site lets either founder emit native capture history.
    if (g_ai_founder && (uintptr_t)__builtin_return_address(0) == a_CivAIPlayerRet)
        return g_ai_founder;
    if (g_capture_founder && (uintptr_t)__builtin_return_address(0) == a_ChangeOwnerPlayerRet)
        return g_capture_founder;
    return o_PlayerCiv(manager);
}
static void SK_THISCALL h_CivAI(void* country)
{
    // Native appearance can run an AI tick before the outside-frame binding.
    // A reserved new capital must not buy units or grow as an NPC in that gap.
    if (g_founder_control && g_auto_founders && active() && is_host() && g_founder_spawned &&
        !g_founder_nations.guest().faction && country != player_civ()) return;
    void* previous = g_ai_founder;
    if (founder_country(country)) g_ai_founder = country;
    o_CivAI(country);
    g_ai_founder = previous;
}

using GetMoney_t = float(SK_THISCALL*)(void*);
using SpendMoney_t = void(SK_THISCALL*)(void*, float);
static GetMoney_t o_GetMoney;
static SpendMoney_t o_SpendMoney;
static float SK_THISCALL h_GetMoney(void* country)
{
    return o_GetMoney(founder_country(country) ? player_civ() : country);
}
static void SK_THISCALL h_SpendMoney(void* country, float amount)
{
    o_SpendMoney(founder_country(country) ? player_civ() : country, amount);
}

using Unlocks_t = void(SK_THISCALL*)(void*);
using Tech_t = void(SK_THISCALL*)(void*, int);
using CityCount_t = int(SK_THISCALL*)(void*);
static Unlocks_t o_Unlocks;
static Tech_t o_Tech;
static CityCount_t o_CityCount;
static uintptr_t a_FinalCityCount, a_UnlockCountRet, a_TechCountRet;
using ProgressFn = float(SK_THISCALL*)(void*);
static ProgressFn o_HudProgress;
static uintptr_t a_HudProgressRet;
static unsigned g_hud_samples;
static float g_hud_fraction, g_solo_fraction;

static float SK_THISCALL h_HudProgress(void* strategy)
{
    const float original = o_HudProgress(strategy);
    if ((uintptr_t)__builtin_return_address(0) != a_HudProgressRet || !active() ||
        !g_founder_nations.guest().faction || !nm()) return original;
    float value = original;
    void* home = player_civ();
    if (g_team_progress && g_founder_control && home &&
        pol_of(home) == g_founder_nations.home().faction) {
        void* partner = thiscall<void*>(a_CivByPol, nm(), g_founder_nations.guest().faction);
        const int total = skg::cdecl_call<int>(a_FinalCityCount);
        if (partner && partner != home && total > 0)
            value = std::min(1.f, original + (float)o_CityCount(partner) / total);
    }
    ++g_hud_samples;
    g_solo_fraction = original;
    g_hud_fraction = value;
    return value;
}
static double g_progress_poll, g_campaign_share_at;
static bool g_progress_queued;

static void SK_THISCALL h_Unlocks(void* country)
{
    auto cities = team_cities(country);
    if (cities.empty()) { o_Unlocks(country); return; }
    // BF2510 only reads the city vector (length and first city's specialty).
    // Borrow a complete read-only view for this synchronous calculation, never
    // forge a larger end pointer or insert cities into their partner's nation.
    uintptr_t* view = &F<uintptr_t>(country, kCivCities);
    std::array<uintptr_t, 3> previous{view[0], view[1], view[2]};
    size_t mark = game::swap_open(game::swap_undo_u32, view, view[0], "Civ unlock city view");
    game::swap_open(game::swap_undo_u32, view + 1, view[1], "Civ unlock city end");
    game::swap_open(game::swap_undo_u32, view + 2, view[2], "Civ unlock city capacity");
    view[0] = (uintptr_t)cities.data();
    view[1] = view[2] = (uintptr_t)(cities.data() + cities.size());
    o_Unlocks(country);
    std::copy(previous.begin(), previous.end(), view);
    game::swap_close(mark, 3);
}
static int team_tech(void* country, int requested)
{
    auto cities = team_cities(country);
    if (cities.empty() || !a_FinalCityCount) return requested;
    int final_count = skg::cdecl_call<int>(a_FinalCityCount);
    int earned = final_count > 0 && cities.size() >= (size_t)final_count ? 4 : cities.size() >= 4 ? 3 : 2;
    return std::max(requested, earned);
}
static void SK_THISCALL h_Tech(void* country, int level)
{
    o_Tech(country, team_tech(country, level));
}
static int SK_THISCALL h_CityCount(void* country)
{
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    // These two UI callers compare a threshold; neither indexes a city vector.
    if (ret == a_UnlockCountRet || ret == a_TechCountRet) {
        auto cities = team_cities(country);
        if (!cities.empty()) return (int)cities.size();
    }
    return o_CityCount(country);
}

static int host_index_of_city(void* c)
{
    for (size_t i = 0; i < g_host_cities.size(); i++)
        if (g_host_cities[i] == c)
            return (int)i;
    return -1;
}

static void* nearest_player_city(const net::Vec3& p, uint8_t* owner = nullptr)
{
    void* best = nullptr;
    float bd = 1e18f;
    for (auto& [c, o] : g_city_owner) {
        float d = net::dist(city_pos(c), p);
        if (d < bd) {
            bd = d;
            best = c;
            if (owner)
                *owner = o;
        }
    }
    return best;
}

// ---------------- hooks ----------------
// behaviour tree tick shared by every agent: skipped for vehicle mirrors on guests
using BT_t = int(SK_CDECL*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static BT_t o_BT;
static int SK_CDECL h_BT(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t agent, uint32_t a5, uint32_t a6,
                         uint32_t a7, uint32_t a8)
{
    if (agent && is_guest() && game::mode_id() == modes::kCiv && !local_play() && !IsBadReadPtr((void*)agent, 4) &&
        *(uintptr_t*)agent == a_VehicleVt)
        return 0;
    return o_BT(a0, a1, a2, a3, agent, a5, a6, a7, a8);
}

using SetSelected_t = int(SK_THISCALL*)(void*, int);
static SetSelected_t o_SetSelected;
static int SK_THISCALL h_SetSelected(void* spatial, int sel)
{
    if ((sel & 0xFF) && active()) {
        uint8_t o = owner_of_vehicle((uint8_t*)spatial - kVSpatial);
        if (o != 0xFF && o != C.sess->my_id())
            sel = 0;
    }
    return o_SetSelected(spatial, sel);
}

struct Target {
    uint8_t kind = 0; // 0 none, 1 vehicle id, 2 host city index, 3 object by noun + position
    uint16_t id = 0;
    uint32_t noun = 0;
    net::Vec3 pos;
};

static Target encode_target(void* t)
{
    Target r;
    if (!t)
        return r;
    auto it = g_gv_of.find(t);
    if (it != g_gv_of.end()) {
        r.kind = 1;
        r.id = it->second;
        return r;
    }
    int ci = host_index_of_city(t);
    if (ci >= 0) {
        r.kind = 2;
        r.id = (uint16_t)ci;
        return r;
    }
    r.kind = 3;
    r.noun = game::noun_id(t);
    game::object_position(t, r.pos);
    return r;
}

static void write_target(net::Writer& w, const Target& t)
{
    w.u8(t.kind); w.u16(t.id); w.u32(t.noun);
    w.f32(t.pos.x); w.f32(t.pos.y); w.f32(t.pos.z);
}

static Target read_target(net::Reader& r)
{
    Target t;
    t.kind = r.u8(); t.id = r.u16(); t.noun = r.u32();
    t.pos.x = r.f32(); t.pos.y = r.f32(); t.pos.z = r.f32();
    return t;
}

enum : uint8_t { CMD_ORDER = 0, CMD_MOVE = 1, CMD_BUY = 2, CMD_BUILD = 3, CMD_GIVE = 4 };

static void send_cmd(net::Writer& w)
{
    C.sess->send_reliable_to(0, net::MSG_CIV_CMD, w);
    g_cmds_sent++;
}

using SetOrder_t = int(SK_THISCALL*)(void*, void*, int, int);
static SetOrder_t o_SetOrder;
static int SK_THISCALL h_SetOrder(void* v, void* target, int type, int flag)
{
    if (local_play() && !g_applying)
        g_away_orders++;
    else if (!g_applying && is_guest() && game::mode_id() == modes::kCiv) {
        auto it = g_gv_of.find(v);
        if (it != g_gv_of.end()) {
            // a mirror of another player's or nation's vehicle: the guest's own civ AI orders the
            // other nations' vehicles every few frames (hundreds of rejected commands a minute)
            if (g_gv[it->second].owner != C.sess->my_id()) {
                g_orders_dropped++;
                return 0;
            }
            net::Writer w;
            w.u8(CMD_ORDER);
            w.u16(it->second);
            w.u32((uint32_t)type);
            w.u32((uint32_t)flag);
            write_target(w, encode_target(target));
            send_cmd(w);
            return 0;
        }
    }
    return o_SetOrder(v, target, type, flag);
}

using SetMoveOrder_t = int(SK_THISCALL*)(void*, float, float, float, int);
static SetMoveOrder_t o_SetMoveOrder;
static int SK_THISCALL h_SetMoveOrder(void* v, float x, float y, float z, int flag)
{
    if (local_play() && !g_applying)
        g_away_orders++;
    else if (!g_applying && is_guest() && game::mode_id() == modes::kCiv) {
        auto it = g_gv_of.find(v);
        if (it != g_gv_of.end()) {
            if (g_gv[it->second].owner != C.sess->my_id()) {
                g_orders_dropped++;
                return 0;
            }
            net::Writer w;
            w.u8(CMD_MOVE);
            w.u16(it->second);
            w.f32(x); w.f32(y); w.f32(z);
            w.u32((uint32_t)flag);
            send_cmd(w);
            return 0;
        }
    }
    return o_SetMoveOrder(v, x, y, z, flag);
}

static void request_buy(void* city, int loco)
{
    net::Writer w;
    w.u8(CMD_BUY);
    w.u8((uint8_t)host_index_of_city(city));
    w.u8((uint8_t)loco);
    send_cmd(w);
}

// RolloverCivCity window (city menu): vehicle purchase and city planner buttons
using UIMessage_t = int(SK_THISCALL*)(void*, void*, const uint8_t*);
static UIMessage_t o_UIMessage;
static int SK_THISCALL h_UIMessage(void* self, void* win, const uint8_t* msg)
{
    if (active() && msg && F<uint32_t>((void*)msg, 8) == kMsgActivated) {
        uint32_t id = F<uint32_t>((void*)msg, 0xC);
        int loco = id == kBtnLand ? 0 : id == kBtnWater ? 1 : id == kBtnAir ? 2 : -1;
        if (void* c0 = F<void*>(self, 0xA8))
            g_ui_city = c0;
        if (loco >= 0 || id == kBtnPlanner) {
            void* city = F<void*>(self, 0xA8);
            uint8_t owner = owner_of_city(city);
            if (owner != 0xFF && owner != kShared && owner != C.sess->my_id()) {
                if (C.feed)
                    C.feed("this city belongs to " + C.sess->name_of(owner));
                return 1;
            }
            if (loco >= 0 && is_guest() && host_index_of_city(city) >= 0) {
                request_buy(city, loco); // the host builds it and pays from the shared treasury
                return 1;
            }
        }
    }
    return o_UIMessage(self, win, msg);
}

static uint32_t type_for_noun(uint32_t noun)
{
    switch (noun) {
    case 0x18EB106: return 0xFF10521; // house
    case 0x18EA2CC: return 0xECADE42; // factory
    case 0x1A56ABA: return 0x1A55E4D; // entertainment
    }
    return 0;
}

// BUG-0469: the design id cCity::SetBuildingModel (0xBDB940, switch at 0xBDB944) asks the city's
// civilization for, per building type: house 0xFF10521 -> 0x4E3F7777, entertainment 0x1A55E4D ->
// 0x72C49181, factory 0xECADE42 -> 0x47C10953 (every other type: 0x4E3F7777); 0: not a design we know
static uint32_t building_design_id(uint32_t type)
{
    switch (type) {
    case 0xFF10521: return 0x4E3F7777; // house
    case 0xECADE42: return 0x47C10953; // factory
    case 0x1A55E4D: return 0x72C49181; // entertainment
    }
    return 0;
}

// BUG-0469: a building's model is the design of its city's civilization, and SetBuildingModel reads
// that key itself (a substituted key cannot be passed to it): on a guest, a design whose model names
// parts or paints this game does not have is not built here (the host's building stays missing on that
// guest: logged once per design, see docs/re/civ.md#civ-content-all-kinds). The host builds from its
// own designs, which are its own game's content (the game has them, it bakes them itself).
static unsigned g_build_skipped;
static std::unordered_set<uint32_t> g_build_skipped_logged;
static bool building_model_ok(void* city, uint32_t type, uint32_t noun)
{
    if (!is_guest())
        return true;
    void* civ = city ? F<void*>(city, kCityCiv) : nullptr;
    uint32_t did = building_design_id(type);
    if (!civ || !did)
        return true;
    uint32_t* k = thiscall<uint32_t*>(a_DesignKey, civ, (int)did);
    if (!k || !k[0])
        return true; // the game generates the design itself (FUN_00bf98c0)
    creations::Model m = creations::ensure_model({k[0], k[1], k[2]}, creations::kMkBuilding, false);
    if (m.key.instance)
        return true;
    g_build_skipped++;
    if (g_build_skipped_logged.insert(k[0]).second)
        H->log(M, SK_INFO, "civ: the model of the building design %08X (noun %08X) cannot be built here: the building "
               "is left out on this guest (BUG-0469)", k[0], noun);
    return false;
}

static void broadcast_building(void* city, uint32_t noun, const net::Vec3& pos)
{
    int ci = host_index_of_city(city);
    if (ci < 0) { // a city founded this frame (its first buildings come at once): the list is 0.1 s old
        g_host_cities = all_cities();
        ci = host_index_of_city(city);
    }
    if (ci < 0 || !type_for_noun(noun))
        return;
    net::Writer w;
    w.u8((uint8_t)ci);
    w.u32(noun);
    w.f32(pos.x); w.f32(pos.y); w.f32(pos.z);
    C.sess->broadcast_reliable(net::MSG_CIV_BUILD, w, g_build_from);
    g_builds_sent++;
}

using AddBuilding_t = int(SK_THISCALL*)(void*, void*, int);
static AddBuilding_t o_AddBuilding;
static int SK_THISCALL h_AddBuilding(void* city, void* b, int x)
{
    void* ret = __builtin_return_address(0);
    int r = o_AddBuilding(city, b, x);
    if (!active() || g_applying || !b)
        return r;
    net::Vec3 pos;
    game::object_position(b, pos);
    uint32_t noun = game::noun_id(b);
    if (is_host()) {
        broadcast_building(city, noun, pos); // every building the host gets appears on the guests
    } else if (is_guest() && (uintptr_t)ret == a_PlannerAddBuildingRet) {
        // the guest placed a building in its city planner: the host builds the same one
        int ci = host_index_of_city(city);
        if (ci >= 0) {
            net::Writer w;
            w.u8(CMD_BUILD);
            w.u8((uint8_t)ci);
            w.u32(noun);
            w.f32(pos.x); w.f32(pos.y); w.f32(pos.z);
            send_cmd(w);
        }
    }
    return r;
}

// city growth by the civ AI (docs/re/civ.md 1): the guest runs the other nations' AI too, so
// without this its cities grew on their own and got the host's buildings on top (NPC cities had
// more buildings on the guest after a minute). Only the two AI growth callers are blocked;
// CreateBuildingByType (0xBE74A0, the fill-the-city loop 0xCFB827 waits for the count) is not.
// return addresses: SporeCoop::CivGrowth_Budget_Ret / CivGrowth_New_Ret
using CreateBuilding_t = void*(SK_THISCALL*)(void*, uint32_t, uint32_t);
static CreateBuilding_t o_CreateBuilding;
static void* SK_THISCALL h_CreateBuilding(void* city, uint32_t noun, uint32_t type)
{
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    if (!g_applying && is_guest() && game::mode_id() == modes::kCiv &&
        (ret == a_GrowthRetBudget || ret == a_GrowthRetNew)) {
        g_growth_blocked++;
        return nullptr; // both callers ignore the result (the host's growth comes as MSG_CIV_BUILD)
    }
    return o_CreateBuilding(city, noun, type);
}

// New cities of other nations appear over time at the planet's city sites (0xAE3A80 picks the next
// free site, the city materializes a few seconds later through a timed callback, docs/re/civ.md 9).
// The guest's own planet tick founded cities the host never had (and the host's came without their
// first buildings): guests only found the cities the host founds, in the same order.
constexpr uint8_t kNextCitySite = 0xFF; // MSG_CIV_BUILD city index: "found the next city site", noun 0
// (ex, bool instant, void* who): the runtime-checked order (docs/re/civ.md, BUG-0134)
using SpawnNextCity_t = bool(SK_THISCALL*)(void*, int, void*);
static SpawnNextCity_t o_SpawnNextCity;
static uint32_t g_cities_founded, g_cities_in, g_cities_blocked, g_cities_caught_up;
// BUG-0120: a founding broadcast while a guest was not in Civ (receiving or loading the world) was
// lost. The guest compares the host's city count of every snapshot with its own and founds the next
// site itself when it stays behind (`coop.civ fix cities 0`: only the event, the code before).
static bool g_fix_cities = true;
COOP_FIX("civ.cities", "BUG-0120", g_fix_cities, "civ: a guest founds an NPC city it missed from the host's count");
static double g_city_short_since = -1, g_last_city_spawn = -1e9;
static bool SK_THISCALL h_SpawnNextCity(void* ex, int instant, void* who)
{
    if (!g_applying && is_guest() && game::mode_id() == modes::kCiv) {
        g_cities_blocked++;
        return false; // both callers ignore the result
    }
    bool r = o_SpawnNextCity(ex, instant, who);
    if (r && is_host() && game::mode_id() == modes::kCiv && !who && !instant) {
        net::Writer w;
        w.u8(kNextCitySite);
        w.u32(0);
        w.f32(0); w.f32(0); w.f32(0);
        C.sess->broadcast_reliable(net::MSG_CIV_BUILD, w);
        g_cities_founded++;
    }
    return r;
}

// Money is the host's: the guest's local economy (its own copy of the claimed spice derricks, city
// income) ran too and every local gain went to the host as a shared-money delta (+80..+100 every few
// seconds: the treasury grew about twice as fast with a guest). Guests ignore AddMoney for the player
// civilization; their spending still reaches the host (SpendMoney is another function).
using AddMoney_t = void(SK_THISCALL*)(void*, float, int);
static AddMoney_t o_AddMoney;
static uint32_t g_income_blocked;
static void SK_THISCALL h_AddMoney(void* civ, float amount, int difficulty)
{
    if (founder_country(civ)) civ = player_civ();
    if (!g_applying && is_guest() && game::mode_id() == modes::kCiv && civ == player_civ() && amount > 0) {
        g_income_blocked++;
        return;
    }
    o_AddMoney(civ, amount, difficulty);
}

// Vehicles the game spawns on a guest by itself (BUG-0188): the other nations' AI (city tick,
// civilization AI) bought vehicles on every guest, and the next full snapshot killed them again
// (`removed=` grew). These call sites throw the result away (budget arithmetic only, or a null
// check: docs/re/civ.md#civ-ai-vehicle-spawns), so on a guest SpawnVehicle returns null for them.
// Other callers (the cheat message, the Space-only BFBA70, the player's free vehicle) still run.
// Every guest spawn is counted by its caller (`coop.civ spawns`); `coop.civ fix aispawn 0`: the
// code before (tests).
using SpawnVehicle_t = void*(SK_THISCALL*)(void*, int, int, uint32_t, uint32_t, uint32_t, int);
static SpawnVehicle_t o_SpawnVehicle;
static std::map<uintptr_t, uint32_t> g_local_spawns;
static bool g_fix_aispawn = true;
COOP_FIX("civ.aispawn", "BUG-0188", g_fix_aispawn, "civ: the NPC AI spawns no vehicles on guests");
static uint32_t g_ai_spawns_blocked;
static uintptr_t g_ai_spawn_ret[6];
static void* SK_THISCALL h_SpawnVehicle(void* city, int purpose, int loco, uint32_t k0, uint32_t k1, uint32_t k2, int space)
{
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    if (!g_applying && is_guest() && game::mode_id() == modes::kCiv) {
        g_local_spawns[ret - H->image_base() + 0x400000]++;
        if (g_fix_aispawn)
            for (uintptr_t a : g_ai_spawn_ret)
                if (a && a == ret) {
                    g_ai_spawns_blocked++;
                    return nullptr;
                }
    }
    return o_SpawnVehicle(city, purpose, loco, k0, k1, k2, space);
}

static void caption_arm(); // BUG-0224 (below)
using ChangeOwner_t = int(SK_THISCALL*)(void*, uint32_t, int, int);
static ChangeOwner_t o_ChangeOwner;
static int SK_THISCALL h_ChangeOwner(void* city, uint32_t faction, int quiet, int specialty)
{
    uint32_t old_faction = pol_of(city);
    bool team = g_team_progress && active() && g_founder_nations.contains_faction(faction);
    void* country = team ? thiscall<void*>(a_CivByPol, nm(), faction) : nullptr;
    void* previous = g_capture_founder;
    size_t mark = game::swap_open(game::swap_undo_u32, &g_capture_founder, (uintptr_t)previous,
                                 "Civ team capture branch");
    g_capture_founder = country;
    int result = o_ChangeOwner(city, faction, quiet, specialty);
    g_capture_founder = previous;
    game::swap_close(mark);
    if (team && is_host() && old_faction != faction && pol_of(city) == faction &&
        !g_founder_nations.contains_faction(old_faction)) {
        // Native history/goals are emitted by ChangeOwner's player branch.
        // The home nation's capture counters are the campaign totals used at
        // spaceflight; the partner's raw counters remain its individual record.
        int kind = F<int>(city, 0x29C);
        if (country != player_civ() && kind >= 0 && kind < 3)
            ++F<int>(player_civ(), 0x4C0 + 4 * kind);
        g_campaign_share_at = H->time_seconds() + 2;
    }
    return result;
}
using Capture_t = int(SK_THISCALL*)(void*, uint32_t, int, int, int, int);
static Capture_t o_Capture;
static int SK_THISCALL h_Capture(void* city, uint32_t pol, int type, int a3, int a4, int a5)
{
    if (is_guest() && game::mode_id() == modes::kCiv && !g_applying)
        return 0; // captures happen on the host and come back with the snapshot
    int r = o_Capture(city, pol, type, a3, a4, a5);
    if (is_host() && game::mode_id() == modes::kCiv) {
        caption_arm(); // every capture the host's game runs may show a caption that waits for OK
        void* pc = player_civ();
        if (pc && pol == pol_of(pc) && !g_city_owner.count(city)) {
            // the city goes to the player whose vehicle is closest (who took it)
            uint8_t owner = 0;
            float bd = 1e18f;
            for (auto& [v, hv] : g_hv) {
                if (hv.owner == 0xFE || (g_fix_orphans && !net::is_player(*C.sess, hv.owner)))
                    continue; // BUG-0031: never a player who is not here
                float d = net::dist(veh_pos(v), city_pos(city));
                if (d < bd) {
                    bd = d;
                    owner = hv.owner;
                }
            }
            g_city_owner[city] = owner;
            if (C.feed)
                C.feed(C.sess->name_of(owner) + " captured a city");
        }
    }
    return r;
}

// ---------------- co-op: siege, roles, alarm, superweapon (docs/re/civ.md, co-op features) -----
// The game sees all players as one civilization: who did it comes from the attacking vehicle and
// the mod's owner table.
constexpr double kJointWindow = 10.0;
static uint8_t g_hit_by = 0xFF;                         // the player whose vehicle is hitting right now
static std::unordered_map<void*, std::unordered_map<uint8_t, double>> g_siege; // city -> player -> last hit
static std::unordered_set<void*> g_joint_told;
static uint32_t g_joint_hits, g_military_hits, g_religious_boosts, g_alarms, g_votes;

static uint8_t player_of_combatant(void* att)
{
    if (!att || IsBadReadPtr(att, 4))
        return 0xFF;
    void* v = vcall<void*>(att, 0x5C / 4, 0x137E8E0u); // the vehicle behind a combatant
    if (!v)
        return 0xFF;
    auto it = g_hv.find(v);
    return it == g_hv.end() ? 0xFF : it->second.owner;
}

static uint8_t role(uint8_t p) { return C.role_of && p != 0xFF ? C.role_of(p) : 0; }

// building / city hall TakeDamage (this = building+0x120): a military governor's vehicles hit harder;
// the player is remembered for the capture damage the original adds
using BTakeDamage_t = int(SK_THISCALL*)(void*, float, uint32_t, int, const void*, void*);
static BTakeDamage_t o_BTakeDamage, o_HallTakeDamage;
static int b_damage(BTakeDamage_t orig, void* self, float dmg, uint32_t pol, int type, const void* dir, void* att)
{
    uint8_t p = is_host() && active() ? player_of_combatant(att) : 0xFF;
    if (p != 0xFF && role(p) == 1) {
        dmg *= 1.25f;
        g_military_hits++;
    }
    uint8_t prev = g_hit_by;
    size_t mark = game::swap_open(game::swap_undo_u8, &g_hit_by, prev, "capture attribution"); // BUG-0110
    g_hit_by = p;
    int r = orig(self, dmg, pol, type, dir, att);
    g_hit_by = prev;
    game::swap_close(mark);
    return r;
}
static int SK_THISCALL h_BTakeDamage(void* s, float d, uint32_t pol, int t, const void* dir, void* a)
{
    return b_damage(o_BTakeDamage, s, d, pol, t, dir, a);
}
static int SK_THISCALL h_HallTakeDamage(void* s, float d, uint32_t pol, int t, const void* dir, void* a)
{
    return b_damage(o_HallTakeDamage, s, d, pol, t, dir, a);
}

// capture damage: the city is taken faster when vehicles of two or more players attack it
using AddCapture_t = void(SK_CDECL*)(void*, void*, void*, uint32_t, float, uint32_t, bool);
static AddCapture_t o_AddCapture;
static void SK_CDECL h_AddCapture(void* ctx, void* city, void* meter, uint32_t owner, float dmg, uint32_t att, bool f)
{
    if (is_host() && active() && city && g_hit_by != 0xFF) {
        double now = H->time_seconds();
        auto& hits = g_siege[city];
        hits[g_hit_by] = now;
        int players = 0;
        for (auto& [p, t] : hits)
            if (now - t < kJointWindow)
                players++;
        if (players >= 2) {
            dmg *= 1.5f;
            g_joint_hits++;
            if (!g_joint_told.count(city) && C.notice) {
                g_joint_told.insert(city);
                C.notice("joint siege: the city falls 50% faster");
            }
        }
    }
    o_AddCapture(ctx, city, meter, owner, dmg, att, f);
}

// religious conversion: a religious governor's preachers near the city convert faster, and even
// more while another player's army besieges it
using Convert_t = void(SK_THISCALL*)(void*, uint32_t, int);
static Convert_t o_Convert;
static void SK_THISCALL h_Convert(void* city, uint32_t pol, int count)
{
    if (is_host() && active() && city && count > 0) {
        net::Vec3 cp = city_pos(city);
        uint8_t preacher = 0xFF;
        for (auto& [v, h] : g_hv)
            if (role(h.owner) == 3 && F<int>(v, kVPurpose) == 2 && net::dist(veh_pos(v), cp) < 200.f) {
                preacher = h.owner;
                break;
            }
        if (preacher != 0xFF) {
            float k = 1.5f;
            double now = H->time_seconds();
            auto it = g_siege.find(city);
            if (it != g_siege.end())
                for (auto& [p, t] : it->second)
                    if (p != preacher && now - t < kJointWindow)
                        k = 2.f; // combined arms
            count = (int)std::ceil(count * k);
            g_religious_boosts++;
        }
    }
    o_Convert(city, pol, count);
}

// a player's city is attacked: everybody sees it (beacon + notice), and the other players'
// vehicles near it rush there faster for a while
struct Boost {
    float orig;
    double until;
};
static std::unordered_map<void*, Boost> g_boost;
static std::unordered_map<void*, double> g_alarm_at;

static void show_alarm(void* city, uint8_t kind, uint8_t owner)
{
    if (!city || !C.beacon)
        return;
    std::string who = owner == kShared ? "the capital" : C.sess->name_of(owner) + "'s city";
    C.beacon(city_pos(city), (kind ? "† " : "× ") + who + (kind ? " is being converted" : " is under attack"),
             20.0, 0xFFFF4040);
}

using OnAttacked_t = void(SK_THISCALL*)(void*, int, uint32_t);
static OnAttacked_t o_OnAttacked;
static void SK_THISCALL h_OnAttacked(void* city, int kind, uint32_t att)
{
    o_OnAttacked(city, kind, att);
    if (!is_host() || !active() || kind > 1)
        return;
    uint8_t owner = owner_of_city(city);
    void* pc = player_civ();
    if (owner == 0xFF || (pc && att == pol_of(pc)))
        return;
    double now = H->time_seconds();
    if (now - g_alarm_at[city] < 20.0)
        return;
    g_alarm_at[city] = now;
    g_alarms++;
    int ci = host_index_of_city(city);
    net::Writer w;
    w.u8((uint8_t)ci); w.u8((uint8_t)kind); w.u8(owner);
    C.sess->broadcast_reliable(net::MSG_CIV_ALARM, w);
    show_alarm(city, (uint8_t)kind, owner);
    if (C.notice)
        C.notice(std::string(owner == kShared ? "the capital" : C.sess->name_of(owner) + "'s city") +
                 (kind ? " is being converted: help!" : " is under attack: help!"));
    // the defenders: vehicles of the other players within reach go 50% faster for 20 s
    net::Vec3 cp = city_pos(city);
    for (auto& [v, h] : g_hv) {
        if (g_fix_boost && !exists(v)) // g_hv is rebuilt every 0.1 s: a vehicle freed meanwhile
            continue;
        if (h.owner == owner || net::dist(veh_pos(v), cp) > 600.f)
            continue;
        auto& b = g_boost[v];
        if (b.until < now)
            b.orig = F<float>(v, 0x218);
        b.until = now + 20.0;
    }
}

static void boost_tick(double now)
{
    for (auto it = g_boost.begin(); it != g_boost.end();) {
        void* v = it->first;
        // BUG-0040: object_alive reads the vehicle's own memory and cannot tell a freed one: only a
        // vehicle in the game's own list is written to
        bool alive = g_fix_boost ? g_hv.count(v) && exists(v) : g_hv.count(v) && game::object_alive(v);
        if (!alive) {
            it = g_boost.erase(it);
            continue;
        }
        if (now > it->second.until) {
            F<float>(v, 0x218) = it->second.orig; // back to its own speed
            it = g_boost.erase(it);
            continue;
        }
        float want = it->second.orig * 1.5f; // RecalcStats rewrites it: keep it up every tick
        if (F<float>(v, 0x218) != want)
            F<float>(v, 0x218) = want;
        ++it;
    }
}

// superweapons are fired by a vote of all players
using FireSW_t = bool(SK_THISCALL*)(void*, int, const float*);
static FireSW_t o_FireSW;
static bool g_sw_bypass;
// BUG-0117: the vote's weapon and target are its own (captured), not statics a second request
// overwrote. `coop.civ fix sw_vote 0`: the old statics (tests).
static bool g_fix_sw_vote = true;
COOP_FIX("civ.sw_vote", "BUG-0117", g_fix_sw_vote, "civ: a superweapon vote keeps its own weapon and target");
static uint32_t g_sw_refused;
static int g_sw_last_req = -1, g_sw_fired = -1; // host: last guest request, weapon type the last guest vote fired
static bool SK_THISCALL h_FireSW(void* civ, int type, const float* target)
{
    if (g_sw_bypass || !active() || C.sess->players().size() < 2)
        return o_FireSW(civ, type, target);
    float pos[3] = {target ? target[0] : 0, target ? target[1] : 0, target ? target[2] : 0};
    if (is_guest()) { // the host asks everybody
        net::Writer w;
        w.u8(5 /* CMD_SUPER */);
        w.u32((uint32_t)type);
        w.f32(pos[0]); w.f32(pos[1]); w.f32(pos[2]);
        send_cmd(w);
        return false;
    }
    if (g_fix_sw_vote) {
        // BUG-0117: the vote fires what was asked in it; a request during another vote changes nothing
        std::array<float, 3> p{pos[0], pos[1], pos[2]};
        bool has_target = target != nullptr;
        if (C.vote && C.vote(C.sess->name_of(C.sess->my_id()) + " wants to fire a superweapon", "the superweapon fires",
                             [civ, type, p, has_target] {
                                 g_sw_bypass = true;
                                 o_FireSW(civ, type, has_target ? p.data() : nullptr);
                                 g_sw_bypass = false;
                             }))
            g_votes++;
        else {
            g_sw_refused++;
            if (C.feed)
                C.feed("superweapon: another vote is running, ask again when it is over");
        }
        return false;
    }
    static void* s_civ;
    static int s_type;
    static float s_pos[3];
    s_civ = civ; s_type = type;
    memcpy(s_pos, pos, sizeof s_pos);
    bool has_target = target != nullptr;
    if (C.vote && C.vote(C.sess->name_of(C.sess->my_id()) + " wants to fire a superweapon", "the superweapon fires",
                         [has_target] {
                             g_sw_bypass = true;
                             o_FireSW(s_civ, s_type, has_target ? s_pos : nullptr);
                             g_sw_bypass = false;
                         }))
        g_votes++;
    return false;
}

static void host_superweapon_request(uint8_t from, net::Reader& r)
{
    int type = (int)r.u32();
    float pos[3] = {r.f32(), r.f32(), r.f32()};
    void* pc = player_civ();
    // the game's superweapon types are 0..11 (GetSuperweaponCost, docs/re/civ.md)
    if (!r.ok() || !pc || !C.vote || type < 0 || type > 11 || !net::finite3(pos))
        return;
    g_sw_last_req = type;
    if (g_fix_sw_vote) {
        std::array<float, 3> p{pos[0], pos[1], pos[2]};
        if (C.vote(C.sess->name_of(from) + " wants to fire a superweapon", "the superweapon fires", [pc, type, p] {
                g_sw_bypass = true;
                g_sw_fired = type;
                o_FireSW(pc, type, p.data());
                g_sw_bypass = false;
            }))
            g_votes++;
        else { // tell the sender its request was not taken
            g_sw_refused++;
            net::Writer nw;
            nw.str8("superweapon: another vote is running, ask again when it is over");
            C.sess->send_reliable_to(from, net::MSG_NOTICE, nw);
        }
        return;
    }
    static int s_type;
    static float s_pos[3];
    static void* s_civ;
    s_type = type; s_civ = pc;
    memcpy(s_pos, pos, sizeof s_pos);
    if (C.vote(C.sess->name_of(from) + " wants to fire a superweapon", "the superweapon fires", [] {
            g_sw_bypass = true;
            g_sw_fired = s_type;
            o_FireSW(s_civ, s_type, s_pos);
            g_sw_bypass = false;
        }))
        g_votes++;
}

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    struct {
        uintptr_t* a;
        const char* n;
    } syms[] = {
        {&a_GetAllCivs, "Simulator::cGameNounManager::GetAllCivilizations"},
        {&a_GetAllCities, "Simulator::cGameNounManager::GetAllCities"},
        {&a_GetAllVehicles, "Simulator::cGameNounManager::GetAllVehicles"},
        {&a_CivByPol, "Simulator::cGameNounManager::GetCivilizationByPoliticalID"},
        {&a_PlayerCiv, "Simulator::cGameNounManager::GetPlayerCivilization"},
        {&a_GetMoney, "Simulator::cCivilization::GetMoney"},
        {&a_SpendMoney, "Simulator::cCivilization::SpendMoney"},
        {&a_CreateVehicle, "Simulator::cCivilization::CreateVehicle"},
        {&a_SlotsLeft, "Simulator::cCivilization::VehicleSlotsLeft"},
        {&a_VehicleCost, "Simulator::cCivilization::GetVehicleCost"},
        {&a_DesignKey, "Simulator::cCivilization::GetDesignKey"},
        {&a_DesignId, "Simulator::VehicleDesignId"},
        {&a_SpawnVehicle, "Simulator::cCity::SpawnVehicle"},
        {&a_AddBuilding, "Simulator::cCity::AddBuilding"},
        {&a_SetBuildingModel, "Simulator::cCity::SetBuildingModel"},
        {&a_SnapToSlot, "Simulator::cCity::SnapBuildingToSlot"},
        {&a_ChangeOwner, "Simulator::cCity::ChangeOwner"},
        {&a_Capture, "Simulator::cCity::Capture"},
        {&a_SetOrder, "Simulator::cVehicle::SetOrder"},
        {&a_SetMoveOrder, "Simulator::cVehicle::SetMoveOrder"},
        {&a_OrderStamp, "Simulator::cVehicle::OrderStamp"},
        {&a_Die, "Simulator::cVehicle::Die"},
        {&a_SetSelected, "Simulator::cVehicle::SetIsSelected"},
        {&a_PushEvent, "Simulator::cVehicle::PushEvent"},
        {&a_BTTick, "Simulator::BehaviorTreeTickGeneric"},
        {&a_CreateInstance, "Simulator::cGameNounManager::CreateInstance"},
        {&a_UIMessage, "UI::RolloverCivCity::HandleUIMessage"},
        {&a_CreateBuilding, "Simulator::cCity::CreateBuilding"},
        {&a_NodeClaim, "Simulator::cCommodityNode::Claim"},
        {&a_NodeUnclaim, "Simulator::cCommodityNode::Unclaim"},
        {&a_SetCapture, "Simulator::cCity::SetCaptureProgress"},
        {&a_SpawnNextCity, "Simulator::cCivExpansion::SpawnNextCity"},
        {&a_ExpansionGet, "Simulator::cCivExpansion::Get"},
        {&a_CinematicMgr, "Simulator::cCinematicManager::Get"},
        {&a_DestroyBuilding, "Simulator::cCity::DestroyBuilding"},
        {&a_CombatantDamage, "Simulator::cCombatant::TakeDamage"},
        {&a_AddMoney, "Simulator::cCivilization::AddMoney"},
        {&a_VehicleVt, "Simulator::cVehicle::vftable"},
        {&a_PlannerAddBuildingRet, "SporeCoop::CityPlanner_AddBuilding_Ret"},
        {&a_GrowthRetBudget, "SporeCoop::CivGrowth_Budget_Ret"},
        {&a_GrowthRetNew, "SporeCoop::CivGrowth_New_Ret"},
    };
    g_ok = true;
    for (auto& s : syms)
        if (!(*s.a = H->sym(s.n))) {
            H->log(M, SK_WARN, "civ: missing symbol %s", s.n);
            g_ok = false;
        }
    if (!g_ok)
        return false;
    H->hook_create(M, (void*)a_BTTick, (void*)h_BT, (void**)&o_BT);
    if (uintptr_t kg = H->sym("SporeCoop::Vehicle_KeepOnValidGround")) // optional (BUG-0186, GOG 2024)
        H->hook_create(M, (void*)kg, (void*)h_KeepGround, (void**)&o_KeepGround);
    H->hook_create(M, (void*)a_SetSelected, (void*)h_SetSelected, (void**)&o_SetSelected);
    H->hook_create(M, (void*)a_SetOrder, (void*)h_SetOrder, (void**)&o_SetOrder);
    H->hook_create(M, (void*)a_SetMoveOrder, (void*)h_SetMoveOrder, (void**)&o_SetMoveOrder);
    H->hook_create(M, (void*)a_UIMessage, (void*)h_UIMessage, (void**)&o_UIMessage);
    H->hook_create(M, (void*)a_AddBuilding, (void*)h_AddBuilding, (void**)&o_AddBuilding);
    H->hook_create(M, (void*)a_Capture, (void*)h_Capture, (void**)&o_Capture);
    H->hook_create(M, (void*)a_CreateBuilding, (void*)h_CreateBuilding, (void**)&o_CreateBuilding);
    H->hook_create(M, (void*)a_SpawnNextCity, (void*)h_SpawnNextCity, (void**)&o_SpawnNextCity);
    H->hook_create(M, (void*)a_AddMoney, (void*)h_AddMoney, (void**)&o_AddMoney);
    {
        auto city_owned = H->sym("SporeCoop::City_IsPlayerOwned");
        auto vehicle_owned = H->sym("SporeCoop::Vehicle_IsPlayerOwned");
        auto civ_ai = H->sym("SporeCoop::Civilization_AI");
        auto terrain_cursor = H->sym("Simulator::cGameTerrainCursor::GetTerrainCursor");
        a_CivAIPlayerRet = H->sym("SporeCoop::Civilization_AI_PlayerCiv_Ret");
        if (city_owned && vehicle_owned && civ_ai && terrain_cursor && a_CivAIPlayerRet) {
            H->hook_create(M, (void*)city_owned, (void*)h_CityOwned, (void**)&o_CityOwned);
            H->hook_create(M, (void*)vehicle_owned, (void*)h_VehicleOwned, (void**)&o_VehicleOwned);
            H->hook_create(M, (void*)a_PlayerCiv, (void*)h_PlayerCiv, (void**)&o_PlayerCiv);
            H->hook_create(M, (void*)civ_ai, (void*)h_CivAI, (void**)&o_CivAI);
            H->hook_create(M, (void*)a_GetMoney, (void*)h_GetMoney, (void**)&o_GetMoney);
            H->hook_create(M, (void*)a_SpendMoney, (void*)h_SpendMoney, (void**)&o_SpendMoney);
            H->hook_create(M, (void*)terrain_cursor, (void*)h_TerrainCursor, (void**)&o_TerrainCursor);
            uintptr_t get = H->sym("SporeCoop::TerrainCursor_GetSelected");
            uintptr_t box = H->sym("SporeCoop::TerrainCursor_BoxSelect");
            uintptr_t apply = H->sym("SporeCoop::TerrainCursor_ApplySelection");
            g_offline_selection_ready = get && box && apply &&
                H->hook_create(M, (void*)get, (void*)h_GetSelected, (void**)&o_GetSelected) == 0 &&
                H->hook_create(M, (void*)box, (void*)h_BoxSelect, (void**)&o_BoxSelect) == 0 &&
                H->hook_create(M, (void*)apply, (void*)h_ApplySelection, (void**)&o_ApplySelection) == 0;
        } else g_founder_control = false;
    }
    {
        auto unlocks = H->sym("SporeCoop::Civilization_RefreshUnlocks");
        auto tech = H->sym("SporeCoop::Civilization_SetVehicleTech");
        auto count = H->sym("Simulator::cCivilization::GetCityCount");
        a_FinalCityCount = H->sym("SporeCoop::Civilization_FinalCityCount");
        a_ChangeOwnerPlayerRet = H->sym("SporeCoop::City_ChangeOwner_PlayerCiv_Ret");
        a_UnlockCountRet = H->sym("SporeCoop::CivUI_UnlockCityCount_Ret");
        a_TechCountRet = H->sym("SporeCoop::CivUI_TechCityCount_Ret");
        if (g_founder_control && unlocks && tech && count && a_FinalCityCount &&
            a_ChangeOwnerPlayerRet && a_UnlockCountRet && a_TechCountRet) {
            H->hook_create(M, (void*)unlocks, (void*)h_Unlocks, (void**)&o_Unlocks);
            H->hook_create(M, (void*)tech, (void*)h_Tech, (void**)&o_Tech);
            H->hook_create(M, (void*)count, (void*)h_CityCount, (void**)&o_CityCount);
            H->hook_create(M, (void*)a_ChangeOwner, (void*)h_ChangeOwner, (void**)&o_ChangeOwner);
            auto progress = H->sym("SporeCoop::CivMode_Progress");
            a_HudProgressRet = H->sym("SporeCoop::CivHUD_Progress_Ret");
            if (progress && a_HudProgressRet && o_CityCount)
                H->hook_create(M, (void*)progress, (void*)h_HudProgress, (void**)&o_HudProgress);
        } else g_team_progress = false;
    }
    {
        static const char* rets[6] = {"SporeCoop::CivAI_CityTick_SpawnVehicle_Ret", "SporeCoop::CivAI_CityOrders_SpawnVehicle_Ret",
                                      "SporeCoop::CivAI_BF9960_SpawnVehicle_Ret", "SporeCoop::CivAI_BF9FB0_SpawnVehicle_Ret",
                                      "SporeCoop::CivAI_BFA7A0_SpawnVehicle_Ret", "SporeCoop::CivAI_BFB160_SpawnVehicle_Ret"};
        for (int i = 0; i < 6; i++)
            g_ai_spawn_ret[i] = H->sym(rets[i]);
    }
    H->hook_create(M, (void*)a_SpawnVehicle, (void*)h_SpawnVehicle, (void**)&o_SpawnVehicle);
    // co-op mechanics (optional: a missing symbol only disables that mechanic)
    struct Opt {
        const char* name;
        void* hook;
        void** orig;
    } opts[] = {
        {"Simulator::cBuilding::TakeDamage", (void*)h_BTakeDamage, (void**)&o_BTakeDamage},
        {"Simulator::cCityHall::TakeDamage", (void*)h_HallTakeDamage, (void**)&o_HallTakeDamage},
        {"Simulator::cCity::AddCaptureDamage", (void*)h_AddCapture, (void**)&o_AddCapture},
        {"Simulator::cCity::ConvertCitizens", (void*)h_Convert, (void**)&o_Convert},
        {"Simulator::cCity::OnAttacked", (void*)h_OnAttacked, (void**)&o_OnAttacked},
        {"Simulator::cCivilization::FireSuperweapon", (void*)h_FireSW, (void**)&o_FireSW},
    };
    for (auto& o : opts)
        if (uintptr_t a = H->sym(o.name))
            H->hook_create(M, (void*)a, o.hook, o.orig);
    return true;
}

// ---------------- host ----------------
COOP_FIX("civ.auto_founders", "BUG-9046", g_auto_founders,
         "civ: found a separate capital for the saved Tribal partner");
static uint64_t g_founder_epoch;
static bool g_founder_queued;
static double g_founder_poll;
static std::string g_founder_planet, g_founder_notice;

static void founder_tick(double now)
{
    if (!g_auto_founders || !is_host() || g_founder_queued || now < g_founder_poll ||
        world::load_queued() || world::save_busy() || world::save_pending() ||
        world::time_paused() || world::cinematic_playing()) return;
    g_founder_poll = now + 2;
    g_founder_queued = true;
    H->run_outside_frame([](void* context) {
        uint64_t epoch = *(uint64_t*)context; delete (uint64_t*)context;
        if (epoch != g_founder_epoch) return;
        g_founder_queued = false;
        if (!g_auto_founders || !active() || !is_host() || world::load_queued() || world::save_busy() ||
            world::save_pending() || world::time_paused() || world::cinematic_playing()) return;
        auto planet = world::current_game_key();
        void* home = player_civ();
        if (planet.empty() || !home) return;
        if (planet != g_founder_planet) {
            g_founder_planet = planet; g_founder_spawned = false; g_founder_nations = {};
        }
        auto report = [](const std::string& text) {
            if (text == g_founder_notice) return;
            g_founder_notice = text;
            H->log(M, SK_INFO, "civ automatic founders: %s", text.c_str());
        };
        founders::Roster tribal, saved;
        founders::Outfits outfits;
        std::string error;
        if (founders::read(planet, saved, founders::Stage::Civilization)) {
            if (pol_of(home) != saved.home().faction || F<creations::Key>(home, kNationSpecies).instance != saved.home().species) {
                report("The native home civilization does not match the saved founders."); return;
            }
            void* partner = nullptr;
            auto cities = all_cities();
            for (void* city : cities) if (pol_of(city) == saved.guest().faction) partner = F<void*>(city, kCityCiv);
            if (!partner) {
                // The roster may have reached disk immediately before an
                // interrupted native save. Recreate only an absent reserved
                // capital, never replace a different existing nation's city.
                if (!g_founder_spawned && cities.size() == 1) {
                    if (void* ex = skg::cdecl_call<void*>(a_ExpansionGet))
                        g_founder_spawned = h_SpawnNextCity(ex, 0, nullptr);
                }
                report("Waiting for the saved partner capital to finish founding."); return;
            }
            auto& key = F<creations::Key>(partner, kNationSpecies);
            if (key.instance && key.instance != saved.guest().species) {
                report("The reserved capital belongs to another species; its identity was retained."); return;
            }
            bool restored = !key.instance;
            key = {saved.guest().species, 0x2B978C46, 0x40626200};
            g_founder_nations = saved;
            if (restored) {
                if (C.sess->players().size() == 1) {
                    if (!world::save_game(error)) report(error);
                } else for (const auto& player : C.sess->players()) if (player.id)
                    if (!world::share_with(player.id, error, true)) report(error);
            }
            return;
        }
        if (!founders::read(planet, tribal)) return; // legacy/stage-chooser game
        if (!founders::read_outfits(planet, outfits, error)) { report(error); return; }
        auto host_key = F<creations::Key>(home, kNationSpecies);
        creations::Key guest_key{outfits.current(1), 0x2B978C46, 0x40626200};
        if (!outfits.contains(0, host_key.instance) || !guest_key.instance ||
            guest_key == host_key || !creations::key_known(guest_key)) {
            report("Waiting for the saved founding species before establishing the partner capital."); return;
        }
        auto cities = all_cities();
        if (!g_founder_spawned) {
            // Only start from the native graduation's single capital. Never
            // repurpose an established NPC city in an older Civilization save.
            if (cities.size() != 1 || F<void*>(cities[0], kCityCiv) != home) {
                report("The partner capital was not reserved before other nations appeared."); return;
            }
            void* expansion = skg::cdecl_call<void*>(a_ExpansionGet);
            if (!expansion) return;
            g_founder_spawned = h_SpawnNextCity(expansion, 0, nullptr);
            report(g_founder_spawned ? "Native partner capital founding requested." : "No free native city site.");
            return;
        }
        if (cities.size() != 2) return; // native appearance is asynchronous
        void* nation = nullptr;
        for (void* city : cities) {
            void* country = F<void*>(city, kCityCiv);
            if (!country || country == home) continue;
            if (nation || pol_of(city) != pol_of(country) || F<creations::Key>(city, kCitySpecies).instance ||
                F<creations::Key>(country, kNationSpecies).instance) {
                report("The new capital already has a species; its identity was retained."); return;
            }
            nation = country;
        }
        if (!nation) return;
        founders::Roster pair{host_key.instance, guest_key.instance, pol_of(home), pol_of(nation)};
        if (!founders::record(planet, pair, error, founders::Stage::Civilization)) { report(error); return; }
        F<creations::Key>(nation, kNationSpecies) = guest_key;
        g_founder_nations = pair;
        report("Both original species now have native Civilization capitals.");
        if (C.sess->players().size() == 1 && !world::save_game(error)) report(error);
        for (const auto& player : C.sess->players()) if (player.id)
            if (!world::share_with(player.id, error, true)) report(error);
    }, new uint64_t(g_founder_epoch));
}

static void campaign_tick(double now)
{
    if (!g_team_progress || !g_founder_nations.guest().faction || g_progress_queued || now < g_progress_poll ||
        world::load_queued() || world::save_busy() || world::save_pending() || world::cinematic_playing()) return;
    g_progress_poll = now + 0.5;
    g_progress_queued = true;
    H->run_outside_frame([](void* context) {
        uint64_t epoch = *(uint64_t*)context; delete (uint64_t*)context;
        if (epoch != g_founder_epoch) return;
        g_progress_queued = false;
        if (!g_team_progress || !active() || world::load_queued() || world::save_busy() ||
            world::save_pending() || world::cinematic_playing()) return;
        void* home = player_civ();
        void* guest = thiscall<void*>(a_CivByPol, nm(), g_founder_nations.guest().faction);
        if (!home || !guest) return;
        auto cities = team_cities(home);
        if (cities.empty()) return;
        int tech = team_tech(home, std::max(F<int>(home, 0x298), F<int>(guest, 0x298)));
        int highest = std::max({(int)cities.size(), F<int>(home, 0x4D0), F<int>(guest, 0x4D0)});
        for (void* country : {guest, home}) {
            o_Tech(country, tech);
            F<int>(country, 0x4D0) = highest;
            h_Unlocks(country);
        }
        for (unsigned i = 0; i < 12; ++i) {
            uint8_t unlocked = F<uint8_t>(home, 0x484 + i) | F<uint8_t>(guest, 0x484 + i);
            F<uint8_t>(home, 0x484 + i) = F<uint8_t>(guest, 0x484 + i) = unlocked;
        }
        // SetVehicleTech can start the native unlock caption. Check again
        // after that call before posting an asynchronous campaign save.
        if (world::cinematic_playing() || world::time_paused()) return;
        if (is_host() && g_campaign_share_at && H->time_seconds() >= g_campaign_share_at) {
            std::string error;
            bool saved = true;
            if (C.sess->players().size() == 1) saved = world::save_game(error);
            else for (const auto& player : C.sess->players()) if (player.id)
                saved &= world::share_with(player.id, error, true);
            if (saved) g_campaign_share_at = 0;
            else H->log(M, SK_WARN, "civ campaign checkpoint: %s", error.c_str());
        }
    }, new uint64_t(g_founder_epoch));
}

static void host_assign_cities(const std::vector<uint8_t>& players)
{
    void* pc = player_civ();
    if (!pc || players.empty())
        return;
    auto cities = vec_of((uint8_t*)pc + kCivCities);
    if (g_founder_control && g_founder_nations.guest().faction) {
        g_city_owner.clear();
        for (void* city : all_cities()) {
            uint8_t owner = founder_owner(pol_of(city));
            if (owner != 0xFF) g_city_owner[city] = owner;
        }
        g_split_for = players;
        return;
    }
    // drop cities the player lost
    for (auto it = g_city_owner.begin(); it != g_city_owner.end();) {
        if (std::find(cities.begin(), cities.end(), it->first) == cities.end())
            it = g_city_owner.erase(it);
        else
            ++it;
    }
    bool resplit = players != g_split_for;
    g_split_for = players;
    if (cities.empty())
        return;
    // the first city of the player civ is the capital: the host's; the others in turns
    net::Vec3 cap = city_pos(cities[0]);
    std::sort(cities.begin() + 1, cities.end(),
              [&](void* a, void* b) { return net::dist(city_pos(a), cap) < net::dist(city_pos(b), cap); });
    // fewer cities than players: the capital is shared so that everybody can play from the start
    bool shared = cities.size() < players.size();
    if (shared != (g_city_owner.count(cities[0]) && g_city_owner[cities[0]] == kShared))
        resplit = true;
    size_t k = 0;
    for (size_t i = 0; i < cities.size(); i++) {
        if (!resplit && g_city_owner.count(cities[i]))
            continue;
        if (g_city_gifted.count(cities[i]) && g_city_owner.count(cities[i]) &&
            std::find(players.begin(), players.end(), g_city_owner[cities[i]]) != players.end())
            continue; // a gift stays with its new owner while that player is here
        g_city_owner[cities[i]] = i == 0 ? (shared ? kShared : 0) : players[(1 + k++) % players.size()];
    }
}

// Trade between the players: every pair of cities of two different players trades, the shared
// treasury earns kTradePerPair every kTradePeriod seconds (a player alone earns nothing extra)
constexpr double kTradePeriod = 30.0;
constexpr int kTradePerPair = 20;
static double g_last_trade;
static int g_trade_total, g_trade_pairs, g_trade_payouts;

static void trade_tick(double now)
{
    if (now - g_last_trade < kTradePeriod)
        return;
    g_last_trade = now;
    std::vector<std::pair<void*, uint8_t>> owned;
    for (auto& [city, o] : g_city_owner)
        if (o < kShared)
            owned.push_back({city, o});
    int pairs = 0, gain = 0;
    auto economic = [](uint8_t p) { return C.role_of && C.role_of(p) == 2; };
    for (size_t i = 0; i < owned.size(); i++)
        for (size_t j = i + 1; j < owned.size() && pairs < 8; j++)
            if (owned[i].second != owned[j].second) {
                pairs++;
                // an economic governor at either end doubles the pair (both: triple)
                gain += kTradePerPair * (1 + economic(owned[i].second) + economic(owned[j].second));
            }
    g_trade_pairs = pairs;
    if (!pairs)
        return;
    std::string err;
    if (!shared::debug_add("money", gain, err))
        return;
    g_trade_total += gain;
    if (g_trade_payouts++ % 6 == 0 && C.notice) // now and then, not every payout
        C.notice("trade between the players' cities: +" + std::to_string(gain) + " every 30 s (" +
                 std::to_string(g_trade_total) + " so far)");
}

// spice nodes (cCommodityNode, noun 0x403DF5C): position and owner political id (-1 free)
constexpr uint32_t kNodeNoun = 0x403DF5C;
static std::vector<std::pair<net::Vec3, uint32_t>> spice_nodes()
{
    std::vector<std::pair<net::Vec3, uint32_t>> r;
    game::for_each_object(
        [](void* o, uint32_t n, void* u) {
            net::Vec3 p;
            if (n == kNodeNoun && game::object_alive(o) && game::object_position(o, p))
                ((std::vector<std::pair<net::Vec3, uint32_t>>*)u)->push_back({p, pol_of(o)});
            return true;
        },
        &r);
    return r;
}


// BUG-0186 probe (coop.civ pushprobe, tests): who moves a mirror on the guest. The vehicles' spatial
// vtable slots SetPosition (0x38) and Teleport (0x44) are pointed here; a call on a mirror that moves
// it more than 2 m, from the game (not guest_tick), is logged with its return address.
using SpSetPos_t = void(__thiscall*)(void*, const float*);
using SpTeleport_t = void(__thiscall*)(void*, const float*, const float*);
static SpSetPos_t g_probe_setpos;
static SpTeleport_t g_probe_tp;
static uintptr_t* g_probe_vt;
static unsigned g_probe_hits;
static void probe_note(void* sp, const float* p, const char* what, void* ret)
{
    if (g_applying || !p || g_probe_hits >= 40)
        return;
    void* v = (uint8_t*)sp - kVSpatial;
    auto it = g_gv_of.find(v);
    if (it == g_gv_of.end())
        return;
    net::Vec3 cur = veh_pos(v), to{p[0], p[1], p[2]};
    if (net::dist(cur, to) < 2.f)
        return;
    g_probe_hits++;
    H->log(M, SK_WARN, "civ: probe: %s of mirror #%u by %p: (%.1f %.1f %.1f) -> (%.1f %.1f %.1f)", what,
           (unsigned)it->second, ret, cur.x, cur.y, cur.z, to.x, to.y, to.z);
}
static void __fastcall probe_setpos(void* sp, void*, const float* p)
{
    probe_note(sp, p, "SetPosition", __builtin_return_address(0));
    g_probe_setpos(sp, p);
}
static void __fastcall probe_tp(void* sp, void*, const float* p, const float* q)
{
    probe_note(sp, p, "Teleport", __builtin_return_address(0));
    g_probe_tp(sp, p, q);
}
static std::string push_probe()
{
    if (g_probe_vt)
        return "probe on";
    void* v = nullptr;
    for (auto& [id, g] : g_gv)
        if (g.v && game::object_alive(g.v)) {
            v = g.v;
            break;
        }
    if (!v)
        return "no mirror";
    uintptr_t* vt = *(uintptr_t**)((uint8_t*)v + kVSpatial);
    DWORD old;
    if (!VirtualProtect(vt + 0x38 / 4, 0x10, PAGE_READWRITE, &old))
        return "no access";
    g_probe_setpos = (SpSetPos_t)vt[0x38 / 4];
    g_probe_tp = (SpTeleport_t)vt[0x44 / 4];
    vt[0x38 / 4] = (uintptr_t)&probe_setpos;
    vt[0x44 / 4] = (uintptr_t)&probe_tp;
    VirtualProtect(vt + 0x38 / 4, 0x10, old, &old);
    g_probe_vt = vt;
    char b[96];
    snprintf(b, sizeof b, "probe on vtable %p", (void*)vt);
    return b;
}
static void boost_tick(double now);
// a vehicle given away (or taken by a capture) stays selected on the giver's screen and its orders
// went to the game directly on the host: only the owner keeps a vehicle selected
static void drop_foreign_selection()
{
    uint8_t me = C.sess->my_id();
    for (void* v : all_vehicles())
        if (F<uint8_t>(v, kVSelected)) {
            uint8_t o = owner_of_vehicle(v);
            if (o != 0xFF && o != me) {
                thiscall<int>(a_SetSelected, (uint8_t*)v + kVSpatial, 0);
                g_unselected++;
            }
        }
}

// The host's world stands still while one of its cinematics plays (a captured city's caption waits
// for OK): everybody is told why the vehicles stopped
static bool g_movie;
static bool movie_playing()
{
    void* cm = a_CinematicMgr ? skg::cdecl_call<void*>(a_CinematicMgr) : nullptr;
    return cm && F<uint32_t>(cm, 0x2C) != 0;
}
// BUG-0187: with guests in the session the host's caption closes itself after kCaptionWait seconds
// (the game's EndCinematic, as Enter does), so a host who is away does not stop everybody's game.
// `coop.civ fix caption 0`: the old wait for the host's OK (tests).
static bool g_fix_caption = true;
COOP_FIX("civ.caption", "BUG-0187", g_fix_caption, "civ: the host's capture caption closes itself when guests wait");
static double g_caption_wait = 8.0, g_movie_since;
static uint32_t g_captions_closed;
// BUG-0224: only a cinematic that a capture started is ended. Before, every cinematic of the host
// in Civ (a city purchase, the camera when a city appears, superweapons) was cut after 8 s. A
// capture (h_Capture, host, any nation) arms; the 8 s count from the arm time while a cinematic
// plays (a purchase chain may never drop +0x2C between its cinematic and the caption). Disarmed
// when the cinematic ends after it was seen, when no cinematic came within kArmWindow, after an
// end, and at a session end or world change. Accepted: a capture during another long cinematic
// ends that one (the caption is queued behind it and everybody waits anyway).
// `coop.fix civ.caption_only off`: every cinematic is ended after 8 s (0.7.3.0).
static bool g_fix_caption_only = true;
COOP_FIX("civ.caption_only", "BUG-0224", g_fix_caption_only, "civ: only a capture's caption is closed after 8 s, other cinematics play on");
constexpr double kArmWindow = 3.0;
static double g_caption_armed;  // time of the last capture, 0 = not armed
static bool g_armed_seen;       // a cinematic played since the arm
static uint32_t g_caption_arms; // tests
static void caption_arm()
{
    g_caption_armed = H->time_seconds();
    g_armed_seen = false;
    g_caption_arms++;
}
static void caption_disarm()
{
    g_caption_armed = 0;
    g_armed_seen = false;
}
static void movie_tick()
{
    bool on = movie_playing();
    double now = H->time_seconds();
    if (g_caption_armed > 0) {
        if (on)
            g_armed_seen = true;
        else if (g_armed_seen || now - g_caption_armed > kArmWindow)
            caption_disarm(); // the caption ended, or the capture showed none
    }
    double since = g_fix_caption_only ? (g_caption_armed > 0 && g_movie_since > 0 ? g_caption_armed : -1) : g_movie_since;
    if (on && g_movie && g_fix_caption && since > 0 && now - since > g_caption_wait &&
        C.sess->players().size() >= 2) {
        g_movie_since = -1; // once per cinematic
        caption_disarm();
        H->run_outside_frame(
            [](void*) {
                uintptr_t end = H->sym("SporeCoop::cCinematicManager_EndCinematic");
                void* cm = a_CinematicMgr ? skg::cdecl_call<void*>(a_CinematicMgr) : nullptr;
                if (!end || !cm || !F<uint32_t>(cm, 0x2C) || game::mode_id() != modes::kCiv)
                    return;
                thiscall<void>(end, cm);
                g_captions_closed++;
                if (C.notice)
                    C.notice("the caption closed by itself: the world goes on");
            },
            nullptr);
    }
    if (on == g_movie)
        return;
    g_movie = on;
    g_movie_since = on ? now : 0;
    if (on && C.notice)
        C.notice(C.sess->name_of(C.sess->my_id()) + " watches a cinematic: the world waits for the OK" +
                 (g_fix_caption && (!g_fix_caption_only || g_caption_armed > 0) && C.sess->players().size() >= 2 ? " (or " + std::to_string((int)g_caption_wait) + " s)" : ""));
}

static void host_tick(double now)
{
    trade_tick(now);
    boost_tick(now);
    movie_tick();
    if (now - g_last_send < 0.1)
        return;
    g_last_send = now;
    drop_foreign_selection();
    std::vector<uint8_t> players;
    for (auto& p : C.sess->players())
        players.push_back(p.id);
    std::sort(players.begin(), players.end());
    bool roster_changed = players != g_split_for;
    host_assign_cities(players);
    // BUG-0031: the vehicles of a player who left go to the owner of the nearest player city (the
    // cities were split anew just now); before, nobody could order them, they took slots of the
    // vehicle cap and the next joiner under the reused id inherited them
    // BUG-0223: the game's vehicle list first, then the table pruned against it (alive check before
    // the pol read), then the orphan loop over vehicles that still exist: before, both read the memory
    // of vehicles that may have been freed since the last tick
    void* pc = player_civ();
    uint32_t ppol = pc ? pol_of(pc) : 0;
    g_host_cities = all_cities();
    auto vehicles = all_vehicles();
    std::unordered_set<void*> alive(vehicles.begin(), vehicles.end());
    for (auto it = g_hv.begin(); it != g_hv.end();) {
        // a vehicle of another world (or another nation) at an address we knew: assigned anew
        bool gone = !alive.count(it->first);
        bool other = !gone && g_fix_world && pol_of(it->first) != it->second.pol;
        if (gone || other) {
            g_hv_by_id.erase(it->second.id);
            it = g_hv.erase(it);
        } else
            ++it;
    }
    if (roster_changed || g_command_refresh) {
        g_command_refresh = false;
        for (auto& [v, hv] : g_hv) {
            const auto founder = founder_owner(hv.pol);
            if (founder != 0xFF && (!hv.gifted || !net::is_player(*C.sess, hv.owner))) {
                hv.owner = founder;
                hv.gifted = false;
                continue;
            }
            if (!g_fix_orphans) continue;
            if (hv.owner >= 0xFE || std::binary_search(players.begin(), players.end(), hv.owner))
                continue;
            uint8_t owner = 0;
            nearest_player_city(veh_pos(v), &owner);
            if (owner == 0xFF || owner == kShared || !std::binary_search(players.begin(), players.end(), owner))
                owner = 0;
            hv.owner = owner;
            g_orphans_given++;
        }
    }
    for (void* v : vehicles) {
        if (g_hv.count(v))
            continue;
        uint8_t owner = 0xFE;
        auto founder = founder_owner(pol_of(v));
        if (founder != 0xFF) owner = founder;
        else if (pol_of(v) == ppol) {
            if (g_pending_owner >= 0)
                owner = (uint8_t)g_pending_owner;
            else
                nearest_player_city(veh_pos(v), &owner); // built by that city
            if (owner == 0xFF || owner == kShared)
                owner = 0;
        }
        uint16_t id = net::alloc_id(g_next_id, g_hv_by_id); // BUG-0154: never a live id, never 0
        g_hv[v] = {id, owner, pol_of(v)};
        g_hv_by_id[id] = v;
    }
    g_cycle++;
    // Chunks that fit one packet (kMaxPacket): a larger message is dropped by the connection for
    // good. Until 2026-10-23 chunk 0 carried every city and 24 vehicles (and later the spice nodes and
    // a city's buildings): ~1200 bytes with 9 cities and 24 vehicles, so with a grown planet the
    // guest got no cities, captures or the first 24 vehicles at all. The items are packed in order
    // (cities, spice nodes, one city's buildings, vehicles) into as many chunks as needed.
    constexpr size_t kCityBytes = 25, kNodeBytes = 16, kVehBytes = 41, kHead = 14; // + u32 time (BUG-0122)
    const size_t kBudget = g_chunk_budget; // net::kMaxPacket - 48 (packet + message headers); tests: less
    std::vector<std::pair<net::Vec3, uint32_t>> nodes;
    if (g_cycle % 10 == 0)
        nodes = spice_nodes();
    // the buildings of one city per snapshot (all cities within a second): the guest reconciles
    // what the incremental messages missed (a capture clears the ruins, ...)
    size_t bci = g_host_cities.empty() ? 0xFF : g_cycle % g_host_cities.size();
    std::vector<void*> blds;
    if (bci != 0xFF)
        for (void* bl : vec_of((uint8_t*)g_host_cities[bci] + kCityBuildings))
            if (type_for_noun(game::noun_id(bl)) && blds.size() < 32)
                blds.push_back(bl);
    struct Chunk {
        size_t c0 = 0, c1 = 0, n0 = 0, n1 = 0, v0 = 0, v1 = 0, bytes = kHead;
        bool bld = false;
    };
    std::vector<Chunk> ch(1);
    auto place = [&](size_t sz) -> Chunk& {
        if (ch.back().bytes + sz > kBudget)
            ch.emplace_back();
        ch.back().bytes += sz;
        return ch.back();
    };
    size_t ncity = std::min<size_t>(g_host_cities.size(), 250);
    for (size_t i = 0; i < ncity; i++) {
        Chunk& c = place(kCityBytes);
        if (c.c1 == c.c0)
            c.c0 = i;
        c.c1 = i + 1;
    }
    for (size_t i = 0; i < nodes.size() && i < 250; i++) {
        Chunk& c = place(kNodeBytes);
        if (c.n1 == c.n0)
            c.n0 = i;
        c.n1 = i + 1;
    }
    if (bci != 0xFF)
        place(1 + 17 * blds.size()).bld = true;
    for (size_t i = 0; i < vehicles.size(); i++) {
        Chunk& c = place(kVehBytes);
        if (c.v1 == c.v0)
            c.v0 = i;
        c.v1 = i + 1;
    }
    g_last_chunks = (uint32_t)ch.size();
    for (size_t k = 0; k < ch.size() && k < 255; k++) {
        const Chunk& c = ch[k];
        net::Writer w;
        w.u16(g_cycle);
        w.u8((uint8_t)k);
        w.u8((uint8_t)ch.size());
        w.u8((uint8_t)ncity);
        w.u8((uint8_t)c.c0);
        w.u8((uint8_t)(c.c1 - c.c0));
        for (size_t i = c.c0; i < c.c1; i++) {
            void* city = g_host_cities[i];
            net::Vec3 p = city_pos(city);
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.u32(pol_of(city));
            w.u8(owner_of_city(city));
            w.f32(F<float>(city, kCityCapture)); // military capture progress (the meter over the city)
            w.u32(F<uint32_t>(city, kCityCapture + 4));
        }
        // spice nodes and who owns them (once a second): claims happen on the host only
        w.u8((uint8_t)(c.n1 - c.n0));
        for (size_t i = c.n0; i < c.n1; i++) {
            w.f32(nodes[i].first.x); w.f32(nodes[i].first.y); w.f32(nodes[i].first.z);
            w.u32(nodes[i].second);
        }
        w.u8(c.bld ? (uint8_t)bci : 0xFF);
        if (c.bld) {
            w.u8((uint8_t)blds.size());
            for (void* bl : blds) {
                net::Vec3 bp;
                game::object_position(bl, bp);
                w.u32(game::noun_id(bl));
                w.f32(bp.x); w.f32(bp.y); w.f32(bp.z);
                w.u8(F<int>(bl, kBuildingState) == 2 ? 1 : 0);
            }
        }
        w.u8((uint8_t)(c.v1 - c.v0));
        for (size_t i = c.v0; i < c.v1; i++) {
            void* v = vehicles[i];
            const HV& hv = g_hv[v];
            net::Vec3 p = veh_pos(v);
            net::Quat q = veh_rot(v);
            w.u16(hv.id); w.u8(hv.owner); w.u32(pol_of(v));
            w.u8((uint8_t)F<int>(v, kVPurpose)); w.u8((uint8_t)F<int>(v, kVLoco));
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.f32(q.x); w.f32(q.y); w.f32(q.z); w.f32(q.w);
            w.f32(F<float>(v, kVHp));
        }
        w.u32(net::now_ms()); // BUG-0122: the host's time of the snapshot (velocity on the guest)
        g_max_chunk = std::max<uint32_t>(g_max_chunk, (uint32_t)w.size());
        for (auto& pl : C.sess->players())
            if (pl.id != 0)
                C.sess->send_unreliable_to(pl.id, net::MSG_CIV_STATE, w);
    }
}

static const char* g_buy_fail = "";
static void* host_buy(void* city, int loco)
{
    void* civ = city && g_founder_control && g_founder_nations.contains_faction(pol_of(city))
                    ? F<void*>(city, kCityCiv) : player_civ();
    g_buy_fail = "no such city";
    if (!civ || !city)
        return nullptr;
    int purpose = F<int>(city, kCitySpecialty);
    g_buy_fail = "no vehicle slot left";
    if (thiscall<int>(a_SlotsLeft, civ, purpose) <= 0)
        return nullptr;
    float cost = thiscall<float>(a_VehicleCost, civ, purpose, loco);
    g_buy_fail = "not enough money";
    if (thiscall<float>(a_GetMoney, civ) < cost)
        return nullptr;
    g_buy_fail = "the city cannot build it";
    uint32_t* key = thiscall<uint32_t*>(a_DesignKey, civ, skg::cdecl_call<int>(a_DesignId, purpose, loco));
    if (!key)
        return nullptr;
    void* v = thiscall<void*>(a_SpawnVehicle, city, purpose, loco, key[0], key[1], key[2], 0);
    if (!v)
        return nullptr;
    thiscall<int>(a_PushEvent, v, 10);
    thiscall<void>(a_SpendMoney, civ, cost);
    return v;
}

// build `noun` in `city` at the free slot nearest to `pos` (the game's planner sequence)
static void* build_in_city(void* city, uint32_t noun, const net::Vec3& pos, bool pay)
{
    uint32_t type = type_for_noun(noun);
    if (!type || !city)
        return nullptr;
    if (!building_model_ok(city, type, noun)) // BUG-0469: the building's design model is not in this game
        return nullptr;
    void* o = thiscall<void*>(a_CreateInstance, nm(), noun);
    void* b = o ? game::cast(o, kBuildingType) : nullptr;
    if (!b)
        return nullptr;
    thiscall<void>(a_SetBuildingModel, city, b, type);
    vcall<void>(b, 0x54 / 4, city); // SetOwnerCity
    float p[3] = {pos.x, pos.y, pos.z};
    vcall<void>((uint8_t*)b + 0x34, 0x38 / 4, (const float*)p);
    if (!thiscall<bool>(a_SnapToSlot, city, b, 1))
        return nullptr; // no free slot (left for the game to collect)
    thiscall<int>(a_AddBuilding, city, b, 0);
    if (pay)
        if (void* civ = player_civ())
            thiscall<void>(a_SpendMoney, civ, vcall<float>(b, 0x5C / 4));
    return b;
}

static void* resolve_target(const Target& t)
{
    if (t.kind == 1) {
        auto it = g_hv_by_id.find(t.id);
        return it == g_hv_by_id.end() ? nullptr : it->second;
    }
    if (t.kind == 2)
        return t.id < g_host_cities.size() ? g_host_cities[t.id] : nullptr;
    if (t.kind == 3 && t.noun)
        return game::find_object(t.noun, t.pos, 10.f);
    return nullptr;
}

static void host_command(uint8_t from, net::Reader& r)
{
    uint8_t kind = r.u8();
    auto reject = [&] {
        if (from != C.sess->my_id()) {
            g_cmds_rejected++;
            if (g_cmds_rejected <= 20) // BUG-0325 diagnostics: which command, from whom
                H->log(M, SK_INFO, "civ: rejected command kind %u from player %u", (unsigned)kind, (unsigned)from);
        }
    };
    auto own_vehicle = [&](uint16_t id) -> void* {
        auto it = g_hv_by_id.find(id);
        if (it == g_hv_by_id.end() || g_hv[it->second].owner != from)
            return nullptr;
        return it->second;
    };
    if (kind == 5) { // a guest wants to fire a superweapon: everybody votes
        host_superweapon_request(from, r);
        return;
    }
    if (kind == CMD_GIVE) {
        uint8_t what = r.u8(), to = r.u8(), n = r.u8();
        if (!r.ok() || !net::is_player(*C.sess, to))
            return reject();
        int given = 0;
        for (int i = 0; i < n && r.ok(); i++) {
            uint16_t id = r.u16();
            if (what == 0) {
                if (void* v = own_vehicle(id)) {
                    g_hv[v].owner = to;
                    g_hv[v].gifted = true;
                    given++;
                }
            } else if (id < g_host_cities.size()) {
                void* city = g_host_cities[id];
                if (owner_of_city(city) == from) {
                    g_city_owner[city] = to;
                    g_city_gifted.insert(city);
                    given++;
                }
            }
        }
        g_gifts += given;
        if (given && C.notice)
            C.notice(C.sess->name_of(from) + " gave " +
                     (what == 0 ? std::to_string(given) + (given == 1 ? " vehicle" : " vehicles") : std::string("a city")) +
                     " to " + C.sess->name_of(to));
        if (!given)
            return reject();
        if (from != C.sess->my_id()) // the host's own gifts are not guest commands
            g_cmds_done++;
        return;
    }
    if (kind == CMD_ORDER) {
        uint16_t id = r.u16();
        int type = (int)r.u32(), flag = (int)r.u32();
        Target t = read_target(r);
        void* v = own_vehicle(id);
        void* target = resolve_target(t);
        if (!r.ok() || !v || (t.kind && !target))
            return reject();
        o_SetOrder(v, target, type, flag);
        thiscall<void>(a_OrderStamp, v, 0);
    } else if (kind == CMD_MOVE) {
        uint16_t id = r.u16();
        float x = r.f32(), y = r.f32(), z = r.f32();
        int flag = (int)r.u32();
        void* v = own_vehicle(id);
        if (!r.ok() || !v)
            return reject();
        o_SetMoveOrder(v, x, y, z, flag);
        thiscall<void>(a_OrderStamp, v, 0);
    } else if (kind == CMD_BUY) {
        uint8_t ci = r.u8(), loco = r.u8();
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        uint8_t co = city ? owner_of_city(city) : 0xFF;
        if (!r.ok() || !city || (co != from && co != kShared) || loco > 2)
            return reject();
        g_pending_owner = from;
        void* v = host_buy(city, loco);
        if (v && !g_hv.count(v)) {
            uint16_t id = net::alloc_id(g_next_id, g_hv_by_id); // BUG-0154 (this one could hand out 0)
            g_hv[v] = {id, from, pol_of(v)};
            g_hv_by_id[id] = v;
        }
        g_pending_owner = -1;
        if (!v) {
            // the guest's button was enabled (its own count of vehicles may differ for a moment):
            // tell that player why nothing comes
            net::Writer nw;
            nw.str8(std::string("no vehicle: ") + g_buy_fail);
            C.sess->send_reliable_to(from, net::MSG_NOTICE, nw);
            g_buys_refused++;
            return reject();
        }
    } else if (kind == CMD_BUILD) {
        uint8_t ci = r.u8();
        uint32_t noun = r.u32();
        net::Vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        uint8_t co = city ? owner_of_city(city) : 0xFF;
        if (!r.ok() || !city || (co != from && co != kShared))
            return reject();
        g_build_from = from; // the guest already has it
        void* b = build_in_city(city, noun, p, false); // the guest paid in its planner (shared money)
        g_build_from = -1;
        if (!b)
            return reject();
    } else
        return reject();
    g_cmds_done++;
}

// ---------------- guest ----------------
static void* local_city_near(const net::Vec3& p)
{
    void* best = nullptr;
    float bd = 60.f;
    for (void* c : all_cities()) {
        float d = net::dist(city_pos(c), p);
        if (d < bd) {
            bd = d;
            best = c;
        }
    }
    return best;
}

// the host's building list of a city against the local one: a building missing here is built, one
// the host does not have is removed (a mismatch must be seen on two passes in a row: an incremental
// MSG_CIV_BUILD may be on its way)
static std::unordered_map<void*, int> g_bfix_seen;
// BUG-0324: the host's game rebuilt a building the siege had ruined (a siege that ended without a
// capture); the guest kept the ruin for good: reconcile_buildings only made ruins, never undid one.
static bool g_ruin_repair_fix = true;
COOP_FIX("civ.ruin_repair", "BUG-0324", g_ruin_repair_fix,
         "civ: a guest replaces a ruin the host has whole again with the host's building");
static uint32_t g_bfix_added, g_bfix_removed, g_ruins_differ, g_ruins_in;
static void reconcile_buildings(void* city, size_t n,
                                const std::function<void(size_t, uint32_t&, net::Vec3&, bool&)>& host_b)
{
    auto local = vec_of((uint8_t*)city + kCityBuildings);
    std::vector<bool> used(local.size(), false);
    std::vector<size_t> missing;
    std::vector<void*> ruin_now;
    int ruins = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t noun;
        net::Vec3 p;
        bool ruin;
        host_b(i, noun, p, ruin);
        bool found = false;
        for (size_t k = 0; k < local.size() && !found; k++) {
            net::Vec3 lp;
            if (used[k] || game::noun_id(local[k]) != noun || !game::object_position(local[k], lp) ||
                net::dist(lp, p) > 4.f)
                continue;
            // BUG-0324: a ruin here that the host has whole again (rebuilt after the siege): not this
            // building, so it goes as an extra and the host's comes back as a missing one
            if (g_ruin_repair_fix && !ruin && F<int>(local[k], kBuildingState) == 2 && type_for_noun(noun))
                continue;
            used[k] = found = true;
            if (ruin && F<int>(local[k], kBuildingState) != 2)
                ruin_now.push_back(local[k]);
            ruins += ruin != (F<int>(local[k], kBuildingState) == 2);
        }
        if (!found)
            missing.push_back(i);
    }
    std::vector<void*> extra;
    for (size_t k = 0; k < local.size(); k++)
        if (!used[k] && type_for_noun(game::noun_id(local[k])))
            extra.push_back(local[k]);
    g_ruins_differ = ruins;
    // a building the host's siege destroyed: the same damage here (the generic combatant damage
    // with no attacker: hp 0, state 2; no capture damage or alarm, those come from the host)
    static const float zero[3] = {0, 0, 0};
    for (void* b : ruin_now) {
        g_applying = true;
        thiscall<int>(a_CombatantDamage, (uint8_t*)b + 0x120, F<float>(b, kBuildingState + 4) + 1.f, 0xFFFFFFFFu, 0,
                      (const float*)zero, (void*)nullptr);
        g_applying = false;
        g_ruins_in++;
    }
    int& seen = g_bfix_seen[city];
    if (missing.empty() && extra.empty()) {
        seen = 0;
        return;
    }
    if (++seen < 2)
        return;
    seen = 0;
    g_applying = true;
    for (void* b : extra) {
        thiscall<void>(a_DestroyBuilding, city, b);
        g_bfix_removed++;
    }
    for (size_t i : missing) {
        uint32_t noun;
        net::Vec3 p;
        bool ruin;
        host_b(i, noun, p, ruin);
        if (build_in_city(city, noun, p, false))
            g_bfix_added++;
    }
    g_applying = false;
}

static void kill_local(void* v)
{
    g_applying = true;
    thiscall<void>(a_Die, v, 0, 0xFFFFFFFFu);
    g_applying = false;
}

// BUG-0380: the catch-up founded one city per 15 s, so a guest that missed several foundings (the
// campaign's shortcut founds the remaining cities with instant=1, which is not broadcast; a join)
// lacked those cities, and every vehicle of their nations (no civilization to make the mirror
// for), for up to a minute and a half: sync-check "v: 6 missing on the guest" in every
// campaign_space. The city simulator counts its own foundings at once (+0xB0 sites not founded yet,
// +0xAC all sites, docs/re/civ.md#civ-city-burst): a guest whose foundings (appeared or on their
// way) are fewer than the host's cities founds the difference in one go, and never founds while its
// own foundings are only waiting to appear (a paused game holds them). Off: one per 15 s (0.7.3.3).
static bool g_city_burst_fix = true;
COOP_FIX("civ.city_burst", "BUG-0380", g_city_burst_fix,
         "civ: a guest founds every NPC city it is short of at once (by the game's own founding count), not one per 15 s");
constexpr uintptr_t kExSites = 0xAC, kExSitesLeft = 0xB0; // cCivExpansion: all sites, sites not founded yet
static size_t g_cities_waiting_told;                     // the "founded here, on their way" line, once per count
// the sites this game founded (appeared or still on their way), -1 when the fields look wrong
static int founded_here(void* ex)
{
    if (!ex || !skg::readable(ex, kExSitesLeft + 4))
        return -1;
    int all = F<int>(ex, kExSites), left = F<int>(ex, kExSitesLeft);
    return all > 0 && all <= 64 && left >= 0 && left <= all ? all - left : -1;
}

// a founded city appears a few seconds after SpawnNextCity (docs/re/civ.md 9.3): the count must
// stay short for 6 s, and 15 s must pass after any founding here, so a replayed event is not
// founded a second time; never more cities than the host has
static void city_catch_up(size_t host_total)
{
    if (!g_fix_cities)
        return;
    // BUG-0231: a second guard behind the message gate: a guest that plays its own world (another
    // world of the host's stage) or plays on while the host is away founds nothing for the host
    if (local_play())
        return;
    double now = H->time_seconds();
    size_t have = all_cities().size();
    if (have >= host_total) {
        g_city_short_since = -1;
        g_cities_waiting_told = 0;
        return;
    }
    if (g_city_short_since < 0)
        g_city_short_since = now;
    if (g_city_burst_fix) {
        void* ex = skg::cdecl_call<void*>(a_ExpansionGet);
        int founded = founded_here(ex);
        // 2 s short first: a founding of the host whose broadcast is on its way is not made twice
        if (founded >= 0 && now - g_city_short_since < 2.0)
            return;
        if (founded >= 0 && (size_t)founded >= host_total) { // founded here: they appear by themselves
            if (g_cities_waiting_told != host_total)
                H->log(M, SK_INFO, "civ: the host has %u cities, this game %u and %d founded (on their way)",
                       (unsigned)host_total, (unsigned)have, founded);
            g_cities_waiting_told = host_total;
            return;
        }
        if (founded >= 0) {
            int n = (int)host_total - founded, made = 0;
            g_applying = true;
            for (int i = 0; i < n; i++)
                if (o_SpawnNextCity(ex, 0, nullptr))
                    made++;
                else
                    break; // no free site left
            g_applying = false;
            g_last_city_spawn = now;
            g_city_short_since = -1;
            g_cities_caught_up += made;
            H->log(M, SK_INFO, "civ: the host has %u cities, this game %u (%d founded): founding %d of %d at once",
                   (unsigned)host_total, (unsigned)have, founded, made, n);
            return;
        }
        // fields that look wrong: the old pace below
    }
    if (now - g_city_short_since < 6.0 || now - g_last_city_spawn < 15.0)
        return;
    void* ex = skg::cdecl_call<void*>(a_ExpansionGet);
    if (!ex)
        return;
    g_applying = true;
    o_SpawnNextCity(ex, 0, nullptr);
    g_applying = false;
    g_last_city_spawn = now;
    g_city_short_since = -1;
    g_cities_caught_up++;
    H->log(M, SK_INFO, "civ: the host has %u cities, this game %u: founding the next site", (unsigned)host_total,
           (unsigned)all_cities().size());
}

static bool guest_cities(net::Reader& r) // false: the list does not fit the message
{
    uint8_t total = r.u8(), c0 = r.u8(), nc = r.u8();
    if (!r.ok() || c0 + nc > total)
        return false;
    if (c0 == 0)
        city_catch_up(total);
    if (g_host_cities.size() != total)
        g_host_cities.resize(total, nullptr);
    for (int i = 0; i < nc && r.ok(); i++) {
        net::Vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        uint32_t pol = r.u32();
        uint8_t owner = r.u8();
        float capture = r.f32();
        uint32_t capture_by = r.u32();
        if (!r.ok())
            return false;
        void* c = local_city_near(p);
        g_host_cities[c0 + i] = c;
        if (!c)
            continue;
        if (owner != 0xFE && owner != 0xFF)
            g_city_owner[c] = owner; // kShared included
        else
            g_city_owner.erase(c);
        if (pol_of(c) != pol) { // captured on the host
            g_applying = true;
            thiscall<void>(a_ChangeOwner, c, pol, 0, 0);
            g_applying = false;
            g_captures_in++;
        }
        // the siege meter: the guest's city is never hit (its mirrors do not shoot), so its own
        // progress stays 0 (and decays): show the host's (SetCaptureProgress updates the meter)
        if (std::isfinite(capture) && (std::fabs(F<float>(c, kCityCapture) - capture) > 0.5f ||
                                       F<uint32_t>(c, kCityCapture + 4) != capture_by)) {
            g_applying = true;
            thiscall<void>(a_SetCapture, c, capture_by, capture);
            g_applying = false;
        }
    }
    return true;
}

static void guest_nodes_and_buildings(net::Reader& r)
{
    uint8_t nn = r.u8(); // spice node owners (a guest's derricks appear where the host's vehicles claimed)
    for (int i = 0; i < nn && r.ok(); i++) {
        net::Vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        uint32_t pol = r.u32();
        if (!r.ok())
            return;
        void* node = game::find_object(kNodeNoun, p, 5.f);
        if (!node || pol_of(node) == pol)
            continue;
        g_applying = true;
        if (pol == 0xFFFFFFFFu)
            thiscall<void>(a_NodeUnclaim, node);
        else
            thiscall<void>(a_NodeClaim, node, pol);
        g_applying = false;
        g_nodes_in++;
    }
    uint8_t ci = r.u8();
    if (!r.ok() || ci == 0xFF)
        return;
    struct HB {
        uint32_t noun;
        net::Vec3 p;
        bool ruin;
    };
    std::vector<HB> hb(r.u8());
    for (auto& b : hb) {
        b.noun = r.u32();
        b.p.x = r.f32(); b.p.y = r.f32(); b.p.z = r.f32();
        b.ruin = r.u8() != 0;
    }
    void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
    if (r.ok() && city)
        reconcile_buildings(city, hb.size(), [&](size_t i, uint32_t& noun, net::Vec3& p, bool& ruin) {
            noun = hb[i].noun; p = hb[i].p; ruin = hb[i].ruin;
        });
}

// BUG-0469: the design key a mirror vehicle of that purpose and locomotion is built with, and the
// vehicle is created from that key (CreateVehicle reads the model of the design: cVehicle::Load
// 0xCA67C0). The civ's own design for the purpose and locomotion is checked with the parts and paints
// of its model (creations::ensure_model):
//  - the design this game has: the key itself;
//  - a part or paint this game does not have, with a substitute: our copy of the model (under the
//    design's own key, above the game's packages: the mirror gets the similar base game parts and no
//    key of ours reaches the save);
//  - one that cannot be built here at all: a zero key, the game's own design of the same purpose and
//    locomotion (cVehicle::Load generates one from the object templates for 0 and -1), logged;
//  - false: the civilization has no design for that purpose and locomotion here (no mirror, 0.7.3.5).
// `coop.fix content.all_kinds off`: the design key as it is (0.7.3.5).
static unsigned g_veh_design_checked, g_veh_design_fallback;
static std::unordered_set<uint32_t> g_veh_design_logged;
static bool g_veh_force; // tests (BUG-0469): mirrors are built with this design key instead
static uint32_t g_veh_force_key[3];

static bool mirror_design(void* civ, int purpose, int loco, uint32_t key[3])
{
    if (!civ)
        return false; // no civilization of that political id here: no mirror (as 0.7.3.5)
    if (g_veh_force) { // tests (BUG-0469): a design key the guest builds in the mirror path
        key[0] = g_veh_force_key[0];
        key[1] = g_veh_force_key[1];
        key[2] = g_veh_force_key[2];
    } else {
        uint32_t* k = thiscall<uint32_t*>(a_DesignKey, civ, skg::cdecl_call<int>(a_DesignId, purpose, loco));
        if (!k)
            return false;
        key[0] = k[0];
        key[1] = k[1];
        key[2] = k[2];
    }
    if (!key[0])
        return true; // the game's own design for that purpose and locomotion (it generates one)
    creations::Model m = creations::ensure_model({key[0], key[1], key[2]}, creations::kMkVehicle);
    if (m.key.instance) {
        g_veh_design_checked++;
        key[0] = m.key.instance;
        key[1] = m.key.type;
        key[2] = m.key.group;
        return true;
    }
    if (g_veh_design_logged.insert(key[0]).second)
        H->log(M, SK_INFO, "civ: the model of the vehicle design %08X cannot be built here: the mirror takes the "
               "game's own design of that purpose and locomotion (BUG-0469)", key[0]);
    key[0] = key[1] = key[2] = 0;
    g_veh_design_fallback++;
    return true;
}

static void guest_state(net::Reader& r)
{
    uint16_t cycle = r.u16();
    uint8_t chunk = r.u8(), chunks = r.u8();
    if (!r.ok() || !net::cycle_chunk_ok(chunk, chunks))
        return;
    if (cycle != g_rx_cycle) {
        g_rx_cycle = cycle;
        g_cycle_ids.clear();
        g_cycle_chunks.clear();
    }
    if (!guest_cities(r))
        return;
    guest_nodes_and_buildings(r);
    double now = H->time_seconds();
    uint8_t n = r.u8();
    double sent = net::snapshot_time(r, (size_t)n * 41); // 41 bytes per vehicle
    for (int i = 0; i < n && r.ok(); i++) {
        uint16_t id = r.u16();
        GV m;
        m.owner = r.u8(); m.pol = r.u32(); m.purpose = r.u8(); m.loco = r.u8();
        m.pos.x = r.f32(); m.pos.y = r.f32(); m.pos.z = r.f32();
        m.rot.x = r.f32(); m.rot.y = r.f32(); m.rot.z = r.f32(); m.rot.w = r.f32();
        m.hp = r.f32();
        if (!r.ok())
            return;
        g_cycle_ids.insert(id);
        auto& g = g_gv[id];
        void* v = g.v;
        g.owner = m.owner; g.pol = m.pol; g.purpose = m.purpose; g.loco = m.loco;
        g.pos = m.pos; g.rot = m.rot; g.hp = m.hp; g.seen = now;
        // BUG-0186: a step faster than twice the vehicle's own top speed is a jump of the host's game
        bool alive_v = v && game::object_alive(v) && !F<uint8_t>(v, kVDead);
        g.est.max_speed = g_vel_cap_fix && alive_v ? 2.f * F<float>(v, 0x218) + 10.f : 0.f;
        g.est.max_ahead = g_extrap_fix ? kVehAhead : 0.4;
        g.est.update(m.pos, now, sent);
        if (v && (!game::object_alive(v) || F<uint8_t>(v, kVDead))) {
            g_gv_of.erase(v);
            g.v = v = nullptr;
            g.listed = false;
        }
        if (v)
            continue;
        // bind a local vehicle of the same kind nearby, else create one for that civilization (with a
        // design key the tests force (BUG-0469): always create, so the creation path is reached)
        float best = 25.f;
        for (void* lv : all_vehicles()) {
            if (g_veh_force || g_gv_of.count(lv) || pol_of(lv) != m.pol || F<int>(lv, kVPurpose) != m.purpose ||
                F<int>(lv, kVLoco) != m.loco)
                continue;
            float d = net::dist(veh_pos(lv), m.pos);
            if (d < best) {
                best = d;
                v = lv;
            }
        }
        if (!v) {
            void* civ = thiscall<void*>(a_CivByPol, nm(), m.pol);
            uint32_t key[3] = {0, 0, 0};
            if (mirror_design(civ, (int)m.purpose, (int)m.loco, key)) { // BUG-0469
                float p[3] = {m.pos.x, m.pos.y, m.pos.z};
                v = thiscall<void*>(a_CreateVehicle, civ, (int)m.purpose, (int)m.loco, key[0], key[1], key[2],
                                    (const float*)p, 0);
                if (v)
                    g_created++;
            }
        }
        if (!v)
            continue;
        g.v = v;
        g.listed = false;
        g_gv_of[v] = id;
    }
    if (!r.ok()) // a cut chunk does not count: the cycle stays incomplete, nothing is removed
        return;
    g_cycle_chunks.insert(chunk);
    if (chunk + 1 < chunks)
        return;
    // only a complete cycle tells which vehicles are gone: with a chunk lost (or late) every
    // vehicle of it was killed and made again on the next cycle
    if (g_cycle_chunks.size() < chunks) {
        g_incomplete++;
        return;
    }
    for (auto it = g_gv.begin(); it != g_gv.end();) {
        if (!g_cycle_ids.count(it->first)) {
            if (it->second.v && game::object_alive(it->second.v) && !F<uint8_t>(it->second.v, kVDead)) {
                kill_local(it->second.v);
                g_removed++;
            }
            g_gv_of.erase(it->second.v);
            it = g_gv.erase(it);
        } else
            ++it;
    }
    // the guest's own vehicles that the host does not have (its local civ AI, the start vehicle)
    for (void* lv : all_vehicles())
        if (!g_gv_of.count(lv)) {
            kill_local(lv);
            g_removed++;
            g_removed_local++; // tests: a vehicle of this game's own, not a mirror the host no longer has
        }
}

static void guest_tick(double now)
{
    static double last_sel;
    if (now - last_sel > 0.2) {
        last_sel = now;
        drop_foreign_selection();
    }
    if (local_play()) {
        for (auto& [id, g] : g_gv)
            g.has_put = false;
        return; // the last states would stop every order for 3 s (StopMovement below)
    }
    for (auto& [id, g] : g_gv) {
        if (!g.v || !game::object_alive(g.v) || now - g.seen > 3.0) {
            g.has_put = false;
            continue;
        }
        net::Vec3 cur = veh_pos(g.v);
        net::Vec3 want = g.est.at(now); // moved on with the estimated velocity (no lag behind the host)
        float d = net::dist(cur, want);
        if (g.has_put) {
            g.pushed = net::dist(cur, g.put);
            if (g.pushed > g.pushed_max || now - g.pushed_at > 5.0) {
                g.pushed_max = g.pushed;
                g.pushed_at = now;
            }
        }
        g.err = d;
        net::Vec3 p = want;
        if (d < 30.f && d > 0.01f) { // glide out small corrections
            float k = 0.5f;
            p = {cur.x + (want.x - cur.x) * k, cur.y + (want.y - cur.y) * k, cur.z + (want.z - cur.z) * k};
        }
        void* sp = (uint8_t*)g.v + kVSpatial;
        float pp[3] = {p.x, p.y, p.z}, q[4] = {g.rot.x, g.rot.y, g.rot.z, g.rot.w};
        vcall<void>(sp, 0xEC / 4); // StopMovement: the locomotion does not pull it back
        vcall<void>(sp, 0x44 / 4, (const float*)pp, (const float*)q); // Teleport
        F<float>(g.v, kVHp) = g.hp;
        g.put = p;
        g.has_put = true;
    }
}

static void guest_build(net::Reader& r)
{
    uint8_t ci = r.u8();
    uint32_t noun = r.u32();
    net::Vec3 p;
    p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
    if (r.ok() && ci == kNextCitySite && noun == 0) { // the host's planet grew a new city: the same site here
        if (void* ex = skg::cdecl_call<void*>(a_ExpansionGet)) {
            g_applying = true;
            bool ok = o_SpawnNextCity(ex, 0, nullptr);
            g_applying = false;
            g_cities_in++;
            H->log(M, SK_INFO, "civ: the host founded a city: here %s (cities now %u)", ok ? "founded" : "NOT founded",
                   (unsigned)all_cities().size());
            g_last_city_spawn = H->time_seconds();
        }
        return;
    }
    void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
    if (!r.ok() || !city)
        return;
    g_applying = true;
    if (build_in_city(city, noun, p, false))
        g_builds_in++;
    g_applying = false;
}

static void prune_gone()
{
    // guests only: the host's map is rebuilt from the vehicle list in host_tick, and a vehicle it has
    // just bought for a guest is not in the list yet (dropping it lost the guest's ownership)
    for (auto& [id, g] : g_gv) {
        if (!g.v)
            continue;
        if (exists(g.v))
            g.listed = true;
        else if (g.listed) {
            g_gv_of.erase(g.v);
            g.v = nullptr;
            g.listed = false;
        }
    }
}

void tick(double now)
{
    if (!active())
        return;
    if (is_guest() && !g_founder_nations.guest().faction && now >= g_founder_poll) {
        g_founder_poll = now + 2;
        founders::read(world::current_game_key(), g_founder_nations, founders::Stage::Civilization);
    }
    founder_tick(now);
    if (is_host() && g_founder_nations.guest().faction && g_offline_planet != g_founder_planet) {
        g_offline_planet = g_founder_planet;
        g_offline_control = founders::offline_control(g_offline_planet, g_founder_nations);
        g_command_refresh = true;
    }
    campaign_tick(now);
    prune_gone();
    if (is_host())
        host_tick(now);
    else if (is_guest())
        guest_tick(now);
}

static uint32_t g_founds_outside; // BUG-0319 diagnostics: city foundings received outside the Civ mode
void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (!active()) {
        if (type == net::MSG_CIV_BUILD && g_ok && is_guest()) {
            net::Reader peek = r;
            uint8_t ci = peek.u8();
            uint32_t noun = peek.u32();
            if (peek.ok() && ci == kNextCitySite && noun == 0) {
                g_founds_outside++;
                H->log(M, SK_INFO, "civ: the host founded a city while this game is outside the Civ mode (mode %08X)",
                       game::mode_id());
            }
        }
        return;
    }
    prune_gone();
    if (type == net::MSG_CIV_STATE && is_guest())
        guest_state(r);
    else if (type == net::MSG_CIV_CMD && is_host())
        host_command(from, r);
    else if (type == net::MSG_CIV_BUILD && is_guest())
        guest_build(r);
    else if (type == net::MSG_CIV_ALARM && is_guest()) {
        uint8_t ci = r.u8(), kind = r.u8(), owner = r.u8();
        if (r.ok() && ci < g_host_cities.size())
            show_alarm(g_host_cities[ci], kind, owner);
    }
}

// the tables keyed by game pointers that only the co-op mechanics use (BUG-0040, BUG-0059): a new
// session or world must not inherit them (a city or vehicle at a reused address)
static void clear_coop_tables(bool restore_speed)
{
    if (restore_speed && g_ok && game::mode_id() == modes::kCiv && nm())
        for (auto& [v, b] : g_boost)
            if (exists(v))
                F<float>(v, 0x218) = b.orig;
    g_boost.clear();
    g_alarm_at.clear();
    g_siege.clear();
    g_joint_told.clear();
    g_city_gifted.clear();
    g_bfix_seen.clear();
    g_ui_city = nullptr;
}

void on_session_end()
{
    g_hud_samples = 0;
    ++g_founder_epoch;
    g_founder_queued = g_founder_spawned = false;
    g_founder_poll = 0;
    g_founder_planet.clear();
    g_founder_notice.clear();
    g_offline_planet.clear();
    g_offline_control = false;
    g_command_refresh = false;
    g_founder_nations = {};
    g_progress_queued = false;
    g_progress_poll = g_campaign_share_at = 0;
    if (o_TerrainCursor && game::mode_id() == modes::kCiv) h_TerrainCursor();
    caption_disarm(); // BUG-0224
    if (g_fix_boost)
        clear_coop_tables(true);
    g_hv.clear();
    g_hv_by_id.clear();
    g_gv.clear();
    g_gv_of.clear();
    g_city_owner.clear();
    g_host_cities.clear();
    g_split_for.clear();
    g_city_short_since = -1;
}

// a load (or a stage change): every vehicle and city of the old world is freed. Before BUG-0059 civ
// had no such reset: the host kept the old vehicle table (a new vehicle at a reused address kept an
// old id and a player owner, even an NPC nation's one) and the guest the old host-city table.
void on_world_change()
{
    if (!g_fix_world)
        return;
    // the vehicle / building editors have no co-op adapter either (coop.cpp sees "no local" as a
    // world change): the world stays, and the owners must not be lost with it
    uint32_t m = game::mode_id();
    if (m == modes::kEdit || m == modes::kEditor)
        return;
    g_hud_samples = 0;
    ++g_founder_epoch;
    g_founder_queued = g_founder_spawned = false;
    g_founder_poll = 0;
    g_founder_planet.clear();
    g_founder_nations = {};
    g_offline_planet.clear();
    g_offline_control = false;
    g_command_refresh = false;
    g_progress_queued = false;
    g_progress_poll = g_campaign_share_at = 0;
    g_world_resets++;
    caption_disarm();
    clear_coop_tables(false); // nothing to restore: the vehicles are gone
    // BUG-0223: the tables are forgotten here, not through on_session_end (its restoring clear
    // would write +0x218 into the old world's vehicles if the order ever changed)
    g_hv.clear();
    g_hv_by_id.clear();
    g_gv.clear();
    g_gv_of.clear();
    g_city_owner.clear();
    g_host_cities.clear();
    g_split_for.clear();
    g_city_short_since = -1;
    g_cities_waiting_told = 0;
    g_rx_cycle = 0xFFFF;
    g_cycle_ids.clear();
    g_cycle_chunks.clear();
    g_exist_frame = ~0ull;
}

// BUG-0223: which Civ world is open. Before, a single frame without the stage's local entity (an
// anchor built from the player civ's first city) counted as a world change: the host dropped the
// vehicle table (every vehicle a new id, rebuilt on the guests), every city's owner (captures and
// gifts) and the boosts while the world was still there. Now: the stage counter
// (game::stage_epoch: a load, a received world, another stage), leaving Civ for a mode that is not
// an editor, and the in-stage signals (another player civilization object, another save name).
// Limit: a reload of the same save that reuses the player civ's address and never shows a
// non-stage frame is not seen. `coop.fix civ.world_epoch off`: the old trigger (coop.cpp
// avatar_change_check, with the editor check above).
static bool g_fix_world_epoch = true;
COOP_FIX("civ.world_epoch", "BUG-0223", g_fix_world_epoch, "civ: only a real world change (not a frame without the city anchor) clears the tables");
static stage_epoch::Watch g_world_watch;
static bool g_world_left = true;
static uint32_t g_world_checks_fired;

bool world_epoch_fix() { return g_fix_world_epoch; }

void world_check()
{
    if (!g_fix_world_epoch) {
        g_world_watch = {};
        g_world_left = true;
        return;
    }
    uint32_t m = game::mode_id();
    bool in = m == modes::kCiv;
    bool editor = m == modes::kEdit || m == modes::kEditor;
    void* pc = in && g_ok && nm() ? player_civ() : nullptr;
    bool ch = stage_epoch::changed(g_world_watch, game::stage_epoch(modes::kCiv), in, pc, in ? modes::current_world() : 0);
    bool fire = false;
    if (!in && !editor) {
        fire = !g_world_left;
        g_world_left = true;
    } else if (in) {
        fire = ch && !g_world_left;
        g_world_left = false;
    }
    if (fire) {
        g_world_checks_fired++;
        if (g_ok)
            H->log(M, SK_INFO, "civ: the world changed (mode %08X, epoch %u)", m, game::stage_epoch(modes::kCiv));
        on_world_change();
    }
}

void for_each_owned(void (*fn)(void* obj, const net::Vec3& pos, uint8_t owner, bool city, void* user), void* user)
{
    if (!active())
        return;
    for (auto& [c, o] : g_city_owner)
        if (c && game::object_alive(c) && o != kShared)
            fn(c, city_pos(c), o, true, user);
    if (is_host()) {
        for (auto& [v, hv] : g_hv)
            if (hv.owner != 0xFE)
                fn(v, veh_pos(v), hv.owner, false, user);
    } else {
        for (auto& [id, g] : g_gv)
            if (g.v && g.owner != 0xFE)
                fn(g.v, veh_pos(g.v), g.owner, false, user);
    }
}

std::string status()
{
    char b[768];
    if (C.sess && C.sess->role() == net::Role::Host)
        snprintf(b, sizeof b,
                 "civ(host): vehicles=%u cities=%u owned_cities=%u cmds_done=%u rejected=%u builds_sent=%u gifts=%u "
                 "trade_pairs=%d trade_total=%d joint_hits=%u military_hits=%u religious=%u alarms=%u votes=%u boosts=%u "
                 "cities_founded=%u unselected=%u movie=%d buys_refused=%u chunks=%u max_chunk=%u\n",
                 (unsigned)g_hv.size(), (unsigned)g_host_cities.size(), (unsigned)g_city_owner.size(), g_cmds_done,
                 g_cmds_rejected, g_builds_sent, g_gifts, g_trade_pairs, g_trade_total, g_joint_hits, g_military_hits,
                 g_religious_boosts, g_alarms, g_votes, (unsigned)g_boost.size(), g_cities_founded, g_unselected, (int)movie_playing(), g_buys_refused, g_last_chunks,
                 g_max_chunk);
    else
        snprintf(b, sizeof b,
                 "civ(guest): mirrors=%u created=%u removed=%u cities=%u cmds_sent=%u builds_in=%u captures_in=%u "
                 "orders_dropped=%u growth_blocked=%u nodes_in=%u cities_in=%u cities_blocked=%u unselected=%u movie=%d "
                 "buildings_fixed=+%u/-%u ruins_in=%u ruins_differ=%u incomplete=%u income_blocked=%u cities_caught_up=%u ai_spawns_blocked=%u away_orders=%u local=%d founds_outside=%u mirror_place_skipped=%u removed_local=%u\n",
                 (unsigned)g_gv.size(), g_created, g_removed, (unsigned)g_host_cities.size(), g_cmds_sent, g_builds_in,
                 g_captures_in, g_orders_dropped, g_growth_blocked, g_nodes_in, g_cities_in, g_cities_blocked, g_unselected, (int)movie_playing(), g_bfix_added,
                 g_bfix_removed, g_ruins_in, g_ruins_differ, g_incomplete, g_income_blocked, g_cities_caught_up, g_ai_spawns_blocked, g_away_orders, (int)local_play(), g_founds_outside, g_mirror_place_skipped, g_removed_local);
    return b;
}

void for_each_target(void (*fn)(const char* kind, int id, int owner, const net::Vec3& pos, void* user), void* user)
{
    if (!active())
        return;
    for (size_t i = 0; i < g_host_cities.size(); i++)
        if (void* c = g_host_cities[i]) {
            uint8_t o = owner_of_city(c);
            fn("city", (int)i, o == 0xFF ? -1 : (int)o, city_pos(c), user);
        }
    if (is_host()) {
        for (auto& [v, hv] : g_hv)
            if (hv.owner != 0xFE && game::object_alive(v))
                fn("vehicle", hv.id, hv.owner, veh_pos(v), user);
    } else {
        for (auto& [id, g] : g_gv)
            if (g.v && g.owner != 0xFE && game::object_alive(g.v))
                fn("vehicle", id, g.owner, veh_pos(g.v), user);
    }
}

std::string list()
{
    std::string out;
    char b[300];
    if (!g_ok || !nm())
        return out;
    void* pc = player_civ();
    auto cities = all_cities();
    for (size_t i = 0; i < cities.size(); i++) {
        void* c = cities[i];
        net::Vec3 p = city_pos(c);
        uint8_t o = owner_of_city(c);
        auto blds = vec_of((uint8_t*)c + 0x340);
        int ruins = 0; // buildings destroyed (combatant +0x120, state +0x34 == 2; 0x8E8220)
        float hp = 0;
        for (void* bl : blds) {
            ruins += F<int>(bl, 0x120 + 0x34) == 2;
            hp += F<float>(bl, 0x120 + 0x38);
        }
        snprintf(b, sizeof b,
                 "city %d %p pol=%08X%s owner=%d buildings=%u pos=(%.0f %.0f %.0f) capture=%.0f/%08X convert=%.0f buy=%.0f "
                 "ruins=%d hp=%.0f\n",
                 host_index_of_city(c), c, pol_of(c), pc && pol_of(c) == pol_of(pc) ? " (player)" : "",
                 o == 0xFF ? -1 : (int)o, (unsigned)blds.size(), p.x, p.y, p.z, F<float>(c, 0x748),
                 F<uint32_t>(c, 0x74C), F<float>(c, 0x750), F<float>(c, 0x758), ruins, hp);
        out += b;
    }
    for (void* v : all_vehicles()) {
        net::Vec3 p = veh_pos(v);
        uint16_t id = 0;
        if (C.sess && C.sess->role() == net::Role::Host) {
            auto it = g_hv.find(v);
            id = it == g_hv.end() ? 0 : it->second.id;
        } else {
            auto it = g_gv_of.find(v);
            id = it == g_gv_of.end() ? 0 : it->second;
        }
        uint8_t o = owner_of_vehicle(v);
        // the current order (mOrders +0xB68, element {target, pos, type, issuer}): type, -1 none
        uint8_t* ob = F<uint8_t*>(v, kVOrders);
        uint8_t* oe = F<uint8_t*>(v, kVOrders + 4);
        int otype = ob && oe > ob ? *(int*)(ob + 0x10) : -1;
        // the order's own place (element +4, Vector3; BUG-0222: tests check whose order the game took)
        float ox = otype >= 0 ? *(float*)(ob + 4) : 0, oy = otype >= 0 ? *(float*)(ob + 8) : 0,
              oz = otype >= 0 ? *(float*)(ob + 12) : 0;
        snprintf(b, sizeof b,
                 "  veh #%u %p pol=%08X owner=%d purpose=%d loco=%d sel=%u hp=%.0f pos=(%.1f %.1f %.1f) order=%d "
                 "opos=(%.1f %.1f %.1f)\n",
                 id, v, pol_of(v), o == 0xFF ? -1 : (int)o, F<int>(v, kVPurpose), F<int>(v, kVLoco),
                 F<uint8_t>(v, kVSelected), F<float>(v, kVHp), p.x, p.y, p.z, otype, ox, oy, oz);
        out += b;
        auto gi = id && !(C.sess && C.sess->role() == net::Role::Host) ? g_gv.find(id) : g_gv.end();
        if (gi != g_gv.end()) { // BUG-0186: the guest's view of the mirror (on the same line)
            out.pop_back();
            const net::Vec3& ev = gi->second.est.vel;
            snprintf(b, sizeof b, " age=%.1f err=%.1f pushed=%.1f pushed5s=%.1f vel=%.1f top=%.1f jumps=%u\n",
                     H->time_seconds() - gi->second.seen, gi->second.err, gi->second.pushed, gi->second.pushed_max,
                     std::sqrt(ev.x * ev.x + ev.y * ev.y + ev.z * ev.z), F<float>(v, 0x218), gi->second.est.capped);
            out += b;
        }
    }
    return out;
}

// coop.census (census.h): every vehicle with its net id, owner (player id, 254 an NPC civ's) and
// position; key = the political id of its civilization
void census_rows(std::vector<census::Row>& out)
{
    if (!g_ok || !nm())
        return;
    bool host = C.sess && C.sess->role() == net::Role::Host;
    for (void* v : all_vehicles()) {
        census::Row r;
        r.kind = 'v';
        if (host) {
            auto it = g_hv.find(v);
            r.id = it == g_hv.end() ? -1 : it->second.id;
        } else {
            auto it = g_gv_of.find(v);
            r.id = it == g_gv_of.end() ? -1 : it->second;
        }
        r.own = r.id < 0 ? "local" : host ? "host" : "mirror";
        uint8_t o = owner_of_vehicle(v);
        r.owner = o == 0xFF ? -1 : o;
        r.key = pol_of(v);
        r.alive = F<uint8_t>(v, kVDead) ? 0 : 1;
        r.hp = F<float>(v, kVHp);
        r.pos = veh_pos(v);
        out.push_back(r);
    }
}

// ---- debug (tests): the same hooked paths the UI uses ----
static void* local_vehicle(uint16_t id)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        auto it = g_hv_by_id.find(id);
        return it == g_hv_by_id.end() ? nullptr : it->second;
    }
    auto it = g_gv.find(id);
    return it == g_gv.end() ? nullptr : it->second.v;
}

bool offline_control_available() { return active() && is_host() && g_founder_nations.guest().faction && g_offline_selection_ready; }
bool offline_control_enabled() { return g_offline_control; }

bool founder_space_ready()
{
    if (!active() || !g_founder_nations.guest().faction) return false;
    static uintptr_t ready = H->sym("SporeCoop::CivMode_SpaceReady");
    return ready && skg::cdecl_call<bool>(ready);
}

std::string debug(const std::vector<std::string>& a)
{
    if (!a.empty() && a[0] == "offline_control") {
        if (!active() || !is_host() || !g_founder_nations.guest().faction)
            return "Open the founder campaign on the host to change offline control.";
        if (a.size() == 2 && (a[1] == "on" || a[1] == "off")) {
            const bool enabled = a[1] == "on";
            if (enabled && !g_offline_selection_ready) return "Offline selection is unavailable on this game build.";
            if (!founders::set_offline_control(world::current_game_key(), g_founder_nations, enabled))
                return "Could not save offline control for this campaign.";
            g_offline_control = enabled;
            g_command_refresh = true;
        } else if (a.size() != 1) return "Use offline_control on or off.";
        return g_offline_control ? "Offline partner control: on" : "Offline partner control: off";
    }
    if (a.size() == 2 && a[0] == "bind_guest") {
        if (!active() || !is_host() || C.sess->players().size() != 2 || !player_civ())
            return "Bind a nation on the Civilization host with one guest present.";
        char* end = nullptr;
        unsigned long long raw = strtoull(a[1].c_str(), &end, 16);
        if (!raw || raw > UINT32_MAX || !end || *end || raw == pol_of(player_civ())) return "Choose a distinct non-player nation from coop civ nations.";
        uint8_t guest = 0;
        for (const auto& p : C.sess->players()) if (p.id) guest = p.id;
        auto key = creations::player_key(guest, creations::kCreature);
        auto home_key = F<creations::Key>(player_civ(), kNationSpecies);
        if (!guest || !key.instance || !home_key.instance || key == home_key || !creations::player_species(guest))
            return "Waiting for a distinct, loaded guest species.";
        std::string planet = world::current_game_key(), err;
        if (planet.empty()) return "Save and name the planet before binding a nation.";
        void* nation = nullptr;
        for (void* city : all_cities()) {
            void* country = F<void*>(city, kCityCiv);
            if (!country || country == player_civ() || pol_of(country) != raw) continue;
            if (pol_of(city) != raw || (nation && nation != country)) return "City ownership is changing. Try again once it settles.";
            nation = country;
            auto city_key = F<creations::Key>(city, kCitySpecies);
            if (city_key.instance && !(city_key == key)) return "This city already uses another species.";
        }
        if (!nation) return "No live capital or city belongs to that nation.";
        auto& saved = F<creations::Key>(nation, kNationSpecies);
        if (saved.instance && !(saved == key)) return "This nation already has a different species.";
        founders::Roster pair{home_key.instance, key.instance, pol_of(player_civ()), pol_of(nation)};
        auto stage = founders::Stage::Civilization;
        if (!founders::can_record(planet, pair, err, stage) || !creations::archive_guest_model(guest, planet, err) ||
            !founders::record(planet, pair, err, stage)) return err;
        saved = key; // the native save owns this field. Spore derives live citizens from it as the city enters view.
        if (!world::share_with(guest, err, true)) return "Nation assigned; could not queue the save transfer. Retry the binding. " + err;
        return "Guest species assigned to the nation. Saving and sharing the planet.";
    }
    if (a.size() == 1 && a[0] == "citizens") {
        if (!g_ok || game::mode_id() != modes::kCiv || !nm()) return "Open a Civilization planet first.";
        struct Census {
            std::vector<void*> cities;
            std::map<std::tuple<uint32_t, uint32_t, uint32_t>, unsigned> groups;
        } census{all_cities(), {}};
        game::for_each_object([](void* obj, uint32_t noun, void* data) {
            auto& census = *static_cast<Census*>(data);
            if (noun != 0x018EB4B7 || !game::object_alive(obj)) return true;
            void* city = F<void*>(obj, 0x1010); // cCreatureCitizen::mpOwnerCity
            bool known = std::find(census.cities.begin(), census.cities.end(), city) != census.cities.end();
            ++census.groups[{known ? pol_of(city) : 0, pol_of(obj), game::species_key(obj).instance}];
            return true;
        }, &census);
        std::string result;
        for (auto& [keys, count] : census.groups) {
            char row[100];
            auto [city_pol, citizen_pol, species] = keys;
            snprintf(row, sizeof row, "citizens city_pol=%08X citizen_pol=%08X species=%08X count=%u\n",
                     city_pol, citizen_pol, species, count);
            result += row;
        }
        return result.empty() ? "No live citizen objects in this view." : result;
    }
    if (a.size() == 1 && a[0] == "campaign") {
        if (!active()) return "Open a connected Civilization campaign first.";
        std::string result;
        for (void* country : vec_of(thiscall<void*>(a_GetAllCivs, nm()))) {
            if (!g_founder_nations.contains_faction(pol_of(country))) continue;
            char row[256];
            snprintf(row, sizeof row, "campaign pol=%08X cities=%u team=%u tech=%d highest=%d captures=%d,%d,%d unlocks=",
                     pol_of(country), (unsigned)vec_of((uint8_t*)country + kCivCities).size(),
                     (unsigned)team_cities(country).size(), F<int>(country, 0x298), F<int>(country, 0x4D0),
                     F<int>(country, 0x4C0), F<int>(country, 0x4C4), F<int>(country, 0x4C8));
            result += row;
            for (unsigned i = 0; i < 12; ++i) result += F<uint8_t>(country, 0x484 + i) ? '1' : '0';
            result += '\n';
        }
        return result;
    }
    if (a.size() == 1 && a[0] == "hud_progress") {
        if (!active() || !a_FinalCityCount || !o_HudProgress) return "No active Civilization HUD hook.";
        char row[160];
        snprintf(row, sizeof row, "samples=%u home=%.6f displayed=%.6f final_cities=%d",
                 g_hud_samples, g_solo_fraction, g_hud_fraction, skg::cdecl_call<int>(a_FinalCityCount));
        return row;
    }
    if (a.size() == 1 && a[0] == "nations") {
        if (!g_ok || game::mode_id() != modes::kCiv || !nm()) return "Open a Civilization planet first.";
        std::string result;
        void* player = player_civ();
        for (void* city : all_cities()) {
            void* nation = F<void*>(city, kCityCiv);
            if (!nation || !skg::readable(nation, 0x274) || !skg::readable(city, 0x664)) continue;
            char row[210];
            snprintf(row, sizeof row, "city_pol=%08X nation_pol=%08X player=%d nation_species=%08X city_species=%08X wealth=%.2f available=%.2f\n",
                     pol_of(city), pol_of(nation), nation == player,
                     F<uint32_t>(nation, kNationSpecies), F<uint32_t>(city, kCitySpecies), (double)F<float>(nation, 0x98),
                     (double)thiscall<float>(a_GetMoney, nation));
            result += row;
        }
        return result;
    }
    if (a.size() >= 3 && a[0] == "fix") { // fix boost|world 0|1: the code before BUG-0040 / BUG-0059 (tests)
        bool on = atoi(a[2].c_str()) != 0;
        if (a[1] == "boost")
            g_fix_boost = on;
        else if (a[1] == "world")
            g_fix_world = on;
        else if (a[1] == "cities")
            g_fix_cities = on;
        else if (a[1] == "aispawn")
            g_fix_aispawn = on;
        else if (a[1] == "sw_vote")
            g_fix_sw_vote = on;
        else if (a[1] == "orphans")
            g_fix_orphans = on;
        else if (a[1] == "caption")
            g_fix_caption = on;
        else if (a[1] == "away")
            g_fix_away = on;
        else
            return "usage: coop.civ fix boost|world|cities|aispawn|sw_vote|orphans|caption|away 0|1";
        return "fix " + a[1] + " " + (on ? "on" : "off");
    }
    if (!a.empty() && a[0] == "spawns") { // spawns: the game's own vehicle spawns on this guest by caller
        std::string out;
        char b[64];
        for (auto& [ra, n] : g_local_spawns) {
            snprintf(b, sizeof b, "spawn from 0x%08X x%u\n", (unsigned)ra, n);
            out += b;
        }
        snprintf(b, sizeof b, "ai spawns blocked %u\n", g_ai_spawns_blocked);
        return out + b;
    }
    if (!a.empty() && a[0] == "tables") { // sizes of the pointer tables (tests: BUG-0040, BUG-0059)
        char b[320];
        snprintf(b, sizeof b,
                 "tables: hv=%u boost=%u alarm_at=%u siege=%u joint=%u gifted=%u bfix=%u host_cities=%u city_owner=%u "
                 "gv=%u world_resets=%u fix_boost=%d fix_world=%d next_id=%u world_changes=%u "
                 "mirror_designs=%u veh_fallback=%u build_skipped=%u\n",
                 (unsigned)g_hv.size(), (unsigned)g_boost.size(), (unsigned)g_alarm_at.size(), (unsigned)g_siege.size(),
                 (unsigned)g_joint_told.size(), (unsigned)g_city_gifted.size(), (unsigned)g_bfix_seen.size(),
                 (unsigned)g_host_cities.size(), (unsigned)g_city_owner.size(), (unsigned)g_gv.size(), g_world_resets,
                 (int)g_fix_boost, (int)g_fix_world, (unsigned)g_next_id, g_world_checks_fired, g_veh_design_checked,
                 g_veh_design_fallback, g_build_skipped);
        return b;
    }
    if (!a.empty() && a[0] == "fakeveh") { // tests (BUG-0469): fakeveh <hex design instance|off> [<hex type> <hex group>]
        // every mirror vehicle is built with that design key (the guest's own civ design otherwise)
        g_veh_force = a.size() > 1 && a[1] != "off";
        if (g_veh_force) {
            g_veh_force_key[0] = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
            g_veh_force_key[1] = a.size() > 2 ? (uint32_t)strtoul(a[2].c_str(), nullptr, 16) : 0x24682294; // vcl
            g_veh_force_key[2] = a.size() > 3 ? (uint32_t)strtoul(a[3].c_str(), nullptr, 16) : 0x40646200;
        }
        return std::string("mirror vehicles: the design key ") + (g_veh_force ? "is forced for the tests" : "is the civ's own");
    }
    if (!a.empty() && a[0] == "content") { // tests (BUG-0469): the model checks of the mirrors
        return creations::content_status();
    }
    if (a.size() >= 2 && a[0] == "design") { // design <city> [house|factory|fun]: the civ's building design key
        // and the model check of it (tests, BUG-0469: the guest checks a design it has)
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        uint32_t noun = a.size() > 2 && a[2] == "factory" ? 0x18EA2CC : a.size() > 2 && a[2] == "fun" ? 0x1A56ABA
                                                                                                      : 0x18EB106;
        uint32_t type = type_for_noun(noun), did = building_design_id(type);
        void* civ = city ? F<void*>(city, kCityCiv) : nullptr;
        uint32_t* k = civ && did ? thiscall<uint32_t*>(a_DesignKey, civ, (int)did) : nullptr;
        char b[220];
        snprintf(b, sizeof b, "design noun=%08X type=%08X id=%08X city=%p civ=%p key=%08X/%08X/%08X\n", noun, type, did,
                 city, civ, k ? k[0] : 0, k ? k[1] : 0, k ? k[2] : 0);
        if (!k || !k[0])
            return std::string(b) + "no design here: the game generates one\n";
        creations::Model m = creations::ensure_model({k[0], k[1], k[2]}, creations::kMkBuilding, false);
        return std::string(b) + "model " +
               (m.key.instance ? "ok" : "not in this game") + ", " + creations::content_status();
    }
    if (!a.empty() && a[0] == "nulllocal") { // tests (BUG-0223): the stage's local entity is null for N frames
        int n = a.size() > 1 ? atoi(a[1].c_str()) : 1;
        modes::test_null_local(n < 1 ? 1 : n > 600 ? 600 : n);
        return "the local entity is null for " + std::to_string(n < 1 ? 1 : n > 600 ? 600 : n) + " frames\n";
    }
    // tests of the co-op mechanics through the game's own functions (host)
    if (a.size() >= 3 && a[0] == "siege" && is_host()) { // siege <city> <player>: capture damage by that player
        size_t ci = (size_t)atoi(a[1].c_str());
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        uintptr_t ctxf = H->sym("Simulator::GetActivePlanetRecord");
        if (!city)
            return "no such city";
        void* pc = player_civ();
        g_hit_by = (uint8_t)atoi(a[2].c_str());
        float before = F<float>(city, 0x748);
        (void)pc; // an attacker that is not the city's owner (the game ignores its own civ)
        h_AddCapture(skg::cdecl_call<void*>(ctxf), city, (uint8_t*)city + 0x2F0, pol_of(city), 10.f, 0x0BADF00Du,
                     false);
        g_hit_by = 0xFF;
        char b[96];
        snprintf(b, sizeof b, "capture %.2f -> %.2f", before, F<float>(city, 0x748));
        return b;
    }
    if (a.size() >= 2 && a[0] == "alarm" && is_host()) { // alarm <city>: as if an enemy attacked it
        size_t ci = (size_t)atoi(a[1].c_str());
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        g_alarm_at.erase(city);
        h_OnAttacked(city, 0, 0x0BADF00D);
        return "alarm raised";
    }
    if (a.size() >= 2 && a[0] == "capture" && is_host()) { // capture <city>: the player civ takes it (tests, BUG-0187)
        size_t ci = (size_t)atoi(a[1].c_str());
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        void* pc = player_civ();
        if (!city || !pc)
            return "no such city / civ";
        int r = h_Capture(city, pol_of(pc), a.size() > 2 ? atoi(a[2].c_str()) : 0, 0, 0, 0);
        return "capture -> " + std::to_string(r);
    }
    if (!a.empty() && a[0] == "movie") { // the host's cinematic / caption (BUG-0187)
        char b[96];
        snprintf(b, sizeof b, "movie playing=%d closed=%u fix=%d wait=%.0f armed=%d arms=%u", (int)movie_playing(), g_captions_closed,
                 (int)g_fix_caption, g_caption_wait, (int)(g_caption_armed > 0), g_caption_arms);
        return b;
    }
    if (!a.empty() && a[0] == "sw") { // the superweapon votes (host, BUG-0117)
        // civ= the player civ (tests read the game's own fields: +0x98 money, +0x484 unlocked[12], BUG-0222)
        char b[192];
        snprintf(b, sizeof b, "sw last_request=%d fired=%d refused=%u fix=%d orphans_given=%u civ=%p", g_sw_last_req,
                 g_sw_fired, g_sw_refused, (int)g_fix_sw_vote, g_orphans_given, g_ok && game::mode_id() == modes::kCiv && nm() ? player_civ() : nullptr);
        return b;
    }
    if (a.size() >= 2 && a[0] == "super") { // super <type> [x y z]: fire a superweapon (through the vote)
        // BUG-0222: always a target, as the game's own callers pass one: FireSuperweapon reads it without
        // a null check (GOG 2024 0xBF8BB0: compares it with the zero vector 0x168C514 = untargeted)
        void* pc = player_civ();
        float t[3] = {0, 0, 0};
        if (a.size() >= 5)
            for (int i = 0; i < 3; i++)
                t[i] = (float)atof(a[2 + i].c_str());
        return pc ? std::string("fired -> ") + (h_FireSW(pc, atoi(a[1].c_str()), t) ? "1" : "0") : "no civ";
    }
    if (!a.empty() && a[0] == "boosts") { // the boosted vehicles (tests read the game's +0x218 speed, BUG-0222)
        std::string out;
        char b[96];
        double now = H->time_seconds();
        for (auto& [v, bo] : g_boost) {
            snprintf(b, sizeof b, "boost %p orig=%.3f left=%.1f\n", v, bo.orig, bo.until - now);
            out += b;
        }
        return out.empty() ? "no boosts\n" : out;
    }
    if (a.size() >= 3 && a[0] == "stale_hv" && is_host() && game::mode_id() == modes::kCiv) {
        // stale_hv <vehicle id> <owner> (tests, BUG-0222 / BUG-0059): the table entry of that vehicle as if
        // its address had held a player's vehicle of the old world: a player owner and another nation's pol
        auto it = g_hv_by_id.find((uint16_t)atoi(a[1].c_str()));
        if (it == g_hv_by_id.end() || !g_hv.count(it->second))
            return "no such vehicle";
        HV& h = g_hv[it->second];
        h.owner = (uint8_t)atoi(a[2].c_str());
        h.pol ^= 0x5A5A5A5Au;
        return "stale entry: owner=" + std::to_string(h.owner);
    }
    if (a.size() >= 3 && a[0] == "select") {
        void* v = local_vehicle((uint16_t)atoi(a[1].c_str()));
        if (!v)
            return "no such vehicle";
        thiscall<int>(a_SetSelected, (uint8_t*)v + kVSpatial, atoi(a[2].c_str()));
        return "selected=" + std::to_string(F<uint8_t>(v, kVSelected));
    }
    if (!a.empty() && a[0] == "pushprobe" && is_guest()) // tests (BUG-0186): who moves a mirror
        return push_probe();
    if (a.size() >= 5 && a[0] == "move") { // move <id> <x> <y> <z>
        void* v = local_vehicle((uint16_t)atoi(a[1].c_str()));
        if (!v)
            return "no such vehicle";
        thiscall<int>(a_SetMoveOrder, v, (float)atof(a[2].c_str()), (float)atof(a[3].c_str()),
                      (float)atof(a[4].c_str()), 1);
        return "move ordered";
    }
    if (a.size() >= 3 && a[0] == "buy") { // buy <host city index> <loco>: like the city menu button
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        uint8_t o = owner_of_city(city);
        if (o != C.sess->my_id() && o != kShared)
            return "this city belongs to player " + std::to_string(o);
        if (is_guest()) {
            request_buy(city, atoi(a[2].c_str()));
            return "purchase requested";
        }
        return host_buy(city, atoi(a[2].c_str())) ? "bought" : "purchase failed";
    }
    if (a.size() >= 3 && a[0] == "build") { // build <host city index> <house|factory|fun>
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        uint32_t noun = a[2] == "factory" ? 0x18EA2CC : a[2] == "fun" ? 0x1A56ABA : 0x18EB106;
        net::Vec3 p = city_pos(city);
        if (is_guest()) {
            net::Writer w;
            w.u8(CMD_BUILD);
            w.u8((uint8_t)ci);
            w.u32(noun);
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            send_cmd(w);
            g_applying = true; // the guest's own copy, as its planner would place it
            void* b = build_in_city(city, noun, p, true);
            g_applying = false;
            return b ? "built locally, sent to the host" : "no free slot";
        }
        return build_in_city(city, noun, p, true) ? "built" : "no free slot";
    }
    if (a.size() >= 1 && a[0] == "nouns") { // nouns [hex noun [TYPE]]: objects per noun, or that noun's objects on screen
        struct Q {
            uint32_t noun = 0, type = 0;
            std::unordered_map<uint32_t, int> count;
            std::string out;
            const SkHost* h;
        } q;
        q.h = H;
        if (a.size() >= 2)
            q.noun = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        if (a.size() >= 3)
            q.type = (uint32_t)strtoul(a[2].c_str(), nullptr, 16);
        game::for_each_object(
            [](void* o, uint32_t n, void* u) {
                auto* q = (Q*)u;
                if (!q->noun) {
                    q->count[n]++;
                    return true;
                }
                net::Vec3 p;
                if (n != q->noun || !game::object_alive(o) || !game::object_position(o, p) || q->out.size() > 6000)
                    return true;
                float w[3] = {p.x, p.y, p.z}, sc[2] = {-1, -1};
                if (!q->h->world_to_screen(w, sc))
                    sc[0] = sc[1] = -1;
                char line[128];
                snprintf(line, sizeof line, "obj %p %.0f %.0f %.0f %d %d pol=%08X%s\n", o, p.x, p.y, p.z, (int)sc[0],
                         (int)sc[1], pol_of(o), q->type && !game::cast(o, q->type) ? " (not that type)" : "");
                q->out += line;
                return true;
            },
            &q);
        if (q.noun)
            return q.out;
        std::string out;
        for (auto& [n, c] : q.count) {
            char line[48];
            snprintf(line, sizeof line, "noun %08X %d\n", n, c);
            out += line;
        }
        return out;
    }
    if (a.size() >= 2 && a[0] == "ruinlocal" && is_guest()) {
        // ruinlocal <host city index>: this game alone ruins one building of that city (tests, BUG-0324)
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        static const float zero[3] = {0, 0, 0};
        for (void* bl : vec_of((uint8_t*)city + kCityBuildings))
            if (type_for_noun(game::noun_id(bl)) && F<int>(bl, kBuildingState) != 2) {
                g_applying = true;
                thiscall<int>(a_CombatantDamage, (uint8_t*)bl + 0x120, F<float>(bl, kBuildingState + 4) + 1.f, 0xFFFFFFFFu,
                              0, (const float*)zero, (void*)nullptr);
                g_applying = false;
                char b[96];
                snprintf(b, sizeof b, "building %p ruined here: state %d", bl, F<int>(bl, kBuildingState));
                return b;
            }
        return "no building to ruin";
    }
    if (a.size() >= 2 && a[0] == "buildings") { // buildings <host city index>: object, noun, state, hp, position
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        std::string out;
        for (void* bl : vec_of((uint8_t*)city + kCityBuildings)) {
            net::Vec3 p;
            game::object_position(bl, p);
            char line[128];
            snprintf(line, sizeof line, "building %p noun=%08X state=%d hp=%.0f pos=(%.0f %.0f %.0f)\n", bl,
                     game::noun_id(bl), F<int>(bl, kBuildingState), F<float>(bl, kBuildingState + 4), p.x, p.y, p.z);
            out += line;
        }
        return out;
    }
    if (a.size() >= 2 && a[0] == "chunk") { // chunk <bytes>: smaller snapshot chunks (tests: many chunks)
        g_chunk_budget = std::clamp<size_t>((size_t)atoi(a[1].c_str()), 200, net::kMaxPacket - 48);
        g_max_chunk = 0;
        return "chunk budget " + std::to_string(g_chunk_budget);
    }
    if (a.size() >= 2 && a[0] == "slots") { // slots <host city index>: building plots, where they are on screen
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        std::string out;
        uint8_t* b = F<uint8_t*>(city, kCitySlots);
        uint8_t* e = F<uint8_t*>(city, kCitySlots + 4);
        for (int k = 0; b && b + (k + 1) * 0x18 <= e && k < 32; k++) {
            uint8_t* s = b + k * 0x18;
            float* p = (float*)(s + 0xC);
            float sc[2] = {-1, -1};
            if (!H->world_to_screen(p, sc))
                sc[0] = sc[1] = -1;
            char line[128];
            snprintf(line, sizeof line, "slot %d %s %.0f %.0f %.0f %d %d\n", k, *(void**)(s + 4) ? "busy" : "free", p[0],
                     p[1], p[2], (int)sc[0], (int)sc[1]);
            out += line;
        }
        return out;
    }
    return "usage: coop.civ select <id> <0|1> | move <id> <x> <y> <z> | buy <city> <0 land|1 water|2 air> | "
           "build <city> <house|factory|fun> | slots <city>";
}

std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    auto alive = all_vehicles();
    auto live = [&](void* x) { return std::find(alive.begin(), alive.end(), x) != alive.end(); };
    if (C.sess && C.sess->role() == net::Role::Host) {
        for (auto& [x, h] : g_hv)
            if (live(x))
                v.push_back({"v" + std::to_string(h.id), veh_pos(x), veh_rot(x)});
    } else {
        for (auto& [id, g] : g_gv)
            if (g.v && live(g.v))
                v.push_back({"v" + std::to_string(id), veh_pos(g.v), veh_rot(g.v)});
    }
    return v;
}

// own selected vehicles (ids as the host knows them); none: the own city whose menu was opened
static void own_selection(std::vector<uint16_t>& vehicles, int& city)
{
    uint8_t me = C.sess->my_id();
    city = -1;
    if (C.sess->role() == net::Role::Host) {
        for (auto& [v, h] : g_hv)
            if (h.owner == me && F<uint8_t>(v, kVSelected))
                vehicles.push_back(h.id);
    } else
        for (auto& [id, g] : g_gv)
            if (g.v && g.owner == me && F<uint8_t>(g.v, kVSelected))
                vehicles.push_back(id);
    if (vehicles.empty() && g_ui_city && owner_of_city(g_ui_city) == me)
        city = host_index_of_city(g_ui_city);
}

int own_selected(bool* is_city)
{
    if (!active())
        return 0;
    std::vector<uint16_t> v;
    int city;
    own_selection(v, city);
    if (is_city)
        *is_city = v.empty() && city >= 0;
    return v.empty() ? (city >= 0 ? 1 : 0) : (int)v.size();
}

int give_selected(uint8_t to)
{
    if (!active() || to == C.sess->my_id())
        return 0;
    std::vector<uint16_t> v;
    int city;
    own_selection(v, city);
    if (v.empty() && city < 0)
        return 0;
    net::Writer w;
    w.u8(CMD_GIVE);
    w.u8(v.empty() ? 1 : 0);
    w.u8(to);
    if (v.empty()) {
        w.u8(1);
        w.u16((uint16_t)city);
    } else {
        w.u8((uint8_t)std::min<size_t>(v.size(), 64));
        for (size_t i = 0; i < v.size() && i < 64; i++)
            w.u16(v[i]);
    }
    if (C.sess->role() == net::Role::Host) {
        net::Reader r(w.data.data(), w.data.size());
        host_command(C.sess->my_id(), r);
    } else
        send_cmd(w);
    if (v.empty())
        g_ui_city = nullptr;
    return v.empty() ? 1 : (int)v.size();
}

} // namespace civ
