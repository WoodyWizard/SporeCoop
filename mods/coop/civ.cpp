#include "civ.h"
#include "squadron.h"
#include "player_life.h"
#include "census.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "game.h"
#include "gamefixes.h"
#include "shared.h"
#include "stage_epoch.h"
#include "modes.h"
#include "net/held_cmds.h"
#include "net/ids.h"
#include "net/interp.h"
#include "net/validate.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "settings.h"
#include "fields.h"
#include "creations.h"
#include "savestrip.h"

using skg::thiscall;
using skg::vcall;

namespace tribe {
extern bool g_cycle_order_fix; // net.cycle_order (BUG-0612)
}

namespace civ {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_ok;

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

// ---------------- the local city planner (BUG-0491, BUG-0493) ----------------
// The city planner (cCommunityEditor, docs/re/civ.md#civ-planner-lifetime) pauses this game and keeps
// raw pointers into the city it edits: the building in the hand (+0xC0), the city's layout slots
// (+0xF4) and a map of objects it moves out of the way (+0x118, vehicles too) that its update
// (0xD124E0) reads every frame. The game itself changes nothing while it is open; the mod did (the
// host's buildings, ruins, captures and mirror removals arrived): a guest crashed in the planner at
// cLayoutSlot::RemoveObject on a garbage slot and on a freed object of that map (0.7.3.6).
constexpr uintptr_t kEdCity = 0x40, kEdMoved = 0x118, kEdMovedSize = 0x128;
constexpr uint32_t kCityType = 0xEE9B2232;
static uintptr_t a_EditorUpdate, a_EditorPickUp, a_DestroyInstance, a_VehicleRemove;
static void* g_editor;                  // the editor whose update ran last
static uint64_t g_editor_frame = ~0ull; // the frame of that update
static bool g_fix_planner_hold = true;
COOP_FIX("civ.planner_hold", "BUG-0493", g_fix_planner_hold,
         "civ: the mod changes no building, owner or mirror the open city planner holds until it closes");
static uint32_t g_planner_held, g_planner_kills_held, g_planner_picks_refused;
// BUG-0682: the editor of the last update is freed with the city input strategy by the stage teardown (the quit to
// the galaxy menu, a load): its memory stays readable, +0x40 garbage, and the next planner_city() within 3 frames
// cast it (a guest's caught fault in game::cast from wsync's city row, campaign_space). Now the editor is trusted
// only in the mode of its update and not while the stage is leaving, and +0x40 must be an object of the game's.
static bool g_fix_planner_stale = true;
COOP_FIX("civ.planner_stale", "BUG-0682", g_fix_planner_stale,
         "civ: the city planner's editor is forgotten at the quit / a mode change; its city must have a game vtable");
static uint32_t g_editor_mode;     // the mode of the last update
static bool g_planner_read;        // planner_city() read the editor's +0x40 (BUG-0682 probe)
static uint32_t g_planner_stale_refused;
static bool vtable_in_game(const void* obj);

// the city the local planner edits now, or null (the update runs every frame while it is open)
static void* planner_city()
{
    if (!g_editor || H->frame_index() - g_editor_frame > 3 || !skg::readable(g_editor, kEdMovedSize + 4))
        return nullptr;
    if (g_fix_planner_stale && (game::leaving_stage() || game::mode_id() != g_editor_mode)) {
        g_editor = nullptr; // (the update sets it again while the planner lives)
        g_planner_stale_refused++;
        return nullptr;
    }
    g_planner_read = true;
    void* o = F<void*>(g_editor, kEdCity);
    if (g_fix_planner_stale && o && (!skg::readable(o, 0x10) || !vtable_in_game(o))) {
        g_planner_stale_refused++;
        return nullptr;
    }
    return o ? game::cast(o, kCityType) : nullptr;
}
static bool planner_holds(void* city) { return g_fix_planner_hold && city && city == planner_city(); }
bool planner_holds_city(void* city) { return planner_holds(city); }

// BUG-0682 test probe (`coop.civ planner_probe on|off`): planner_city() once a frame in every mode, as wsync's city rows
// and the guest's messages call it; counts the frames in which it read the editor of an update of another mode or
// while the stage was leaving (the freed editor of the teardown) and the cities it returned then
static bool g_planner_probe;
static uint32_t g_probe_frames, g_probe_stale_reads, g_probe_stale_cities, g_probe_seen;
void planner_probe_tick()
{
    if (!g_planner_probe || !g_ok)
        return;
    g_probe_frames++;
    bool stale = g_editor && (game::leaving_stage() || game::mode_id() != g_editor_mode);
    if (g_editor)
        g_probe_seen++;
    g_planner_read = false;
    void* c = planner_city();
    if (stale && g_planner_read) {
        g_probe_stale_reads++;
        if (c)
            g_probe_stale_cities++;
    }
}
// the spatial objects in the planner's moved-objects map (an eastl rbtree: anchor right +0, left +4,
// root +8; node value at +0x10 = the object's spatial interface, then its target position)
static void planner_moved(std::vector<void*>& out)
{
    if (!planner_city())
        return;
    uint8_t* anchor = (uint8_t*)g_editor + kEdMoved;
    size_t n = F<uint32_t>(g_editor, kEdMovedSize);
    std::vector<uint8_t*> st;
    if (uint8_t* root = F<uint8_t*>(anchor, 8))
        st.push_back(root);
    while (!st.empty() && out.size() < std::min<size_t>(n, 256)) {
        uint8_t* nd = st.back();
        st.pop_back();
        if (!nd || nd == anchor || !skg::readable(nd, 0x20))
            continue;
        out.push_back(F<void*>(nd, 0x10));
        st.push_back(F<uint8_t*>(nd, 0));
        st.push_back(F<uint8_t*>(nd, 4));
    }
}
static bool planner_moves(void* v)
{
    if (!g_fix_planner_hold || !v)
        return false;
    std::vector<void*> m;
    planner_moved(m);
    return std::find(m.begin(), m.end(), (uint8_t*)v + kVSpatial) != m.end();
}
// BUG-0491: the planner's pick-up (0xD0FCC0, thiscall(editor, spatial) ret 4; 0 picked, 2 not) looks
// for the layout slot nearest to the building and, when that slot holds another object, goes on with
// slot index -1: cLayoutSlot::RemoveObject and SetObject on the 0x18 bytes before the slot vector
// (heap memory). A building in no slot of its own (or in a slot of another) is refused like a click on
// nothing. `coop.fix civ.planner_pick off`: the game's own code.
static bool g_fix_planner_pick = true;
COOP_FIX("civ.planner_pick", "BUG-0491", g_fix_planner_pick,
         "civ: the city planner does not pick up a building that is not in its layout slot (the game wrote before the slot list)");
static bool planner_pick_ok(void* ed, void* sp)
{
    void* city = F<void*>(ed, kEdCity) ? game::cast(F<void*>(ed, kEdCity), kCityType) : nullptr;
    if (!city || !sp || !vcall<void*>(sp, 0xB8 / 4, kBuildingType))
        return true; // not a building of a city: the game's own checks
    void* occupant = vcall<void*>(sp, 0xB8 / 4, (uint32_t)0x17F243B); // what the game compares
    const float* p = vcall<const float*>(sp, 0x2C / 4);
    uint8_t* b = F<uint8_t*>(city, kCitySlots);
    uint8_t* e = F<uint8_t*>(city, kCitySlots + 4);
    if (!p || !b || e <= b)
        return false;
    int best = -1;
    float bd = 3.4e38f;
    for (int k = 0; b + (k + 1) * 0x18 <= e && k < 64; k++) {
        float* s = (float*)(b + k * 0x18 + 0xC);
        float d = (p[0] - s[0]) * (p[0] - s[0]) + (p[1] - s[1]) * (p[1] - s[1]) + (p[2] - s[2]) * (p[2] - s[2]);
        if (d < bd) {
            bd = d;
            best = k;
        }
    }
    return best >= 0 && F<void*>(b + best * 0x18, 4) == occupant;
}
using EditorPickUp_t = int(SK_THISCALL*)(void*, void*);
static EditorPickUp_t o_EditorPickUp;
static int SK_THISCALL h_EditorPickUp(void* ed, void* sp)
{
    if (g_fix_planner_pick && g_ok && game::mode_id() == modes::kCiv && !planner_pick_ok(ed, sp)) {
        if (g_planner_picks_refused++ < 10)
            H->log(M, SK_INFO, "civ: the city planner picked up a building that is in no slot of its own: refused (BUG-0491)");
        return 2;
    }
    return o_EditorPickUp(ed, sp);
}
// BUG-0492: the "new discovery" card (cUIAssetDiscoveryManager, 0xE30CF0 thiscall(mgr, data) ret 4)
// reads data+0xC at once; the civilization's card data (0xBA5B60) is null when the political id has
// no civilization in this game (GetCivilizationByPoliticalID null): a guest clicked a city whose owner
// its game does not know (0.7.3.6 crash at SetImageForWindow+0x21F). No data: no card.
static bool g_fix_discovery_card = true;
COOP_FIX("civ.discovery_card", "BUG-0492", g_fix_discovery_card,
         "civ: no discovery card for a civilization this game does not have (the game read a null card)");
static uint32_t g_cards_skipped, g_owner_unknown;
static int g_met = -1; // tests: the last `coop.civ discover`
using DiscoveryCard_t = void(SK_THISCALL*)(void*, void*);
static DiscoveryCard_t o_DiscoveryCard;
static void SK_THISCALL h_DiscoveryCard(void* mgr, void* data)
{
    if (!data && g_fix_discovery_card) {
        if (g_cards_skipped++ < 10)
            H->log(M, SK_INFO, "civ: a discovery card without data (a civilization this game does not have): skipped (BUG-0492)");
        return;
    }
    o_DiscoveryCard(mgr, data);
}
using EditorUpdate_t = int(SK_THISCALL*)(void*, int);
static EditorUpdate_t o_EditorUpdate;
static int SK_THISCALL h_EditorUpdate(void* ed, int x)
{
    g_editor = ed;
    g_editor_frame = H->frame_index();
    g_editor_mode = game::mode_id();
    return o_EditorUpdate(ed, x);
}
static uint32_t pol_of(void* o) { return F<uint32_t>(o, kPol); }
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
// BUG-0875: a city a player captured keeps its captor when the cities are dealt out again (the shared capital's
// threshold, a join or a leave). `coop.fix civ.capture_keeps off`: every city but the gifts dealt in turns (0.7.12).
static bool g_fix_capture_keeps = true;
COOP_FIX("civ.capture_keeps", "BUG-0875", g_fix_capture_keeps,
         "civ: a city a player captured stays that player's when the cities are dealt out again");
static std::unordered_set<void*> g_city_captured; // host: cities a player captured
// host: the captor of a capture whose ChangeOwner waits for its cinematic (Capture plays CVG_Capture and the game
// changes the owner at its end, docs/re/civ.md#civ-capture-keeps): the city is not the player civilization's yet
// when h_Capture names the captor, so the drop loop of host_assign_cities forgot it and dealt the city out anew
struct PendingCapture {
    uint8_t owner;
    double at;
};
static std::unordered_map<void*, PendingCapture> g_capture_by;
static void* g_ui_city;                               // the city whose menu was opened last
static uint32_t g_gifts;

// ---------------- host ----------------
struct HV {
    uint16_t id;
    uint8_t owner;
    uint32_t pol; // the vehicle's political id when it was listed (BUG-0059: a reused address)
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
constexpr double kVehLead = 0.3; // BUG-0425
// BUG-0438 (K3): a mirrored vehicle is placed where its target is when the frame is drawn. guest_tick moved it half
// way from where it stood to the target every frame (StopMovement + Teleport: the game does not move it), so a vehicle
// at 15-20 m/s was drawn a frame's glide plus the frame the placing shows up in behind its own target: 0.4-1.8 m (p50)
// between the drawn vehicle and the target, more than the target's own error (coop.bodyrec diag rows, body_trail_civ_loss,
// lab 20261107-074040). With the fix the vehicle goes on with the target's velocity and only the rest (a new state's
// correction, a push) is glided half per frame, aimed one frame ahead (the last frame's time, at most 0.1 s). Off: 0.7.9.
static bool g_place_now_fix = true;
COOP_FIX("civ.place_now", "BUG-0438", g_place_now_fix,
         "civ: a mirrored vehicle is drawn on its target, not half a step and a frame behind it");
static bool g_vel_cap_fix = true;
COOP_FIX("civ.vel_cap", "BUG-0186", g_vel_cap_fix,
         "civ: a vehicle step faster than twice its top speed is taken as a jump, not a speed to extrapolate");
// BUG-0787: the jump limit came from the guest mirror's own top speed (+0x218), which the host's alarm boost (or a
// faster host design) does not change: a host vehicle over twice as fast had every step taken as a jump, the mirror
// stood and jumped. The host now sends each vehicle's top speed after the snapshot time (a tail an older guest
// ignores) and the guest takes the larger of the two.
static bool g_vel_cap_host_fix = true;
COOP_FIX("civ.vel_cap_host", "BUG-0787", g_vel_cap_host_fix,
         "civ: a guest's jump limit for a host vehicle uses the host vehicle's top speed (a boosted vehicle glides, it does not stand and jump)");
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
    float host_top = -1.f; // BUG-0787: the host vehicle's top speed from the last snapshot, -1 = not sent
};
static std::unordered_map<uint16_t, GV> g_gv;
static std::unordered_map<void*, uint16_t> g_gv_of;

static std::unordered_set<uint16_t> g_cycle_ids;
static net::CycleTracker g_cyc;                    // the cycle being received and its chunks (BUG-0612)
static uint32_t g_incomplete;                      // cycles whose cleanup was skipped (a chunk missing)
static uint32_t g_last_chunks, g_max_chunk;        // host: chunks per cycle, largest chunk (bytes)
static size_t g_chunk_budget = net::kMaxPacket - 48; // host: bytes per chunk (coop.civ chunk <bytes>, tests)
static bool& g_cycle_order_fix = tribe::g_cycle_order_fix; // tribe.cpp (net.cycle_order, BUG-0612)
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
// ---------------- BUG-0477: the host's shots on the guests ----------------
// Every Civ weapon (vehicles, city turrets) fires through cToolManager::FireTool (0x01050080, called each
// update by cCombatSimulator::Update 0xAE7360 for a combatant whose target is in range; the tool's own
// refire time decides whether a projectile leaves). A guest's mirrors run no behaviour tree, so they never
// had a target and the guest saw the host's fights only as falling hit points. Now the host notes who
// fires where (squadron.cpp's FireTool hook calls fire_observed) and sends the shooters of the last 0.25 s
// every 0.2 s (MSG_CIV_STATE with chunk 0xFE / chunks 0: an older guest drops it at cycle_chunk_ok); the
// guest fires its mirror's own weapon at the same point with the same FireTool, so the game makes the
// projectile, its flight and its explosion. Its hits do nothing on the guest (cosmetic_hit: the damage
// of a mirror's weapon is dropped in the TakeDamage hooks; the host's state brings the real damage).
// `coop.fix civ.shots off`: the guest shows no shot (0.7.4).
static bool g_fix_shots = true;
COOP_FIX("civ.shots", "BUG-0477", g_fix_shots, "civ: a guest sees the host's vehicles and turrets fire (no damage of their own)");
constexpr uint8_t kShotChunk = 0xFE;     // MSG_CIV_STATE chunk index of a shot list (chunks 0)
constexpr uint32_t kTurretNoun = 0x0436F342;
constexpr uintptr_t kVCombatant = 0x508, kBCombatant = 0x120;
// BUG-0688: a city turret is no building: its combatant is at +0x588 (vtable 0x1466C28 GOG 2024, the turret TakeDamage's
// `this`); +0x120 of a turret holds no object. guest_fire took every turret's combatant at kBCombatant and
// weapon_of called vt 0x14 of that word: the guest crashed (execute of 0xCCCCCCCC) at the first turret shot it
// copied. Now a turret's combatant is taken at kTCombatant, and weapon_of calls vt 0x14 only of a combatant
// whose vtable lies in the game's image and whose slot 0x14 points into the game's code (any shooter kind; a
// wrong one is skipped and logged once). `coop.fix civ.turret_weapon off`: the old offset, no check (0.7.10).
constexpr uintptr_t kTCombatant = 0x588;
static bool g_fix_turret_weapon = true;
COOP_FIX("civ.turret_weapon", "BUG-0688", g_fix_turret_weapon,
         "civ: a guest copies a host turret's shot with the turret's own weapon (no call through a non-object)");
static uint32_t g_turret_shots_seen, g_turret_fired, g_bad_comb; // host: turret shots noted; guest: copies fired, bad combatants
static uintptr_t shooter_comb_off(uint8_t kind) { return kind == 1 ? kVCombatant : g_fix_turret_weapon ? kTCombatant : kBCombatant; }
// BUG-0647: the vehicle's model key {instance, type, group} (its model object +0x34, +0x90: what cVehicle::Load
// 0xCA67C0 sets through vt 0x94 of +0x34; runtime: +0xC4 holds the design instance, coop.memfind)
constexpr uintptr_t kVModelKey = 0xC4;
constexpr double kShotSend = 0.2, kShotKeep = 0.25, kShotFireEvery = 0.1;
static uintptr_t a_FireTool, a_ToolMgrGet;
struct Shot {
    uint8_t kind = 0;   // 1 vehicle (host net id), 3 turret (its position)
    uint16_t id = 0;
    net::Vec3 at{};     // turret: where it stands
    net::Vec3 aim{};
    uint8_t tkind = 0;  // its target (FireTool's, a spatial: object + 0x34): 0 none, 1 vehicle id, 3 noun + position
    uint16_t tid = 0;
    uint32_t tnoun = 0;
    net::Vec3 tpos{};
    double seen = -100; // host: the last FireTool; guest: until when it fires
    double tried = -100; // guest: the last FireTool of the mirror
    uint8_t count = 0;   // BUG-0530: host: the shots that left (FireTool true); guest: the host's count
    uint8_t done = 0;    // guest: the host's shots this game fired (FireTool true)
    bool counted = false; // guest: a list of the host's fired shots (chunk kShotChunkFired)
    bool stopped = false; // BUG-0530: the host's game stopped this weapon (cToolManager::OnMouseUp)
    bool released = false; // guest: this game's copy of the weapon was stopped too
};
static std::map<std::pair<uint16_t, uint32_t>, Shot> g_shots;   // host: shooter -> last shot
static std::map<std::pair<uint16_t, uint32_t>, Shot> g_shots_in; // guest: shooter -> its shots
static double g_shots_sent_at = -100;
static uint32_t g_shots_seen, g_shots_tx, g_shots_rx, g_shots_fired, g_shots_nomirror, g_shots_notool,
    g_hits_dropped;
static uint32_t g_shots_tgt_wanted, g_shots_tgt_found;
static uint32_t g_shots_notarget; // guest: shots not fired, their target is not in this game (BUG-0534)
static bool g_fix_shot_target = true;
COOP_FIX("civ.shot_target", "BUG-0534", g_fix_shot_target,
         "civ: a guest fires a host's shot only at a target it has (the game faults on a shot without one)");
static std::unordered_set<void*> g_turret_shooters; // guest: turrets fired from (their hits are dropped too)
// BUG-0530: cCombatSimulator::Update calls FireTool every update for a combatant with a target in range; FireTool
// returns true only when the weapon's refire time passed and its strategy (vt 0x34, WhileFiring) made the shot
// (GOG 2024 0x0105030B: `call [strategy+0x34]`, its al returned). The relay of every call let a guest's weapon
// fire on its own refire time for as long as the host's kept trying: religious (cultural) shots and AoE areas
// the host's game never made. Now the host counts the shots that left and the guest fires that many.
static bool g_fix_shot_fired = true;
COOP_FIX("civ.shot_fired", "BUG-0530", g_fix_shot_fired,
         "civ: a guest fires the host's shots that left, not one per FireTool the host's game tried");
constexpr uint8_t kShotChunkFired = 0xFC;  // MSG_CIV_STATE chunk index of a counted shot list (chunks 0)
constexpr double kShotKeepFired = 1.0;     // host: a shooter stays in the lists (its count) for so long
static uint32_t g_shots_tried, g_shots_refused; // host: FireTool calls that made no shot; guest: FireTool false
// BUG-0530: a shot that left goes out at once (at most every 0.05 s), not with the next 0.2 s list: religious
// projectiles and AoE areas live about a second, and 0.2-0.4 s of relay delay showed them in one game only
static bool g_shot_new;
// BUG-0530: an AoE weapon (cDefaultAoETool) makes its area once when it starts firing (tool +0x120) and keeps it
// until cToolManager::OnMouseUp (0x0104FAA0, thiscall(mgr, tool) ret 4) stops the tool; cCombatSimulator::Update
// stops a combatant's tool when its target is gone. The area sends cultural projectiles at what is inside. A guest's
// copy of the weapon was never stopped: its area (and the area's cultural projectiles) stayed in the guest's game.
// Now the host notes the stop of a listed shooter's tool, the guest stops its copy (and at the latest when the
// shooter's list entry ends).
static bool g_fix_shot_release = true;
COOP_FIX("civ.shot_release", "BUG-0530", g_fix_shot_release,
         "civ: a guest stops its copy of a host's weapon when the host's stops (no AoE area left behind)");
static uintptr_t a_ToolMouseUp;
static uint32_t g_shot_stops_seen, g_shot_released;
static bool g_fix_shot_now = true;
COOP_FIX("civ.shot_now", "BUG-0530", g_fix_shot_now,
         "civ: the host's new shots go to the guests at once (no 0.2 s wait for the next shot list)");

static uint32_t pos_key(const net::Vec3& p)
{
    return (uint32_t)(int)std::lround(p.x) * 73856093u ^ (uint32_t)(int)std::lround(p.y) * 19349663u ^
           (uint32_t)(int)std::lround(p.z) * 83492791u;
}

// an object whose vtable lies in the game's image (no read through a vtable that is not one: a target of FireTool
// can be an object without a spatial at -kVSpatial, and IsBadReadPtr on its "vtable", a float, wrote a fault
// per shot to the host's log, BUG-0535)
static bool vtable_in_game(const void* obj)
{
    static uintptr_t lo, hi;
    if (!hi)
        if (HMODULE exe = GetModuleHandleW(nullptr)) {
            auto* nt = (IMAGE_NT_HEADERS*)((uint8_t*)exe + ((IMAGE_DOS_HEADER*)exe)->e_lfanew);
            lo = (uintptr_t)exe;
            hi = lo + nt->OptionalHeader.SizeOfImage;
        }
    uintptr_t vt = *(const uintptr_t*)obj;
    return vt >= lo && vt + 0x24 <= hi;
}

// BUG-0688: an address inside an executable section of the game's image (a virtual function of the game)
static bool code_in_game(uintptr_t a)
{
    static uintptr_t lo, hi;
    if (!hi)
        if (HMODULE exe = GetModuleHandleW(nullptr)) {
            auto* nt = (IMAGE_NT_HEADERS*)((uint8_t*)exe + ((IMAGE_DOS_HEADER*)exe)->e_lfanew);
            auto* sec = IMAGE_FIRST_SECTION(nt);
            for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++)
                if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
                    uintptr_t b = (uintptr_t)exe + sec[i].VirtualAddress, e = b + sec[i].Misc.VirtualSize;
                    lo = lo && lo < b ? lo : b;
                    hi = hi > e ? hi : e;
                }
        }
    return a >= lo && a < hi;
}

// BUG-0530 (diagnostics): FireTool calls a guest's game makes itself in Civ (not guest_fire's copies), by caller
static bool g_in_guest_fire;
struct LocalFire {
    uint32_t calls = 0, fired = 0;
    uintptr_t owner_vt = 0;
};
static std::map<uintptr_t, LocalFire> g_local_fire;
// BUG-0530 (diagnostics): shots by the tool's strategy key (tool +0x178: which weapon made it), host and guest
static std::map<uint32_t, uint32_t> g_shot_tools;
static void note_tool(void* tool)
{
    if (tool && !IsBadReadPtr(tool, 0x17C) && g_shot_tools.size() < 32)
        g_shot_tools[F<uint32_t>(tool, 0x178)]++;
}
static void shots_log(double now) // BUG-0530 (diagnostics): the shots by weapon every 120 s in the game log
{
    static double last = -1;
    if (last >= 0 && now - last < 120)
        return;
    last = now;
    if (g_shot_tools.empty())
        return;
    std::string t;
    for (auto& [k, n] : g_shot_tools) {
        char x[32];
        snprintf(x, sizeof x, " %08X=%u", k, n);
        t += x;
    }
    H->log(M, SK_INFO, "civ: shots by weapon (%s):%s", is_host() ? "host, fired" : "guest, copies fired", t.c_str());
}
void fire_local(void* owner, uintptr_t ret, bool fired)
{
    if (g_in_guest_fire || !is_guest() || game::mode_id() != modes::kCiv || !owner)
        return;
    auto& e = g_local_fire[ret];
    e.calls++;
    e.fired += fired;
    void* o = (uint8_t*)owner - kVSpatial;
    if (!e.owner_vt && !IsBadReadPtr(o, 4))
        e.owner_vt = *(uintptr_t*)o;
    if (fired && e.fired == 1 && g_local_fire.size() <= 16)
        H->log(M, SK_INFO, "civ: this guest's game fired a tool itself: FireTool from 0x%08X (owner vtable %08X, noun %08X)",
               (unsigned)ret, (unsigned)e.owner_vt, e.owner_vt && vtable_in_game(o) ? game::noun_id(o) : 0u);
}

void fire_observed(void* tool, void* owner, const float* aim, void* target, bool fired)
{
    if (!g_fix_shots || !owner || !aim || !tool || !is_host() || !active())
        return;
    if (g_fix_shot_fired && !fired) {
        g_shots_tried++;
        return;
    }
    if (fired)
        note_tool(tool);
    void* o = (uint8_t*)owner - kVSpatial;
    Shot s;
    auto it = g_hv.find(o);
    if (it != g_hv.end()) {
        s.kind = 1;
        s.id = it->second.id;
    } else if (!IsBadReadPtr(o, 0x40) && vtable_in_game(o) && game::noun_id(o) == kTurretNoun && game::object_position(o, s.at)) {
        s.kind = 3;
        g_turret_shots_seen++;
    } else
        return;
    s.aim = {aim[0], aim[1], aim[2]};
    if (!std::isfinite(s.aim.x) || !std::isfinite(s.aim.y) || !std::isfinite(s.aim.z))
        return;
    if (target && target != owner && !IsBadReadPtr((uint8_t*)target - kVSpatial, 0x40)) {
        void* t = (uint8_t*)target - kVSpatial;
        auto tv = g_hv.find(t);
        if (tv != g_hv.end()) {
            s.tkind = 1;
            s.tid = tv->second.id;
        } else if (vtable_in_game(t) && game::object_alive(t) && game::object_position(t, s.tpos)) {
            s.tkind = 3;
            s.tnoun = game::noun_id(t);
        } else
            s.tkind = 2; // a target the guest cannot find: it does not fire this one (BUG-0534)
    }
    s.seen = H->time_seconds();
    auto& e = g_shots[{s.id, s.kind == 3 ? pos_key(s.at) : 0u}];
    s.count = (uint8_t)(e.count + 1);
    g_shot_new = true;
    e = s;
    g_shots_seen++;
}

static void tribe_count_log(double now); // BUG-0539 diagnostics (below)
static void host_send_shots(double now)
{
    shots_log(now);
    tribe_count_log(now);
    bool soon = g_fix_shot_now && g_fix_shot_fired && g_shot_new && now - g_shots_sent_at >= 0.05;
    if (!g_fix_shots || (now - g_shots_sent_at < kShotSend && !soon))
        return;
    g_shots_sent_at = now;
    g_shot_new = false;
    const bool counted = g_fix_shot_fired;
    const double keep = counted ? kShotKeepFired : kShotKeep;
    std::vector<const Shot*> list;
    for (auto it = g_shots.begin(); it != g_shots.end();) {
        if (now - it->second.seen > keep) {
            it = g_shots.erase(it);
            continue;
        }
        if (list.size() < 120)
            list.push_back(&it->second);
        ++it;
    }
    // one message holds at most 22 shooters (47 bytes each, kMaxPacket): 40 in one (up to 0.7.5) were 1845
    // bytes, a message the connection drops, so with more than 24 shooters a guest saw no shot at all
    constexpr size_t kPerMsg = 22;
    for (size_t at = 0; at < list.size(); at += kPerMsg) {
        size_t n = std::min(kPerMsg, list.size() - at);
        net::Writer w;
        w.u16(0); w.u8(counted ? kShotChunkFired : kShotChunk); w.u8(0);
        w.u8((uint8_t)n);
        for (size_t i = at; i < at + n; i++) {
            const Shot* s = list[i];
            w.u8(s->kind); w.u16(s->id);
            w.f32(s->at.x); w.f32(s->at.y); w.f32(s->at.z);
            w.f32(s->aim.x); w.f32(s->aim.y); w.f32(s->aim.z);
            w.u8(s->tkind); w.u16(s->tid); w.u32(s->tnoun);
            w.f32(s->tpos.x); w.f32(s->tpos.y); w.f32(s->tpos.z);
            if (counted) {
                w.u8(s->count);
                w.u8(s->stopped ? 1 : 0);
            }
        }
        for (auto& pl : C.sess->players())
            if (pl.id != 0)
                C.sess->send_unreliable_to(pl.id, net::MSG_CIV_STATE, w);
        g_shots_tx++;
    }
}

static void guest_shots(net::Reader& r, bool counted)
{
    uint8_t n = r.u8();
    double now = H->time_seconds();
    for (int i = 0; i < n && r.ok(); i++) {
        Shot s;
        s.kind = r.u8(); s.id = r.u16();
        s.at.x = r.f32(); s.at.y = r.f32(); s.at.z = r.f32();
        s.aim.x = r.f32(); s.aim.y = r.f32(); s.aim.z = r.f32();
        s.tkind = r.u8(); s.tid = r.u16(); s.tnoun = r.u32();
        s.tpos.x = r.f32(); s.tpos.y = r.f32(); s.tpos.z = r.f32();
        uint8_t flags = 0;
        if (counted) {
            s.count = r.u8();
            flags = r.u8();
        }
        s.stopped = (flags & 1) != 0;
        if (!r.ok() || (s.kind != 1 && s.kind != 3) || !std::isfinite(s.aim.x) || !std::isfinite(s.aim.y) ||
            !std::isfinite(s.aim.z) || !std::isfinite(s.at.x))
            return;
        auto key = std::make_pair(s.id, s.kind == 3 ? pos_key(s.at) : 0u);
        auto found = g_shots_in.find(key);
        bool fresh = found == g_shots_in.end() || found->second.counted != counted;
        auto& e = g_shots_in[key];
        double tried = e.tried;
        if (counted && g_fix_shot_now && !fresh && e.count != s.count)
            tried = -100; // a new shot of the host's: fired on this frame (the weapon's refire time still decides)
        uint8_t done = fresh ? (uint8_t)(s.count - 1) : e.done; // a shooter first heard of: its last shot
        bool released = !fresh && e.released && s.stopped;
        e = s;
        e.tried = tried;
        e.counted = counted;
        e.done = done;
        e.released = released;
        if (s.stopped)
            e.done = e.count; // fires no more
        if ((uint8_t)(e.count - e.done) > 3) // far behind (a hitch, lost lists): the last few only
            e.done = (uint8_t)(e.count - 3);
        // fires on until then (the next list keeps it going; not much past the host's last shot)
        e.seen = now + (counted ? kShotKeepFired + 0.2 : kShotKeep + 0.05);
        g_shots_rx++;
    }
}

// BUG-0539 (diagnostics): who destroys a Tribe stage tribe in Civ, and when the count of tribes changes
constexpr uint32_t kTribeNoun = 0x018C6D19;
static bool g_tribe_gone; // BUG-0539, host: a tribe was destroyed since the last list
static std::map<uintptr_t, uint32_t> g_tribe_destroys;
// BUG-0623: a listener of gamefixes' DestroyInstance hook (a second hook of the function failed and switched off
// the stale stimulus sender guard, BUG-0456)
static void on_destroy_instance(void* obj, uintptr_t ra)
{
    if (obj && game::mode_id() == modes::kCiv && !IsBadReadPtr(obj, 4) && vtable_in_game(obj) &&
        game::noun_id(obj) == kTribeNoun) {
        if (is_host())
            g_tribe_gone = true; // BUG-0539: the guests hear of it with the next tribe list
        if (g_tribe_destroys[ra]++ == 0 && g_tribe_destroys.size() <= 8)
            H->log(M, SK_INFO, "civ: a tribe of the Tribe stage is destroyed here (%s), DestroyInstance from 0x%08X",
                   is_host() ? "host" : is_guest() ? "guest" : "alone", (unsigned)ra);
    }
}
// BUG-0539: the host's NPC vehicles raid the old Tribe stage villages (a behaviour tree action, 0xDCAAB0 "HR":
// money and an event, then DestroyInstance of the tribe at 0xDCB2B2, its huts, tools, food mat and bundles go with
// it); a guest's mirrors run no behaviour tree, so the guest's villages stood for good. The host sends the places of
// its tribes in Civ (MSG_CIV_STATE chunk 0xFA, every 5 s and when one is destroyed); a guest destroys a tribe of its
// own with no host tribe within 10 m (two looks in a row), the same DestroyInstance.
static bool g_fix_tribe_ruins = true;
COOP_FIX("civ.tribe_ruins", "BUG-0539", g_fix_tribe_ruins,
         "civ: an old tribe village the host's vehicles raided is gone on the guests too");
constexpr uint8_t kTribeChunk = 0xFA;
static uintptr_t a_DestroyInst;
static double g_tribes_sent_at = -100;
static uint32_t g_tribes_tx, g_tribes_rx, g_tribes_removed;
static std::unordered_map<void*, int> g_tribe_extra_seen; // guest: tribe -> looks without a host tribe
static std::vector<std::pair<void*, net::Vec3>> tribes_here()
{
    std::vector<std::pair<void*, net::Vec3>> out;
    game::for_each_object([](void* o, uint32_t noun, void* u) {
        net::Vec3 p;
        if (noun == kTribeNoun && game::object_alive(o) && game::object_position(o, p))
            ((std::vector<std::pair<void*, net::Vec3>>*)u)->push_back({o, p});
        return true;
    }, &out);
    return out;
}
// BUG-0659 (docs/re/civ.md#civ-tribe-ruins-fast): a guest removes a village on its second look without the host's
// tribe, and the second list came 5 s after the one sent 0.5 s after the raid: each raid left the village in the
// guest's world for 5-5.5 s (the detector's transient `Civilization:planet:village` / tribe_hut / tribe_tool /
// tribe_food_mat rows, one sample each). With civ.tribe_ruins_fast the host sends kQuickLists lists 0.5 s apart after
// a raid (the two looks then come within ~1 s; a lost one costs 0.5 s). Off: one quick list (0.7.10).
static bool g_tribe_ruins_fast = true;
COOP_FIX("civ.tribe_ruins_fast", "BUG-0659", g_tribe_ruins_fast,
         "civ: a raided old tribe village goes on the guests within about a second, not 5 s later");
constexpr int kQuickLists = 3;
static int g_quick_left;
static void host_send_tribes(double now)
{
    if (g_tribe_gone)
        g_quick_left = g_tribe_ruins_fast ? kQuickLists : 1;
    g_tribe_gone = false;
    if (!g_fix_tribe_ruins || !(now - g_tribes_sent_at >= 5.0 || (g_quick_left > 0 && now - g_tribes_sent_at >= 0.5)))
        return;
    if (now - g_tribes_sent_at < 5.0)
        g_quick_left--;
    g_tribes_sent_at = now;
    auto ts = tribes_here();
    if (ts.size() > 80)
        ts.resize(80); // 80 x 12 bytes: one packet
    net::Writer w;
    w.u16(0); w.u8(kTribeChunk); w.u8(0);
    w.u8((uint8_t)ts.size());
    for (auto& [t, p] : ts) {
        w.f32(p.x); w.f32(p.y); w.f32(p.z);
    }
    for (auto& pl : C.sess->players())
        if (pl.id != 0)
            C.sess->send_unreliable_to(pl.id, net::MSG_CIV_STATE, w);
    g_tribes_tx++;
}
static void guest_tribes(net::Reader& r)
{
    std::vector<net::Vec3> hp(r.u8());
    for (auto& p : hp) {
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
    }
    if (!r.ok())
        return;
    g_tribes_rx++;
    if (!g_fix_tribe_ruins || !a_DestroyInst || local_play() || game::mode_id() != modes::kCiv)
        return;
    auto here = tribes_here();
    // a list without any tribe while this game has several: the host's world is not there yet (a load, a
    // stage change), not a raid of all of them; and never more than two per list (a raid takes one at a time)
    if (hp.empty() && here.size() > 2)
        return;
    int removed = 0;
    std::unordered_map<void*, int> seen;
    for (auto& [t, p] : here) {
        bool host_has = false;
        for (auto& q : hp)
            if (net::dist(p, q) < 10.f) {
                host_has = true;
                break;
            }
        if (host_has)
            continue;
        int n = g_tribe_extra_seen.count(t) ? g_tribe_extra_seen[t] + 1 : 1;
        if (n < 2 || removed >= 2) {
            seen[t] = n;
            continue;
        }
        removed++;
        g_applying = true;
        thiscall<void>(a_DestroyInst, nm(), t);
        g_applying = false;
        g_tribes_removed++;
        H->log(M, SK_INFO, "civ: the old tribe village at (%.0f %.0f %.0f) is gone on the host: removed here (BUG-0539)", p.x,
               p.y, p.z);
    }
    g_tribe_extra_seen = std::move(seen);
}

static void tribe_count_log(double now)
{
    static double last = -100;
    static int known = -1;
    if (now - last < 5)
        return;
    last = now;
    int n = 0;
    game::for_each_object([](void*, uint32_t noun, void* u) {
        if (noun == kTribeNoun)
            ++*(int*)u;
        return true;
    }, &n);
    if (n != known)
        H->log(M, SK_INFO, "civ: tribes of the Tribe stage in this game: %d (was %d, cities %u)", n, known,
               (unsigned)all_cities().size());
    known = n;
}

// host: a tool stops (cToolManager::OnMouseUp): a listed shooter's entry says so (BUG-0530)
using ToolMouseUp_t = int(SK_THISCALL*)(void*, void*);
static ToolMouseUp_t o_ToolMouseUp;
static int SK_THISCALL h_ToolMouseUp(void* mgr, void* tool)
{
    if (g_fix_shot_release && g_fix_shot_fired && g_fix_shots && tool && is_host() && active() &&
        !IsBadReadPtr(tool, 0x118)) {
        void* owner = *(void**)((uint8_t*)tool + 0x114);
        void* o = owner ? (uint8_t*)owner - kVSpatial : nullptr;
        auto it = o ? g_hv.find(o) : g_hv.end();
        std::pair<uint16_t, uint32_t> key{0, 0};
        bool known = false;
        if (it != g_hv.end()) {
            key = {it->second.id, 0u};
            known = true;
        } else if (o && !IsBadReadPtr(o, 0x40) && vtable_in_game(o) && game::noun_id(o) == kTurretNoun) {
            net::Vec3 at;
            if (game::object_position(o, at)) {
                key = {0, pos_key(at)};
                known = true;
            }
        }
        auto e = known ? g_shots.find(key) : g_shots.end();
        if (e != g_shots.end() && !e->second.stopped) {
            e->second.stopped = true;
            e->second.seen = H->time_seconds(); // in the lists a while longer, so the guests hear of it
            g_shot_new = true;
            g_shot_stops_seen++;
        }
    }
    return o_ToolMouseUp(mgr, tool);
}

// guest: the weapon of a local shooter (combatant vt 0x14, as cCombatSimulator::Update gets it)
static void* weapon_of(void* comb)
{
    if (!comb || IsBadReadPtr(comb, 8) || IsBadReadPtr(*(void**)comb, 0x18))
        return nullptr;
    if (g_fix_turret_weapon && !(vtable_in_game(comb) && code_in_game((*(uintptr_t**)comb)[0x14 / 4]))) {
        // BUG-0688: no game combatant (a shooter read at the wrong offset): skipped, logged once per word
        static std::unordered_set<uintptr_t> logged;
        g_bad_comb++;
        uintptr_t vt = *(uintptr_t*)comb;
        if (logged.size() < 8 && logged.insert(vt).second)
            H->log(M, SK_WARN, "civ: a shooter's combatant %p is no game object (vtable %08X): its shot is not copied (BUG-0688)",
                   comb, (unsigned)vt);
        return nullptr;
    }
    void* t = vcall<void*>(comb, 0x14 / 4);
    return t && !IsBadReadPtr(t, 0x120) ? t : nullptr;
}

// guest: objects by noun near a place, all found in one walk of the noun list (turret shooters, building targets)
struct Want {
    uint32_t noun;
    net::Vec3 pos;
    float best;
    void* found;
};
static void find_wanted(std::vector<Want>& w)
{
    if (w.empty())
        return;
    game::for_each_object(
        [](void* o, uint32_t n, void* u) {
            auto& w = *(std::vector<Want>*)u;
            net::Vec3 p;
            bool have = false;
            for (auto& x : w) {
                if (x.noun != n)
                    continue;
                if (!have && !(game::object_alive(o) && game::object_position(o, p)))
                    return true;
                have = true;
                float d = net::dist(p, x.pos);
                if (d < x.best) {
                    x.best = d;
                    x.found = o;
                }
            }
            return true;
        },
        &w);
}

// BUG-0530: this game's copy of a host's weapon stops (vehicles; a turret's copy stops when its entry ends only
// if found again, which needs the noun walk: turrets are military, no AoE)
static void release_copy(Shot& s)
{
    s.released = true;
    if (!g_fix_shot_release || !a_ToolMouseUp || s.kind != 1)
        return;
    auto g = g_gv.find(s.id);
    void* o = g == g_gv.end() ? nullptr : g->second.v;
    if (!o || !exists(o) || F<uint8_t>(o, kVDead))
        return;
    void* tool = weapon_of((uint8_t*)o + kVCombatant);
    if (!tool || !F<uint8_t>(tool, 0x80)) // +0x80: firing (FireTool sets it, OnMouseUp clears it)
        return;
    void* mgr = skg::cdecl_call<void*>(a_ToolMgrGet);
    if (!mgr)
        return;
    g_in_guest_fire = true;
    thiscall<int>(a_ToolMouseUp, mgr, tool);
    g_in_guest_fire = false;
    g_shot_released++;
}

static void guest_fire(double now)
{
    shots_log(now);
    tribe_count_log(now);
    if (!g_fix_shots || !a_FireTool || !a_ToolMgrGet || local_play()) {
        g_shots_in.clear();
        return;
    }
    std::vector<Shot*> due;
    std::vector<Want> want; // per due shot: [2i] its turret (kind 3), [2i + 1] its target (tkind 3)
    for (auto it = g_shots_in.begin(); it != g_shots_in.end();) {
        Shot& s = it->second;
        if (now > s.seen) {
            if (!s.released)
                release_copy(s); // the host's list no longer has it: its weapon stopped there
            it = g_shots_in.erase(it);
            continue;
        }
        ++it;
        if (s.stopped && !s.released) {
            g_shot_stops_seen++;
            release_copy(s);
        }
        if (now - s.tried < kShotFireEvery || (s.counted && s.done == s.count))
            continue;
        s.tried = now;
        due.push_back(&s);
        want.push_back({s.kind == 3 ? kTurretNoun : 0u, s.at, 6.f, nullptr});
        want.push_back({s.tkind == 3 ? s.tnoun : 0u, s.tpos, 4.f, nullptr});
    }
    bool any = false;
    for (auto& w : want)
        any |= w.noun != 0;
    if (any)
        find_wanted(want);
    for (size_t i = 0; i < due.size(); i++) {
        Shot& s = *due[i];
        void* o = nullptr;
        void* comb = nullptr;
        if (s.kind == 1) {
            auto g = g_gv.find(s.id);
            o = g == g_gv.end() ? nullptr : g->second.v;
            if (o && (!exists(o) || F<uint8_t>(o, kVDead)))
                o = nullptr;
            comb = o ? (uint8_t*)o + kVCombatant : nullptr;
        } else {
            o = want[2 * i].found;
            comb = o ? (uint8_t*)o + shooter_comb_off(3) : nullptr;
        }
        if (!o) {
            g_shots_nomirror++;
            continue;
        }
        void* tool = weapon_of(comb);
        void* mgr = tool ? skg::cdecl_call<void*>(a_ToolMgrGet) : nullptr;
        if (!mgr) {
            g_shots_notool++;
            continue;
        }
        if (s.kind == 3)
            g_turret_shooters.insert(o);
        // the target in this game (the projectile homes on it as the host's does); its place is the aim
        void* t = nullptr;
        if (s.tkind == 1) {
            auto g = g_gv.find(s.tid);
            t = g == g_gv.end() ? nullptr : g->second.v;
            if (t && (!exists(t) || F<uint8_t>(t, kVDead)))
                t = nullptr;
        } else if (s.tkind == 3)
            t = want[2 * i + 1].found;
        // BUG-0534: the host's shot had a target and this game has none for it: no FireTool. The game's
        // weapon update (0xCB5FA0, GOG 2024) reads the target of a tool's shot without a null check
        // (`mov eax,[edi+0x50]` with edi = 0) and faulted every frame for a beam with no target.
        if (s.tkind && !t && g_fix_shot_target) {
            g_shots_notarget++;
            continue;
        }
        net::Vec3 ap = s.aim;
        if (s.tkind)
            g_shots_tgt_wanted++;
        if (t) {
            g_shots_tgt_found++;
            net::Vec3 tp;
            if (game::object_position(t, tp)) // the host aimed at its own copy's place: the same offset here
                ap = {s.aim.x + tp.x - (s.tkind == 3 ? s.tpos.x : tp.x), s.aim.y + tp.y - (s.tkind == 3 ? s.tpos.y : tp.y),
                      s.aim.z + tp.z - (s.tkind == 3 ? s.tpos.z : tp.z)};
        }
        float aim[3] = {ap.x, ap.y, ap.z};
        g_in_guest_fire = true;
        int fired = thiscall<int>(a_FireTool, mgr, tool, (uint8_t*)o + kVSpatial, (const float*)aim, -1, 0,
                                  t ? (void*)((uint8_t*)t + kVSpatial) : nullptr, 0);
        g_in_guest_fire = false;
        if (fired & 0xFF) { // FireTool returns a bool in al (BUG-0530)
            note_tool(tool);
            s.done++;
            g_shots_fired++;
            g_turret_fired += s.kind == 3;
        } else
            g_shots_refused++;
    }
}

// guest: a hit from a mirror's (or a fired-from turret's) weapon: the shot is the host's, its damage too
static bool g_fix_shot_damage = true;
COOP_FIX("civ.shot_damage", "BUG-0477", g_fix_shot_damage,
         "civ: the host's shots a guest's game shows hurt nothing there (the host's state brings the damage)");
static uint32_t g_hits_guest, g_hits_noatt; // guest: TakeDamage calls in Civ seen by the hooks, without an attacker
static uintptr_t g_hit_att_vt;               // the vtable of the last attacker (diagnostics)
static bool cosmetic_hit(void* att)
{
    if (game::mode_id() == modes::kSpace) // BUG-0677: a copy of a host ship's shot (a mirror's) hurts no building here
        return squadron::shot_is_cosmetic(att);
    if (is_guest() && game::mode_id() == modes::kCiv) {
        g_hits_guest++;
        if (!att || IsBadReadPtr(att, 4))
            g_hits_noatt++;
        else
            g_hit_att_vt = *(uintptr_t*)att;
    }
    if (!g_fix_shots || !g_fix_shot_damage || !att || !is_guest() || game::mode_id() != modes::kCiv || local_play() || IsBadReadPtr(att, 4) ||
        IsBadReadPtr(*(void**)att, 0x60))
        return false;
    void* v = vcall<void*>(att, 0x5C / 4, 0x137E8E0u); // the vehicle behind a combatant
    bool ours = (v && g_gv_of.count(v)) || g_turret_shooters.count((uint8_t*)att - shooter_comb_off(3));
    if (ours)
        g_hits_dropped++;
    return ours;
}
bool hit_is_cosmetic(void* attacker) { return cosmetic_hit(attacker); }

using VTakeDamage_t = int(SK_THISCALL*)(void*, float, uint32_t, int, const void*, void*);
static VTakeDamage_t o_VTakeDamage;
static int SK_THISCALL h_VTakeDamage(void* s, float d, uint32_t pol, int t, const void* dir, void* a)
{
    if (cosmetic_hit(a))
        return 0;
    return o_VTakeDamage(s, d, pol, t, dir, a);
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

enum : uint8_t { CMD_ORDER = 0, CMD_MOVE = 1, CMD_BUY = 2, CMD_BUILD = 3, CMD_GIVE = 4, CMD_SPACE_BUILD = 5 };
// (also 5 the superweapon request, 6 CMD_BUYCITY (BUG-0877) and 7 CMD_CAPTURE (BUG-0878), written as numbers
// where they are sent above this line)

static void say_host_away(const char* what); // BUG-0693
static bool local_play();
static void send_cmd(net::Writer& w)
{
    if (local_play())
        say_host_away("your command");
    C.sess->send_reliable_to(0, net::MSG_CIV_CMD, w);
    g_cmds_sent++;
}

using SetOrder_t = int(SK_THISCALL*)(void*, void*, int, int);
static SetOrder_t o_SetOrder;
struct LastOrder {
    int type = -1;
    uint8_t kind = 0;
    uint16_t id = 0;
    uint32_t noun = 0;
};
static LastOrder g_last_order;
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
            Target et = encode_target(target);
            g_last_order = {type, et.kind, et.id, et.noun}; // BUG-0648: what the guest's game ordered
            write_target(w, et);
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

// BUG-0693: a guest's command that reaches a host whose Civ is not running (an editor, a menu, a load) is held and run
// when the host is back in the same Civ world (net/held_cmds.h); the guest is told that it waits. Off: dropped (0.7.10).
static bool g_fix_cmd_hold = true;
COOP_FIX("civ.cmd_hold", "BUG-0693", g_fix_cmd_hold,
         "civ: a guest's purchase, planner building, gift or superweapon request made while the host is away runs when it is back");
static net::HeldCommands g_cmd_held;
PLAYER_STATE("civ.held_cmds", [](uint8_t id) { g_cmd_held.forget(id); }, // BUG-0422 / BUG-0693: a leaver's held commands
             [](uint8_t id) -> size_t {
                 size_t n = 0;
                 for (auto& c : g_cmd_held.q)
                     n += c.from == id;
                 return n;
             });
static uint32_t g_cmd_world; // BUG-0693: the save this game last played the stage in (modes::current_world)
static double g_away_said = -100;
static void say_host_away(const char* what)
{
    double t = H ? H->time_seconds() : 0;
    if (!g_fix_cmd_hold || !C.feed || t - g_away_said < 5)
        return;
    g_away_said = t;
    C.feed(std::string(what) + " waits: " + C.sess->name_of(0) + " (host) is away, it is done when the host is back");
}

static void request_buy(void* city, int loco)
{
    if (local_play())
        say_host_away("your purchase");
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

// BUG-0189: `coop.fix civ.planner_send off` keeps a guest's planner placement in the guest's own game (the twin
// of civ_planner_build: the host never gets the building)
static bool g_fix_planner_send = true;
COOP_FIX("civ.planner_send", "BUG-0189", g_fix_planner_send,
         "civ: a building a guest places in its city planner is built in the host's game too");

// BUG-0751: in Space a colony's city planner is the same cCommunityEditor, but the civ module is off there (active()
// is Civ only, Space cities have no host index), so a building a guest bought in its colony's planner stayed in the
// guest's game. The guest now sends it with its planet (CMD_SPACE_BUILD); the host builds it in its city next to that
// spot when its own ship is on the same planet (the cities of a planet exist only while it is the active planet).
// Off: the guest's own game only (0.7.11).
static bool g_fix_space_planner_send = true;
COOP_FIX("civ.space_planner_send", "BUG-0751", g_fix_space_planner_send,
         "civ: a building a guest buys in a Space colony's city planner is built in the host's game too (same planet)");
static uint32_t g_space_builds_sent, g_space_builds_in, g_space_builds_away, g_space_builds_failed;
static void send_space_build(void* city, void* b);
// BUG-0766: a guest's colony building for a planet the host's ship is not on was dropped by the host ("not built:
// this game is at ..."): the guest had it and had paid, the host's world (and every later share) never did. The
// host now keeps it and builds it when its ship is on that planet (its cities exist only while it is the active
// planet), 5 s after it came there. Off: dropped (0.7.11).
static bool g_fix_space_build_hold = true;
COOP_FIX("civ.space_build_hold", "BUG-0766", g_fix_space_build_hold,
         "civ: a guest's Space colony building waits on the host until the host's ship is on that planet");
// BUG-0884: in Space the host's own colony planner building, and the one it builds for a guest, went to nobody
// (h_AddBuilding's relay is Civ only): the other guests' colony cities never had it. The host now sends it to every
// guest but the one it came from (CMD_SPACE_BUILD, host -> guest); a guest builds it in its city there (not paid)
// when its ship is on that planet, or keeps it until it is. Off: the host's (and the buyer's) game only (0.7.11).
static bool g_fix_space_planner_relay = true;
COOP_FIX("civ.space_planner_relay", "BUG-0884", g_fix_space_planner_relay,
         "civ: a Space colony building the host gets is built in the other guests' games too");
// BUG-0902: the guest's BUG-0751 send went at the planner's AddBuilding; a drop that does not end on a free plot adds
// the building and takes it away again, so the host built (unpaid) a house the guest's planner never kept. The guest
// now sends it as the host relays its own (BUG-0884): once the planner closed with the building still in the city.
// Off: sent at AddBuilding (0.7.11).
static bool g_fix_space_planner_closed = true;
COOP_FIX("civ.space_planner_closed", "BUG-0902", g_fix_space_planner_closed,
         "civ: a guest's Space colony planner building goes to the host only when the planner closed with it");
static uint32_t g_space_planner_dropped; // planner buildings taken away again before the planner closed (not sent)
// BUG-0901: a colony city is made again from its cCityData (city +0x2F0; buildings +0x7C, 0x38 bytes each) when its
// planet becomes active (0xFF6900); the game writes the live city into it through City_WriteCityData (0xBD9FA0: the
// planner's exit 0xD100B0, a building's damage, turrets), cCity::AddBuilding does not. A colony building the mod built
// (build_in_city: BUG-0751 / 0766 / 0884) was gone after that game's ship left the planet and landed again (lab
// 20261110-201410). Now the city is written after it. Off: not written (0.7.11).
static bool g_fix_space_city_data = true;
COOP_FIX("civ.space_city_data", "BUG-0901", g_fix_space_city_data,
         "civ: a Space colony building the mod builds is written into the city's data (kept when the planet is left)");
static uintptr_t a_WriteCityData;
static uint32_t g_space_city_written;
static void space_write_city(void* city)
{
    if (!g_fix_space_city_data || !a_WriteCityData || !city || !F<void*>(city, 0x2F0))
        return;
    thiscall<void>(a_WriteCityData, city); // fastcall(ecx = city), plain ret: as thiscall with no stack arguments
    g_space_city_written++;
}
static void space_note_host_build(void* city, void* b, int except, bool planner = false);
static void space_tick(double now);

using AddBuilding_t = int(SK_THISCALL*)(void*, void*, int);
static AddBuilding_t o_AddBuilding;
static int SK_THISCALL h_AddBuilding(void* city, void* b, int x)
{
    void* ret = __builtin_return_address(0);
    int r = o_AddBuilding(city, b, x);
    if (!active() && !g_applying && b && is_guest() && game::mode_id() == modes::kSpace &&
        (uintptr_t)ret == a_PlannerAddBuildingRet && g_fix_space_planner_send) {
        if (g_fix_space_planner_closed)
            space_note_host_build(city, b, -1, true); // BUG-0902: sent by space_tick once the planner kept it
        else
            send_space_build(city, b); // BUG-0751
    }
    if (!active() && !g_applying && b && is_host() && game::mode_id() == modes::kSpace &&
        (uintptr_t)ret == a_PlannerAddBuildingRet && g_fix_space_planner_relay)
        space_note_host_build(city, b, -1, true); // BUG-0884: the host's own planner building
    if (!active() || g_applying || !b)
        return r;
    net::Vec3 pos;
    game::object_position(b, pos);
    uint32_t noun = game::noun_id(b);
    if (is_host()) {
        broadcast_building(city, noun, pos); // every building the host gets appears on the guests
    } else if (is_guest() && (uintptr_t)ret == a_PlannerAddBuildingRet && g_fix_planner_send) {
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
// BUG-0624: an instant founding of the host (SpawnNextCity(ex, 1, 0): the campaign's shortcut, test tools) was not
// broadcast: guests found it only through the catch-up (2 s short, then a founding with the appearance timer, ~3 s
// at 25 fps), so the city and every vehicle of its nation were missing there 4-7 s and the sync-check reported
// "v: 1 missing on the guest" whenever the host ran at low fps (civ_city_layout, 3 of 3 on a loaded PC). Now the
// host broadcasts it too (noun 1 = instant) and a guest founds it instantly at once; the catch-up founds instantly
// as well (the city is late already). Off: not broadcast, the catch-up with the timer (0.7.8).
static bool g_instant_found_fix = true;
COOP_FIX("civ.instant_found", "BUG-0624", g_instant_found_fix,
         "civ: a city the host founds instantly is founded instantly on the guests at once, not by the catch-up seconds later");
constexpr uint32_t kFoundInstant = 1; // MSG_CIV_BUILD kNextCitySite noun: the host founded it instantly
static bool SK_THISCALL h_SpawnNextCity(void* ex, int instant, void* who)
{
    if (!g_applying && is_guest() && game::mode_id() == modes::kCiv) {
        g_cities_blocked++;
        return false; // both callers ignore the result
    }
    bool r = o_SpawnNextCity(ex, instant, who);
    if (r && is_host() && game::mode_id() == modes::kCiv && !who && (!instant || g_instant_found_fix)) {
        net::Writer w;
        w.u8(kNextCitySite);
        w.u32(instant ? kFoundInstant : 0);
        w.f32(0); w.f32(0); w.f32(0);
        C.sess->broadcast_reliable(net::MSG_CIV_BUILD, w);
        g_cities_founded++;
    }
    return r;
}

// BUG-0494: roads between cities (noun 0x2B8A4E7 "inter_city_road") are the ground objects of trade
// routes: cCity::AddTradeRoute (thiscall(city, other, type) ret 8) pushes {partner, route object, type}
// (stride 0xC) on the vector +0x55C of both cities and, for type 0, makes the road. The host's game makes
// routes in its own AI (an economic vehicle's "create route", diplomacy); the guest's mirrors run none of
// it, so the host's roads never appeared there. Now the host sends its route list (MSG_CIV_BUILD with the
// city index kRoadList) on every change and every 10 s, and a guest adds the routes it lacks with the
// game's own AddTradeRoute. Routes are only added: the game removes them itself in ChangeOwner (a capture,
// mirrored), and RemoveTradeRoute (one side) of a shared route object is not proven safe.
// `coop.fix civ.roads off`: no list, the guest keeps its own routes (0.7.4).
constexpr uint8_t kRoadList = 0xFE; // MSG_CIV_BUILD city index: the host's trade routes follow
constexpr uintptr_t kCityRoutes = 0x55C; // vector<{cCity* partner, route object*, int type}>, stride 0xC
static bool g_fix_roads = true;
COOP_FIX("civ.roads", "BUG-0494", g_fix_roads, "civ: roads (trade routes) between cities on the host appear on the guests");
static uintptr_t a_AddTradeRoute;
static uint32_t g_roads_sent, g_roads_in, g_roads_added, g_roads_failed;
static double g_roads_sent_at = -100;
static std::vector<std::array<uint8_t, 3>> g_roads_last; // host: the list sent last

struct Route {
    void* partner;
    void* obj;
    int type;
};
static std::vector<Route> routes_of(void* city)
{
    std::vector<Route> r;
    auto* b = F<uint8_t*>(city, kCityRoutes);
    auto* e = F<uint8_t*>(city, kCityRoutes + 4);
    if (!b || e < b || (e - b) % 0xC || (e - b) > 0xC * 64)
        return r;
    for (uint8_t* p = b; p < e; p += 0xC)
        r.push_back({*(void**)p, *(void**)(p + 4), *(int*)(p + 8)});
    return r;
}

// host: (index a < index b, type) of every route between two cities of the snapshot's list
static std::vector<std::array<uint8_t, 3>> host_routes()
{
    std::vector<std::array<uint8_t, 3>> out;
    size_t n = std::min<size_t>(g_host_cities.size(), 250);
    for (size_t i = 0; i < n; i++)
        for (const Route& r : routes_of(g_host_cities[i])) {
            auto it = std::find(g_host_cities.begin(), g_host_cities.begin() + n, r.partner);
            size_t j = (size_t)(it - g_host_cities.begin());
            if (j < n && i < j && r.type >= 0 && r.type < 256)
                out.push_back({(uint8_t)i, (uint8_t)j, (uint8_t)r.type});
        }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (out.size() > 200)
        out.resize(200);
    return out;
}

static void host_send_roads(double now)
{
    if (!g_fix_roads || !a_AddTradeRoute)
        return;
    auto r = host_routes();
    if (r == g_roads_last && now - g_roads_sent_at < 10.0)
        return;
    g_roads_last = r;
    g_roads_sent_at = now;
    net::Writer w;
    w.u8(kRoadList);
    w.u8((uint8_t)r.size());
    for (auto& x : r) {
        w.u8(x[0]);
        w.u8(x[1]);
        w.u8(x[2]);
    }
    C.sess->broadcast_reliable(net::MSG_CIV_BUILD, w);
    g_roads_sent++;
}

static bool city_listed(void* c)
{
    auto all = all_cities();
    return c && std::find(all.begin(), all.end(), c) != all.end();
}

static bool has_route(void* a, void* b)
{
    for (const Route& r : routes_of(a))
        if (r.partner == b)
            return true;
    return false;
}

static bool planner_holds(void* city);
static void guest_roads(net::Reader& r)
{
    uint8_t n = r.u8();
    std::vector<std::array<uint8_t, 3>> list;
    for (int i = 0; i < n && r.ok(); i++) {
        std::array<uint8_t, 3> x{r.u8(), r.u8(), r.u8()};
        list.push_back(x);
    }
    if (!r.ok())
        return;
    g_roads_in++;
    if (!g_fix_roads || !a_AddTradeRoute || game::mode_id() != modes::kCiv)
        return;
    for (auto& x : list) {
        void* a = x[0] < g_host_cities.size() ? g_host_cities[x[0]] : nullptr;
        void* b = x[1] < g_host_cities.size() ? g_host_cities[x[1]] : nullptr;
        if (!a || !b || a == b || !city_listed(a) || !city_listed(b) || planner_holds(a) || planner_holds(b) || has_route(a, b))
            continue;
        g_applying = true;
        thiscall<void>(a_AddTradeRoute, a, b, (int)x[2]);
        g_applying = false;
        if (has_route(a, b)) {
            g_roads_added++;
            H->log(M, SK_INFO, "civ: the host's trade route %u-%u (type %u) added here (BUG-0494)", x[0], x[1], x[2]);
        } else if (g_roads_failed++ < 5)
            H->log(M, SK_WARN, "civ: the host's trade route %u-%u (type %u) did not take here", x[0], x[1], x[2]);
    }
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
using Capture_t = int(SK_THISCALL*)(void*, uint32_t, int, int, int, int);
static Capture_t o_Capture;
// BUG-0775: a capture is credited to the player whose hit took the city (the capture runs inside that hit:
// building / hall TakeDamage -> AddCaptureDamage -> CheckMilitaryCapture -> Capture), else the player who hit it
// last within the joint-siege window; the nearest vehicle only when neither is known (a conversion, a test).
// `coop.fix civ.capture_credit off`: always the nearest vehicle (0.7.12).
static bool g_fix_capture_credit = true;
COOP_FIX("civ.capture_credit", "BUG-0775", g_fix_capture_credit,
         "civ: a captured city goes to the player whose vehicles took it, not to the one whose vehicle is nearest");
static int g_capture_for = -1; // host: the player a capture made for a command is credited to (BUG-0877, BUG-0878)
static uint32_t g_credit_by[4]; // host: captures credited by command, hit, siege, nearest vehicle
static uint8_t capture_credit(void* city); // (below the siege)
// BUG-0878: a capture the guest's own game makes while the host is away (local play: the guest's vehicles fight
// in its game) went nowhere: the game showed "won", Capture was refused here and the host never knew. It goes to
// the host as CMD_CAPTURE (held there until the host is back, civ.cmd_hold). `coop.fix civ.away_capture off`: 0.7.12.
static bool g_fix_away_capture = true;
COOP_FIX("civ.away_capture", "BUG-0878", g_fix_away_capture,
         "civ: a city a guest captures while the host is away is captured in the host's game when it is back");
static std::unordered_set<void*> g_away_capture_sent; // guest: cities sent this absence
static uint32_t g_away_captures_sent;
static int SK_THISCALL h_Capture(void* city, uint32_t pol, int type, int a3, int a4, int a5)
{
    if (is_guest() && game::mode_id() == modes::kCiv && !g_applying) {
        void* pc = player_civ();
        if (g_fix_away_capture && local_play() && pc && pol == pol_of(pc) && !g_away_capture_sent.count(city)) {
            int ci = host_index_of_city(city);
            if (ci >= 0) {
                g_away_capture_sent.insert(city);
                g_away_captures_sent++;
                net::Writer w;
                w.u8(7 /* CMD_CAPTURE */);
                w.u8((uint8_t)ci);
                w.u8((uint8_t)type);
                H->log(M, SK_INFO, "civ: this game captured city %d while the host is away: sent to the host (BUG-0878)", ci);
                send_cmd(w);
            }
        } else if (!local_play())
            g_away_capture_sent.clear();
        return 0; // captures happen on the host and come back with the snapshot
    }
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
            uint8_t credit = capture_credit(city);
            if (credit != 0xFF)
                owner = credit;
            else
                g_credit_by[3]++;
            g_city_owner[city] = owner;
            g_city_captured.insert(city); // BUG-0875 (used with civ.capture_keeps)
            g_capture_by[city] = {owner, H->time_seconds()};
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
PLAYER_STATE("civ.siege", [](uint8_t id) { for (auto& [c, m] : g_siege) m.erase(id); }, // BUG-0422
             [](uint8_t id) -> size_t {
                 size_t n = 0;
                 for (auto& [c, m] : g_siege)
                     n += m.count(id);
                 return n;
             });
static std::unordered_set<void*> g_joint_told;
static uint32_t g_joint_hits, g_military_hits, g_religious_boosts, g_alarms, g_votes;
static bool g_siege_players_fix = true;
COOP_FIX("civ.siege_players", "BUG-0420", g_siege_players_fix, "civ: an NPC nation's army attacking the same city is not a second player of a joint siege");
static uint32_t g_siege_npc;

static uint8_t capture_credit(void* city) // 0xFF: not known (the nearest vehicle then)
{
    auto here = [](int p) { return p >= 0 && p < net::kMaxPlayers && net::is_player(*C.sess, (uint8_t)p); };
    if (here(g_capture_for)) { // a guest's purchase or away capture (or the host's own purchase)
        g_credit_by[0]++;
        return (uint8_t)g_capture_for;
    }
    if (!g_fix_capture_credit)
        return 0xFF;
    if (here(g_hit_by)) { // the hit that took it
        g_credit_by[1]++;
        return g_hit_by;
    }
    auto it = g_siege.find(city);
    if (it == g_siege.end())
        return 0xFF;
    double now = H->time_seconds(), best = -1;
    uint8_t who = 0xFF;
    for (auto& [p, t] : it->second)
        if (here(p) && now - t < kJointWindow && t > best) {
            best = t;
            who = p;
        }
    if (who != 0xFF)
        g_credit_by[2]++;
    return who;
}

// BUG-0877: the purchase of an NPC city (cCivilization::FinishBuyCity -> cCity buy 0xBE99A0: SpendMoney, then
// Capture type 2). On a guest the money went (its spending reaches the shared money) and the Capture was refused:
// the city changed hands nowhere. A guest's purchase now goes to the host as CMD_BUYCITY and the host's game buys
// it (its money, its Capture), credited to that guest. `coop.fix civ.buy_city off`: 0.7.12.
static bool g_fix_buy_city = true;
COOP_FIX("civ.buy_city", "BUG-0877", g_fix_buy_city,
         "civ: a guest's purchase of another nation's city is made in the host's game and the city is that guest's");
using CityBuy_t = void(SK_THISCALL*)(void*, uint32_t, float);
static CityBuy_t o_CityBuy;
static uint32_t g_city_buys_sent, g_city_buys_done, g_city_buys_refused;
static void SK_THISCALL h_CityBuy(void* city, uint32_t pol, float price)
{
    void* pc = g_fix_buy_city && game::mode_id() == modes::kCiv && !g_applying ? player_civ() : nullptr;
    if (pc && pol == pol_of(pc) && is_guest()) {
        int ci = host_index_of_city(city);
        if (ci < 0) {
            if (C.notice)
                C.notice("this city is not in the host's game: not bought");
            return;
        }
        if (local_play())
            say_host_away("your purchase of the city");
        net::Writer w;
        w.u8(6 /* CMD_BUYCITY */);
        w.u8((uint8_t)ci);
        w.f32(price);
        send_cmd(w);
        g_city_buys_sent++;
        H->log(M, SK_INFO, "civ: this game buys city %d (price %.0f): sent to the host (BUG-0877)", ci, price);
        return;
    }
    if (pc && pol == pol_of(pc) && is_host() && g_capture_for < 0) { // the host's own purchase is the host's
        g_capture_for = C.sess->my_id();
        o_CityBuy(city, pol, price);
        g_capture_for = -1;
        return;
    }
    o_CityBuy(city, pol, price);
}

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

// F3 (BUG-0768): civ.roles off: nobody has a role, no role bonus
static uint8_t role(uint8_t p) { return C.role_of && p != 0xFF && settings::on(settings::kCivRoles) ? C.role_of(p) : 0; }

// building / city hall TakeDamage (this = building+0x120): a military governor's vehicles hit harder;
// the player is remembered for the capture damage the original adds
using BTakeDamage_t = int(SK_THISCALL*)(void*, float, uint32_t, int, const void*, void*);
static BTakeDamage_t o_BTakeDamage, o_HallTakeDamage;
static int b_damage(BTakeDamage_t orig, void* self, float dmg, uint32_t pol, int type, const void* dir, void* att)
{
    if (cosmetic_hit(att)) // BUG-0477: a relayed shot of the host's
        return 0;
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
    // BUG-0420: the owner of an NPC nation's vehicle is 0xFE: it counted as a second player (one guest and an NPC
    // army gave the joint-siege bonus). Only a player's vehicles count now; `coop.fix civ.siege_players off`: 0.7.10.
    bool player = g_hit_by != 0xFF && (!g_siege_players_fix || g_hit_by < net::kMaxPlayers);
    if (!player && g_hit_by != 0xFF)
        g_siege_npc++;
    if (is_host() && active() && city && player) {
        double now = H->time_seconds();
        auto& hits = g_siege[city];
        hits[g_hit_by] = now;
        int players = 0;
        for (auto& [p, t] : hits)
            if (now - t < kJointWindow)
                players++;
        if (players >= 2 && settings::on(settings::kCivSiege)) { // F3 (BUG-0768): civ.joint_siege
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
    uint32_t pol = 0; // the vehicle's nation when it was boosted (BUG-0773: restore only on that vehicle)
};
static std::unordered_map<void*, Boost> g_boost;
static std::unordered_map<void*, double> g_alarm_at;
// BUG-0771 (GitHub #199): the next alarm of a city under siege comes when the last boost has just run out (both
// 20 s). A hit in the game's update after `until` and before this frame's boost_tick found the entry expired but not
// yet restored and read the boosted +0x218 as the vehicle's own speed: x1.5 per 40 s of a siege ("like flash").
// With the fix a vehicle's own speed is read once, when its entry is made; re-arming only moves `until`.
static bool g_fix_boost_base = true;
COOP_FIX("civ.boost_base", "BUG-0771", g_fix_boost_base,
         "civ: the city alarm boost keeps each vehicle's own speed, a new alarm at the boost's end does not stack it");
// BUG-0773: a boost entry dropped while its vehicle lives (out of the host's table, a world change the stage
// adapter saw while the vehicles were still there) put nothing back: the vehicle kept 1.5x (or more) for good
static bool g_fix_boost_restore = true;
COOP_FIX("civ.boost_restore", "BUG-0773", g_fix_boost_restore,
         "civ: a boosted vehicle gets its own speed back when the mod forgets the boost while the vehicle lives");
// BUG-0876: the alarm boosted every vehicle near the city that was not its owner's, NPC nations' too (owner 0xFE):
// the army attacking the city drove faster as well
static bool g_fix_boost_players = true;
COOP_FIX("civ.boost_players", "BUG-0876", g_fix_boost_players,
         "civ: the city alarm boosts only the other players' vehicles, not NPC nations' (the attackers)");
static uint32_t g_boost_restored; // tests: boosts put back on a living vehicle that left the tables (BUG-0773)
// the vehicle still carries the boost the mod wrote (not the game's own value after a RecalcStats, not another
// vehicle at a reused address)
static bool boost_ours(void* v, const Boost& b)
{
    return exists(v) && pol_of(v) == b.pol && F<float>(v, 0x218) == b.orig * 1.5f;
}

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
        if (g_fix_boost_players && h.owner >= 0xFE) // BUG-0876: an NPC nation's vehicle (the attackers)
            continue;
        auto [bi, fresh] = g_boost.try_emplace(v);
        Boost& b = bi->second;
        if (g_fix_boost_base ? fresh : b.until < now) { // BUG-0771: the own speed once per entry
            b.orig = F<float>(v, 0x218);
            b.pol = pol_of(v);
        }
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
            if (g_fix_boost_restore && boost_ours(v, it->second)) { // BUG-0773: it lives, out of the table only
                F<float>(v, 0x218) = it->second.orig;
                g_boost_restored++;
            }
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
static unsigned g_sw_unvoted = 0; // superweapons fired without a vote (civ.superweapon_vote off; coop.civ sw_unvoted)
static int g_sw_last_req = -1, g_sw_fired = -1; // host: last guest request, weapon type the last guest vote fired
static bool SK_THISCALL h_FireSW(void* civ, int type, const float* target)
{
    // F3 (BUG-0768): civ.superweapon_vote off: the host's (and a guest's own) superweapon fires at once
    if (g_sw_bypass || !active() || C.sess->players().size() < 2)
        return o_FireSW(civ, type, target);
    if (is_host() && !settings::on(settings::kCivSwVote)) {
        g_sw_unvoted++;
        return o_FireSW(civ, type, target);
    }
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
    if (!settings::on(settings::kCivSwVote)) { // F3 (BUG-0768): no vote, the guest's superweapon fires now
        g_sw_bypass = true;
        g_sw_fired = type;
        o_FireSW(pc, type, pos);
        g_sw_bypass = false;
        g_sw_unvoted++;
        return;
    }
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

// ---- BUG-0484: city turrets ----
// A city's turrets (cCity +0x354, noun 0x436F342) are made by City_AddTurret 0xBE39A0 (the next free turret slot):
// the city growth 0xBE5890 buys them from the city's budget (two calls), so a guest's game, whose growth runs
// on its own copy of an NPC city, put turrets there that the host's never had (and the other way round), and
// the snapshot carried none. Now the guest's growth makes none (like its buildings, CivGrowth_*_Ret) and the
// host sends the places of one city's turrets per snapshot cycle (chunk 0xFB, when they changed and every 3 s);
// a guest adds the ones it lacks with City_AddTurret and removes the ones the host does not have (two looks in
// a row, as reconcile_buildings).
static bool g_fix_turrets = true;
COOP_FIX("civ.turrets", "BUG-0484", g_fix_turrets,
         "civ: a city's defence turrets are the host's on every guest (a guest's own city growth makes none)");
constexpr uint8_t kTurretChunk = 0xFB;
constexpr uintptr_t kCityTurrets = 0x354;
static uintptr_t a_AddTurret, a_RemoveTurret, a_TurretRet1, a_TurretRet2;
using AddTurret_t = int(__fastcall*)(void*);
static AddTurret_t o_AddTurret;
static uint32_t g_turrets_blocked, g_turrets_added, g_turrets_removed, g_turret_tx, g_turret_rx;
static std::unordered_map<void*, std::pair<uint32_t, double>> g_turret_sent; // host: city -> hash, when
static std::unordered_map<void*, int> g_turret_seen;                          // guest: city -> mismatches in a row
static int __fastcall h_AddTurret(void* city)
{
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    if (g_fix_turrets && !g_applying && is_guest() && game::mode_id() == modes::kCiv && !local_play() &&
        (ret == a_TurretRet1 || ret == a_TurretRet2)) {
        g_turrets_blocked++;
        return 0; // both growth calls ignore the result (the host's turrets come with the snapshot)
    }
    return o_AddTurret(city);
}
static std::vector<std::pair<void*, net::Vec3>> turrets_of(void* city)
{
    std::vector<std::pair<void*, net::Vec3>> out;
    for (void* t : vec_of((uint8_t*)city + kCityTurrets)) {
        net::Vec3 p;
        if (t && game::object_position(t, p) && out.size() < 40)
            out.push_back({t, p});
    }
    return out;
}
static void host_send_turrets(size_t ci, void* city, double now)
{
    if (!g_fix_turrets || !a_AddTurret)
        return;
    auto ts = turrets_of(city);
    uint32_t h = 2166136261u + (uint32_t)ts.size();
    for (auto& [t, p] : ts)
        h = (h ^ pos_key(p)) * 16777619u;
    auto& last = g_turret_sent[city];
    if (last.first == h && now - last.second < 3.0)
        return;
    last = {h, now};
    net::Writer w;
    w.u16(0); w.u8(kTurretChunk); w.u8(0);
    w.u8((uint8_t)ci);
    w.u8((uint8_t)ts.size());
    for (auto& [t, p] : ts) {
        w.f32(p.x); w.f32(p.y); w.f32(p.z);
    }
    for (auto& pl : C.sess->players())
        if (pl.id != 0)
            C.sess->send_unreliable_to(pl.id, net::MSG_CIV_STATE, w);
    g_turret_tx++;
}
static void guest_turrets(net::Reader& r)
{
    uint8_t ci = r.u8(), n = r.u8();
    std::vector<net::Vec3> hp(n);
    for (auto& p : hp) {
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
    }
    if (!r.ok())
        return;
    g_turret_rx++;
    void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
    if (!g_fix_turrets || !a_AddTurret || !city || local_play() || planner_holds(city) || game::mode_id() != modes::kCiv)
        return;
    auto local = turrets_of(city);
    std::vector<bool> used(local.size(), false);
    size_t missing = 0;
    for (auto& p : hp) {
        bool found = false;
        for (size_t k = 0; k < local.size() && !found; k++)
            if (!used[k] && net::dist(local[k].second, p) < 6.f)
                used[k] = found = true;
        missing += !found;
    }
    std::vector<void*> extra;
    for (size_t k = 0; k < local.size(); k++)
        if (!used[k])
            extra.push_back(local[k].first);
    int& seen = g_turret_seen[city];
    if (!missing && extra.empty()) {
        seen = 0;
        return;
    }
    if (++seen < 2)
        return;
    seen = 0;
    g_applying = true;
    for (void* t : extra) { // the host has no turret there
        thiscall<void>(a_RemoveTurret, city, t, 0);
        g_turrets_removed++;
    }
    for (size_t i = 0; i < missing; i++) // the next free slot: the order the host's game fills them in
        if (o_AddTurret(city))
            g_turrets_added++;
    g_applying = false;
}

static bool save_filter(void* o, uint32_t noun);
static void save_start();
static void save_end();
// BUG-0549: a guest's mirror vehicle drawn at scale 10 where the host's vehicle has 0.81 (play_civ detector
// `Civilization:scale:vehicle`). The guest's game itself gives the mirror the 10: the model-ready handler of the
// vehicle (0xCA76B0, message 0x2B978C46) runs 0xCA40B0, which sets loco SetScale(10.0) (return address 0xCA41DA,
// loco vtable 0x1474BE8) when it finds its ground object; the host's vehicle never took that path (the
// BUG-0485 diagnostic logged the caller on guests only). The vehicle's scale is a replicated field now
// (fields.h, the class of BUG-0461 / BUG-0475): the host sends its vehicles' scale, a guest's mirror that
// differs gets it. `coop.fix civ.vehicle_scale off`: the guest's own scale (0.7.8).
static bool g_veh_scale_fix = true;
COOP_FIX("civ.vehicle_scale", "BUG-0549", g_veh_scale_fix, "civ: a guest's mirror vehicle has the host vehicle's size");
static const fields::Field kVehFields[] = {fields::kScale};
static void veh_each(void (*cb)(uint16_t, void*, void*), void* u)
{
    auto alive = all_vehicles();
    for (void* v : alive) {
        auto it = g_hv.find(v);
        if (it != g_hv.end() && it->second.id)
            cb(it->second.id, v, u);
    }
}
static uint64_t g_veh_live_frame = ~0ull;
static std::unordered_set<void*> g_veh_live;
static void* veh_mirror(uint16_t id)
{
    auto it = g_gv.find(id);
    if (it == g_gv.end() || !it->second.v)
        return nullptr;
    if (H->frame_index() != g_veh_live_frame) {
        g_veh_live_frame = H->frame_index();
        g_veh_live.clear();
        for (void* v : all_vehicles())
            g_veh_live.insert(v);
    }
    return g_veh_live.count(it->second.v) ? it->second.v : nullptr;
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
    a_WriteCityData = H->sym("SporeCoop::City_WriteCityData"); // optional (BUG-0901, GOG 2024)
    if (uintptr_t kg = H->sym("SporeCoop::Vehicle_KeepOnValidGround")) // optional (BUG-0186, GOG 2024)
        H->hook_create(M, (void*)kg, (void*)h_KeepGround, (void**)&o_KeepGround);
    H->hook_create(M, (void*)a_SetSelected, (void*)h_SetSelected, (void**)&o_SetSelected);
    H->hook_create(M, (void*)a_SetOrder, (void*)h_SetOrder, (void**)&o_SetOrder);
    H->hook_create(M, (void*)a_SetMoveOrder, (void*)h_SetMoveOrder, (void**)&o_SetMoveOrder);
    H->hook_create(M, (void*)a_UIMessage, (void*)h_UIMessage, (void**)&o_UIMessage);
    H->hook_create(M, (void*)a_AddBuilding, (void*)h_AddBuilding, (void**)&o_AddBuilding);
    H->hook_create(M, (void*)a_Capture, (void*)h_Capture, (void**)&o_Capture);
    if (uintptr_t cb = H->sym("SporeCoop::cCity_Buy")) // BUG-0877 (optional)
        H->hook_create(M, (void*)cb, (void*)h_CityBuy, (void**)&o_CityBuy);
    H->hook_create(M, (void*)a_CreateBuilding, (void*)h_CreateBuilding, (void**)&o_CreateBuilding);
    H->hook_create(M, (void*)a_SpawnNextCity, (void*)h_SpawnNextCity, (void**)&o_SpawnNextCity);
    H->hook_create(M, (void*)a_AddMoney, (void*)h_AddMoney, (void**)&o_AddMoney);
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
        {"Simulator::cVehicle::TakeDamage", (void*)h_VTakeDamage, (void**)&o_VTakeDamage}, // BUG-0477
        {"Simulator::cCity::AddCaptureDamage", (void*)h_AddCapture, (void**)&o_AddCapture},
        {"Simulator::cCity::ConvertCitizens", (void*)h_Convert, (void**)&o_Convert},
        {"Simulator::cCity::OnAttacked", (void*)h_OnAttacked, (void**)&o_OnAttacked},
        {"Simulator::cCivilization::FireSuperweapon", (void*)h_FireSW, (void**)&o_FireSW},
    };
    for (auto& o : opts)
        if (uintptr_t a = H->sym(o.name))
            H->hook_create(M, (void*)a, o.hook, o.orig);
    // BUG-0484 (optional): the turret growth of a guest's cities, the host's turrets
    a_AddTurret = H->sym("SporeCoop::City_AddTurret");
    a_RemoveTurret = H->sym("SporeCoop::City_RemoveTurret");
    a_TurretRet1 = H->sym("SporeCoop::CivGrowth_Turret_Ret1");
    a_TurretRet2 = H->sym("SporeCoop::CivGrowth_Turret_Ret2");
    if (a_AddTurret && a_RemoveTurret && a_TurretRet1 && a_TurretRet2)
        H->hook_create(M, (void*)a_AddTurret, (void*)h_AddTurret, (void**)&o_AddTurret);
    else
        a_AddTurret = 0;
    if (uintptr_t di = H->sym("Simulator::cGameNounManager::DestroyInstance")) { // BUG-0539
        gamefixes::set_destroy_listener(on_destroy_instance); // BUG-0623: gamefixes owns the hook
        a_DestroyInst = di;
    }
    a_ToolMouseUp = H->sym("Simulator::cToolManager::OnMouseUp"); // BUG-0530 (optional)
    if (a_ToolMouseUp)
        H->hook_create(M, (void*)a_ToolMouseUp, (void*)h_ToolMouseUp, (void**)&o_ToolMouseUp);
    a_FireTool = H->sym("Simulator::cToolManager::FireTool"); // BUG-0477 (optional)
    a_ToolMgrGet = H->sym("Simulator::cToolManager::Get");
    savestrip::add_filter(save_filter); // BUG-0470
    savestrip::add_save_hooks(save_start, save_end);
    // the city planner (BUG-0491, BUG-0493; GOG 2024 symbols, optional)
    a_DestroyInstance = H->sym("Simulator::cGameNounManager::DestroyInstance");
    a_VehicleRemove = H->sym("SporeCoop::Vehicle_Remove");
    a_AddTradeRoute = H->sym("Simulator::cCity::AddTradeRoute"); // BUG-0494 (optional)
    if ((a_EditorUpdate = H->sym("SporeCoop::CommunityEditor_Update")))
        H->hook_create(M, (void*)a_EditorUpdate, (void*)h_EditorUpdate, (void**)&o_EditorUpdate);
    if (uintptr_t dc = H->sym("SporeCoop::AssetDiscovery_AddCard"))
        H->hook_create(M, (void*)dc, (void*)h_DiscoveryCard, (void**)&o_DiscoveryCard);
    if ((a_EditorPickUp = H->sym("SporeCoop::CommunityEditor_PickUp")))
        H->hook_create(M, (void*)a_EditorPickUp, (void*)h_EditorPickUp, (void**)&o_EditorPickUp);
    fields::add_kind({fields::K_CIV_VEHICLE, "civ_vehicle", &g_veh_scale_fix, kVehFields, 1, veh_each, veh_mirror,
                      [] { return g_ok && game::mode_id() == modes::kCiv; }}); // BUG-0549
    return true;
}

// ---------------- host ----------------
static void host_assign_cities(const std::vector<uint8_t>& players)
{
    void* pc = player_civ();
    if (!pc || players.empty())
        return;
    auto cities = vec_of((uint8_t*)pc + kCivCities);
    // drop cities the player lost
    for (auto it = g_city_owner.begin(); it != g_city_owner.end();) {
        if (std::find(cities.begin(), cities.end(), it->first) == cities.end())
            it = g_city_owner.erase(it);
        else
            ++it;
    }
    if (g_fix_capture_keeps) { // BUG-0875: a capture whose owner change came after its cinematic gets its captor
        double now = H->time_seconds();
        for (auto it = g_capture_by.begin(); it != g_capture_by.end();) {
            bool here = std::find(cities.begin(), cities.end(), it->first) != cities.end();
            if (here && !g_city_owner.count(it->first)) {
                g_city_owner[it->first] = it->second.owner;
                g_city_captured.insert(it->first);
            }
            it = here || now - it->second.at > 120.0 ? g_capture_by.erase(it) : std::next(it);
        }
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
    auto present = [&](uint8_t o) { return std::find(players.begin(), players.end(), o) != players.end(); };
    // BUG-0875: a city its captor (or a gift's receiver) keeps while that player is here
    auto kept = [&](void* c) {
        auto it = g_city_owner.find(c);
        return it != g_city_owner.end() && present(it->second) &&
               (g_city_gifted.count(c) || (g_fix_capture_keeps && g_city_captured.count(c)));
    };
    std::vector<uint8_t> first; // capture_keeps: the players without a city get the free ones first
    if (g_fix_capture_keeps) {
        std::unordered_set<uint8_t> has{0}; // the host: the capital
        size_t free = 0;
        for (size_t i = 1; i < cities.size(); i++)
            if (kept(cities[i]))
                has.insert(g_city_owner[cities[i]]);
            else
                free++;
        for (size_t j = 1; j <= players.size(); j++)
            if (!has.count(players[j % players.size()]))
                first.push_back(players[j % players.size()]);
        shared = first.size() > free; // the capital stays everybody's while a player would have no city
    }
    if (shared != (g_city_owner.count(cities[0]) && g_city_owner[cities[0]] == kShared))
        resplit = true;
    size_t k = 0;
    auto next = [&]() -> uint8_t {
        size_t j = k++;
        return j < first.size() ? first[j] : players[(1 + j - first.size()) % players.size()];
    };
    for (size_t i = 0; i < cities.size(); i++) {
        if (!resplit && g_city_owner.count(cities[i]))
            continue;
        if (kept(cities[i]) && (i > 0 || g_city_gifted.count(cities[i])))
            continue; // a gift (or a capture: capture_keeps) stays with its owner while that player is here
        g_city_owner[cities[i]] = i == 0 ? (shared ? kShared : 0) : next();
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
        if (o != kShared && o != 0xFF)
            owned.push_back({city, o});
    int pairs = 0, gain = 0;
    auto economic = [](uint8_t p) { return role(p) == 2; };
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

// BUG-0493: a guest's building for the city the host's own planner edits (a shared capital) is built
// when that planner closes
struct HeldBuild {
    uint8_t from;
    void* city;
    uint32_t noun;
    net::Vec3 pos;
};
static std::vector<HeldBuild> g_held_builds;
// BUG-0602 diagnostics: the layout slot a building is in (-1: none) and the distance from p to the nearest slot
static int slot_of(void* city, void* b, const net::Vec3& p, float* nearest, void** occ)
{
    uint8_t* sb = F<uint8_t*>(city, kCitySlots);
    uint8_t* se = F<uint8_t*>(city, kCitySlots + 4);
    int in = -1;
    float best = 3.4e38f;
    for (int k = 0; sb && sb + (k + 1) * 0x18 <= se && k < 64; k++) {
        uint8_t* sl = sb + k * 0x18;
        if (F<void*>(sl, 4) == b)
            in = k;
        net::Vec3 q{F<float>(sl, 0xC), F<float>(sl, 0x10), F<float>(sl, 0x14)};
        float d = net::dist(p, q);
        if (d < best) {
            best = d;
            if (occ)
                *occ = F<void*>(sl, 4);
        }
    }
    if (nearest)
        *nearest = best;
    return in;
}
static unsigned g_bfix_logged, g_host_offslot_logged;
static uint32_t g_bfix_off_slot; // BUG-0602: rebuilt here more than 4 m from the host's spot
static std::unordered_set<void*> g_host_offslot_seen;
static void* build_in_city(void* city, uint32_t noun, const net::Vec3& pos, bool pay);
// BUG-0602: a building the host's game has off the layout slot this game gives it (SnapBuildingToSlot takes the
// free slot nearest the host's spot, 8-9 m away in campaign_space): the reconciliation found no building within
// 4 m of the host's, destroyed this one and built it again, hundreds of times (buildings_fixed=+281/-281), and the
// building stood elsewhere in the guest's game. A guest's building made for the host's stands where the host's
// stands (its slot kept: the city's lists and the planner's slot stay the game's). Off: where the slot is (0.7.8).
static bool g_fix_city_layout = true;
COOP_FIX("civ.city_layout", "BUG-0602", g_fix_city_layout,
         "civ: a city building the guest builds for the host's stands where the host's stands, not in the nearest free slot");
static uint32_t g_bfix_moved;
static void to_host_spot(void* b, const net::Vec3& p)
{
    net::Vec3 bp{};
    if (!g_fix_city_layout || !b || !game::object_position(b, bp) || net::dist(bp, p) <= 1.f)
        return;
    float q[3] = {p.x, p.y, p.z};
    vcall<void>((uint8_t*)b + 0x34, 0x38 / 4, (const float*)q); // spatial SetPosition, as build_in_city does
    // its layout slot goes with it (cLayoutSlot +0xC), so the slot nearest the building stays its own (the
    // planner picks a building by that, BUG-0491) and a later snap finds the host's spot
    for (void* c : g_host_cities) {
        uint8_t* sb = c ? F<uint8_t*>(c, kCitySlots) : nullptr;
        uint8_t* se = c ? F<uint8_t*>(c, kCitySlots + 4) : nullptr;
        for (int k = 0; sb && sb + (k + 1) * 0x18 <= se && k < 64; k++)
            if (F<void*>(sb + k * 0x18, 4) == b) {
                F<float>(sb + k * 0x18, 0xC) = p.x;
                F<float>(sb + k * 0x18, 0x10) = p.y;
                F<float>(sb + k * 0x18, 0x14) = p.z;
            }
    }
    g_bfix_moved++;
}

// ---- BUG-0529: the hit points of a city's buildings ----
// The snapshot's building rows (one city per cycle) carry noun, place and the ruined flag: a guest got the
// host's ruins, never the hit points of a building that still stands, so a city the host's vehicles hit stood
// whole on the guest until a building fell. The host now sends the hit points of the same city's buildings
// (MSG_CIV_STATE chunk 0xFD, chunks 0: an older guest drops it) when they changed and every 3 s; the guest
// lowers a building's hit points with the game's combatant damage (no attacker: its own damage look, no
// capture or alarm) and raises them (the host's repair) by setting them.
static bool g_fix_building_hp = true;
COOP_FIX("civ.building_hp", "BUG-0529", g_fix_building_hp,
         "civ: a guest's city buildings have the host's hit points (a city the host's vehicles hit looks hit)");
constexpr uint8_t kHpChunk = 0xFD;
constexpr uint32_t kCityHallNoun = 0x18EA1EB;
static std::unordered_map<void*, std::pair<uint32_t, double>> g_hp_sent; // host: city -> hash of the hit points, when
static uint32_t g_hp_tx, g_hp_rx, g_hp_lowered, g_hp_raised, g_hp_nomatch;
static bool hp_building(void* bl)
{
    uint32_t n = game::noun_id(bl);
    return type_for_noun(n) || n == kCityHallNoun;
}
static void host_send_building_hp(size_t ci, void* city, double now)
{
    if (!g_fix_building_hp)
        return;
    std::vector<void*> blds;
    for (void* bl : vec_of((uint8_t*)city + kCityBuildings))
        if (hp_building(bl) && blds.size() < 40)
            blds.push_back(bl);
    uint32_t h = 2166136261u;
    for (void* bl : blds)
        h = (h ^ (uint32_t)(int)std::lround(F<float>(bl, kBuildingState + 4))) * 16777619u;
    auto& last = g_hp_sent[city];
    if (last.first == h && now - last.second < 3.0)
        return;
    last = {h, now};
    net::Writer w;
    w.u16(0); w.u8(kHpChunk); w.u8(0);
    w.u8((uint8_t)ci);
    w.u8((uint8_t)blds.size()); // 40 x 20 bytes + 6: within kMaxPacket
    for (void* bl : blds) {
        net::Vec3 bp;
        game::object_position(bl, bp);
        w.u32(game::noun_id(bl));
        w.f32(bp.x); w.f32(bp.y); w.f32(bp.z);
        w.f32(F<float>(bl, kBuildingState + 4));
    }
    for (auto& pl : C.sess->players())
        if (pl.id != 0)
            C.sess->send_unreliable_to(pl.id, net::MSG_CIV_STATE, w);
    g_hp_tx++;
}

static void host_tick(double now)
{
    if (!g_held_builds.empty() && !planner_city()) {
        auto held = std::move(g_held_builds);
        g_held_builds.clear();
        auto cities = all_cities();
        for (auto& h : held)
            if (std::find(cities.begin(), cities.end(), h.city) != cities.end()) {
                g_build_from = h.from;
                build_in_city(h.city, h.noun, h.pos, false);
                g_build_from = -1;
            }
    }
    trade_tick(now);
    boost_tick(now);
    movie_tick();
    host_send_shots(now); // BUG-0477
    host_send_tribes(now); // BUG-0539
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
    if (roster_changed && g_fix_orphans)
        for (auto& [v, hv] : g_hv) {
            if (hv.owner >= 0xFE || std::binary_search(players.begin(), players.end(), hv.owner))
                continue;
            uint8_t owner = 0;
            nearest_player_city(veh_pos(v), &owner);
            if (owner == 0xFF || owner == kShared || !std::binary_search(players.begin(), players.end(), owner))
                owner = 0;
            hv.owner = owner;
            g_orphans_given++;
        }
    for (void* v : vehicles) {
        if (g_hv.count(v))
            continue;
        uint8_t owner = 0xFE;
        if (pol_of(v) == ppol) {
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
    host_send_roads(now); // BUG-0494
    // Chunks that fit one packet (kMaxPacket): a larger message is dropped by the connection for
    // good. Until 2026-10-23 chunk 0 carried every city and 24 vehicles (and later the spice nodes and
    // a city's buildings): ~1200 bytes with 9 cities and 24 vehicles, so with a grown planet the
    // guest got no cities, captures or the first 24 vehicles at all. The items are packed in order
    // (cities, spice nodes, one city's buildings, vehicles) into as many chunks as needed.
    // + u32 time (BUG-0122); a vehicle is 41 bytes + its f32 top speed in the tail after the time (BUG-0787)
    constexpr size_t kCityBytes = 25, kNodeBytes = 16, kVehBytes = 45, kHead = 14;
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
            if (type_for_noun(game::noun_id(bl)) && blds.size() < 32) {
                blds.push_back(bl);
                // BUG-0602 diagnostics: a building of the host's city that stands off its layout slot
                net::Vec3 bp{};
                float nd = 0;
                if (game::object_position(bl, bp) && g_host_offslot_logged < 12 && !g_host_offslot_seen.count(bl)) {
                    int in = slot_of(g_host_cities[bci], bl, bp, &nd, nullptr);
                    if (in < 0 || nd > 2.f) {
                        g_host_offslot_seen.insert(bl);
                        g_host_offslot_logged++;
                        H->log(M, SK_INFO, "civ: BUG-0602: host building %08X of city %u at (%.1f %.1f %.1f): slot %d, nearest slot %.1f m",
                               game::noun_id(bl), (unsigned)bci, bp.x, bp.y, bp.z, in, nd);
                    }
                }
            }
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
        // BUG-0787: the vehicles' top speeds (+0x218, the alarm boost included) after the time, in the same order: a
        // guest before 0.7.13 finds more than the time behind the vehicles and only loses the sender time
        for (size_t i = c.v0; i < c.v1; i++)
            w.f32(F<float>(vehicles[i], 0x218));
        g_max_chunk = std::max<uint32_t>(g_max_chunk, (uint32_t)w.size());
        for (auto& pl : C.sess->players())
            if (pl.id != 0)
                C.sess->send_unreliable_to(pl.id, net::MSG_CIV_STATE, w);
    }
    if (bci != 0xFF) {
        host_send_building_hp(bci, g_host_cities[bci], now); // BUG-0529
        host_send_turrets(bci, g_host_cities[bci], now);     // BUG-0484
    }
}

static const char* g_buy_fail = "";
static void* host_buy(void* city, int loco)
{
    void* civ = player_civ();
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

static bool g_fix_build_orphan = true;
COOP_FIX("civ.build_orphan", "BUG-0491", g_fix_build_orphan,
         "civ: a building the mod makes for a full city is destroyed, not left in the world outside every slot");
static uint32_t g_build_orphans;
// BUG-0613: a building the mod pays for (`coop.civ build`, the tests' build on either game) was paid with
// cCivilization::SpendMoney, which takes the money below 0 (the game's planner only offers what the money
// covers): the host's game showed -800 while every guest floors the shared total at 0 (shared.cpp), so the
// divergence detector's `Civilization:state:money` row came in every civ_coop run. On: a paid build the money
// does not cover is not built ("not enough money"), as the planner and a purchase (host_buy) do. Off: built and paid.
static bool g_fix_build_money = true;
COOP_FIX("civ.build_money", "BUG-0613", g_fix_build_money,
         "civ: a building the mod pays for is not built when the money does not cover it (money never below 0)");
static const char* g_build_fail = "";
// build `noun` in `city` at the free slot nearest to `pos` (the game's planner sequence)
static void* build_in_city(void* city, uint32_t noun, const net::Vec3& pos, bool pay)
{
    g_build_fail = "no free slot";
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
    if (pay && g_fix_build_money) { // BUG-0613
        void* civ = player_civ();
        if (civ && thiscall<float>(a_GetMoney, civ) < vcall<float>(b, 0x5C / 4)) {
            g_build_fail = "not enough money";
            if (a_DestroyInstance)
                thiscall<void>(a_DestroyInstance, nm(), o);
            return nullptr;
        }
    }
    vcall<void>(b, 0x54 / 4, city); // SetOwnerCity
    float p[3] = {pos.x, pos.y, pos.z};
    vcall<void>((uint8_t*)b + 0x34, 0x38 / 4, (const float*)p);
    if (!thiscall<bool>(a_SnapToSlot, city, b, 1)) {
        // no free slot: the building was made for nothing. It stayed in the world (in no city list and
        // no slot, nobody collects it, the next save keeps it) at the host's spot, over the slot of
        // another building: a pick in the planner faulted on it (BUG-0491). Off: left as it was.
        g_build_orphans++;
        if (g_fix_build_orphan && a_DestroyInstance)
            thiscall<void>(a_DestroyInstance, nm(), o);
        return nullptr;
    }
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

    // BUG-0325 diagnostics: why a vehicle of a command is not the sender's (gone from the host's game, dead,
    // or another owner: a gift or a capture in between), logged with the rejection
    char why[160] = "";
    auto own_vehicle = [&](uint16_t id) -> void* {
        auto it = g_hv_by_id.find(id);
        if (it == g_hv_by_id.end()) {
            snprintf(why, sizeof why, " (vehicle %u: not in the host's game)", (unsigned)id);
            return nullptr;
        }
        if (g_hv[it->second].owner != from) {
            snprintf(why, sizeof why, " (vehicle %u: owner %u%s)", (unsigned)id, (unsigned)g_hv[it->second].owner,
                     F<uint8_t>(it->second, kVDead) ? ", dead" : "");
            return nullptr;
        }
        return it->second;
    };
    auto reject = [&] {
        if (from != C.sess->my_id()) {
            g_cmds_rejected++;
            if (g_cmds_rejected <= 20) // BUG-0325 diagnostics: which command, from whom, why
                H->log(M, SK_INFO, "civ: rejected command kind %u from player %u%s", (unsigned)kind, (unsigned)from, why);
        }
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
        if (v && t.kind && !target) // BUG-0325 diagnostics: the order's target is not in the host's game
            snprintf(why, sizeof why, " (vehicle %u: target kind %u id %u noun %08X at (%.0f %.0f %.0f) not in the host's game)",
                     (unsigned)id, (unsigned)t.kind, (unsigned)t.id, t.noun, t.pos.x, t.pos.y, t.pos.z);
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
        if (planner_holds(city)) {
            g_held_builds.push_back({from, city, noun, p});
            g_planner_held++;
            g_cmds_done++;
            return;
        }
        g_build_from = from; // the guest already has it
        void* b = build_in_city(city, noun, p, false); // the guest paid in its planner (shared money)
        g_build_from = -1;
        if (!b)
            return reject();
    } else if (kind == 6 /* CMD_BUYCITY, BUG-0877 */ || kind == 7 /* CMD_CAPTURE, BUG-0878 */) {
        uint8_t ci = r.u8();
        float price = kind == 6 ? r.f32() : 0.f;
        int type = kind == 7 ? r.u8() : 2;
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        void* pc = player_civ();
        bool on = kind == 6 ? g_fix_buy_city && o_CityBuy : g_fix_away_capture;
        if (!r.ok() || !on || !city || !pc || pol_of(city) == pol_of(pc) || !std::isfinite(price) || price < 0 ||
            type < 0 || type > 2) {
            snprintf(why, sizeof why, " (city %u: %s)", (unsigned)ci, !city ? "not in the host's game" : "already the players'");
            return reject();
        }
        float money = thiscall<float>(a_GetMoney, pc);
        g_capture_for = from;
        if (kind == 6)
            o_CityBuy(city, pol_of(pc), price);
        else
            h_Capture(city, pol_of(pc), type, 0, 0, 0);
        g_capture_for = -1;
        // (the owner changes when the capture's cinematic ends: a purchase that took spent the money)
        if (kind == 6 && pol_of(city) != pol_of(pc) && thiscall<float>(a_GetMoney, pc) >= money) {
            net::Writer nw;
            nw.str8("the city was not bought: not enough money or no trade route in the host's game");
            C.sess->send_reliable_to(from, net::MSG_NOTICE, nw);
            g_city_buys_refused++;
            return reject();
        }
        g_city_buys_done += kind == 6;
        H->log(M, SK_INFO, "civ: city %u %s for player %u in this game", (unsigned)ci, kind == 6 ? "bought" : "captured",
               (unsigned)from);
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
    if (planner_holds(city)) { // BUG-0493: after the planner closes (the next snapshot)
        g_planner_held++;
        g_bfix_seen[city] = 0;
        return;
    }
    auto local = vec_of((uint8_t*)city + kCityBuildings);
    std::vector<bool> used(local.size(), false);
    std::vector<size_t> missing;
    std::vector<void*> ruin_now;
    std::vector<std::pair<void*, net::Vec3>> off_spot;
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
            if (net::dist(lp, p) > 1.f)
                off_spot.push_back({local[k], p}); // BUG-0602: matched, but not where the host's stands
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
    for (auto& [b, p] : off_spot)
        to_host_spot(b, p);
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
        float near_before = 0;
        void* occ = nullptr;
        slot_of(city, nullptr, p, &near_before, &occ);
        if (void* b = build_in_city(city, noun, p, false)) {
            g_bfix_added++;
            net::Vec3 bp{};
            game::object_position(b, bp);
            if (net::dist(bp, p) > 4.f) {
                g_bfix_off_slot++;
                if (g_bfix_logged++ < 12)
                    H->log(M, SK_INFO,
                           "civ: BUG-0602: the host's building %08X of city %d at (%.1f %.1f %.1f) went to (%.1f %.1f %.1f) here, "
                           "%.1f m off; the nearest slot here was %.1f m from the host's spot (occupant %08X)",
                           noun, (int)(std::find(g_host_cities.begin(), g_host_cities.end(), city) - g_host_cities.begin()), p.x, p.y, p.z, bp.x, bp.y, bp.z, net::dist(bp, p), near_before,
                           occ ? game::noun_id(occ) : 0u);
            }
            to_host_spot(b, p); // BUG-0602
        }
    }
    g_applying = false;
}

// BUG-0529: the host's hit points of one city's buildings (chunk kHpChunk)
static void guest_building_hp(net::Reader& r)
{
    uint8_t ci = r.u8(), n = r.u8();
    struct Row {
        uint32_t noun;
        net::Vec3 p;
        float hp;
    };
    std::vector<Row> rows(n);
    for (auto& b : rows) {
        b.noun = r.u32();
        b.p.x = r.f32(); b.p.y = r.f32(); b.p.z = r.f32();
        b.hp = r.f32();
    }
    if (!r.ok())
        return;
    g_hp_rx++;
    void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
    if (!g_fix_building_hp || !city || local_play() || planner_holds(city) || game::mode_id() != modes::kCiv)
        return;
    auto local = vec_of((uint8_t*)city + kCityBuildings);
    std::vector<bool> used(local.size(), false);
    static const float zero[3] = {0, 0, 0};
    for (auto& b : rows) {
        if (!std::isfinite(b.hp) || b.hp <= 0.f)
            continue; // a ruin: reconcile_buildings makes it
        void* best = nullptr;
        size_t bk = 0;
        float bd = 4.f;
        for (size_t k = 0; k < local.size(); k++) {
            net::Vec3 lp;
            if (used[k] || game::noun_id(local[k]) != b.noun || !game::object_position(local[k], lp))
                continue;
            float d = net::dist(lp, b.p);
            if (d < bd) {
                bd = d;
                best = local[k];
                bk = k;
            }
        }
        if (!best) {
            g_hp_nomatch++;
            continue;
        }
        used[bk] = true;
        if (F<int>(best, kBuildingState) == 2)
            continue; // ruined here, whole there: reconcile_buildings (civ.ruin_repair)
        float& hp = F<float>(best, kBuildingState + 4);
        if (hp > b.hp + 0.5f) {
            g_applying = true;
            thiscall<int>(a_CombatantDamage, (uint8_t*)best + 0x120, hp - b.hp, 0xFFFFFFFFu, 0, (const float*)zero,
                          (void*)nullptr);
            g_applying = false;
            if (F<int>(best, kBuildingState) != 2 && std::fabs(hp - b.hp) > 0.5f)
                hp = b.hp; // the game's damage scaled it: the host's value
            g_hp_lowered++;
        } else if (hp < b.hp - 0.5f) {
            hp = b.hp;
            g_hp_raised++;
        }
    }
}

static bool g_fix_mirror_remove = true; // BUG-0478 (kill_local below)
COOP_FIX("civ.mirror_remove", "BUG-0478", g_fix_mirror_remove,
         "civ: a guest's mirror the host no longer has is removed from the world, not left as a wreck");
// BUG-0493: vehicles of this game the mod removes once the city planner no longer moves them
static std::unordered_set<void*> g_kill_later;
// a removal waits while the planner moves the vehicle, and (BUG-0478: the removal frees it now, Die
// alone did not) while a cinematic plays (its actors are the game's raw pointers, CLAUDE.md 2)
static bool hold_removal(void* v) { return planner_moves(v) || (g_fix_mirror_remove && movie_playing()); }

// BUG-0470: the guest's mirror vehicles are the host's world, not this one's: a save of the guest's
// game keeps them out (savestrip writes a null reference; the civilization's vehicle lists are cleaned
// of nulls on load, savestrip.cpp). `coop.fix civ.save_strip off`: saved like the game's own (0.7.3.6).
static bool g_fix_save_strip = true;
COOP_FIX("civ.save_strip", "BUG-0470", g_fix_save_strip, "saves: a Civ guest's mirror vehicles are kept out of its saves");
static uint32_t g_save_stripped;
// The civilizations' vehicle vector (+0xB0) and selectable vector (+0xD4, the vehicles' spatial
// interfaces) read null entries back on load and faulted (0xBF2BF2): the mirrors leave both lists
// while the save is written and come back on the next frame (savestrip::add_save_hooks).
struct Took {
    void* civ;
    uintptr_t off;
    void* p;
};
static std::vector<Took> g_took;
static bool is_mirror(void* v) { return v && (g_gv_of.count(v) || g_kill_later.count(v)); }
static void host_buildings_keys(bool own);
static void save_start()
{
    host_buildings_keys(true); // BUG-0670: no key of ours in the save
    if (!g_fix_save_strip || !is_guest() || game::mode_id() != modes::kCiv || !nm())
        return;
    for (void* civ : vec_of(thiscall<void*>(a_GetAllCivs, nm())))
        for (uintptr_t off : {(uintptr_t)0xB0, (uintptr_t)0xD4}) {
            void**& b = F<void**>(civ, off);
            void**& e = F<void**>(civ, off + 4);
            if (!b || e < b)
                continue;
            void** w = b;
            for (void** it = b; it != e; ++it) {
                void* v = *it ? (off == 0xB0 ? *it : (uint8_t*)*it - kVSpatial) : nullptr;
                if (is_mirror(v))
                    g_took.push_back({civ, off, *it});
                else
                    *w++ = *it;
            }
            for (void** it = w; it != e; ++it)
                *it = nullptr;
            e = w;
        }
}
static void save_end()
{
    host_buildings_keys(false); // BUG-0670
    for (auto& t : g_took) { // back at the end of the list (the capacity is the one they left)
        void**& e = F<void**>(t.civ, t.off + 4);
        if (e < F<void**>(t.civ, t.off + 8))
            *e++ = t.p;
    }
    g_took.clear();
}
static bool save_filter(void* o, uint32_t noun)
{
    if (!g_fix_save_strip || noun != 0x18C6DE8 || !(g_gv_of.count(o) || g_kill_later.count(o)))
        return false;
    g_save_stripped++;
    return true;
}

// BUG-0478: Die(v, 0, -1) alone leaves the dead vehicle in the world for good (in the noun list, the
// civilization's lists, moved on by its AI): the host's vehicle that died is gone from the host's
// game, the guest kept a wreck (the detector's "a vehicle more on the guest", more the longer it
// played). The game removes a vehicle with 0xCAA7B0 (the disband path: Die, the behavior manager,
// cCivilization 0xBF3D90 which destroys it). `coop.fix civ.mirror_remove off`: Die only (0.7.3.6).


static void kill_local(void* v)
{
    g_applying = true;
    if (g_fix_mirror_remove && a_VehicleRemove) {
        thiscall<void>(a_VehicleRemove, v, 0); // no refund
        g_exist_frame = ~0ull;                 // freed: the frame's list of vehicles is read again
    } else
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
                if (o_SpawnNextCity(ex, g_instant_found_fix ? 1 : 0, nullptr)) // BUG-0624: late already
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
        if (pol_of(c) != pol && planner_holds(c)) // BUG-0493: the next snapshot after the planner
            g_planner_held++;
        else if (pol_of(c) != pol && g_fix_discovery_card && !thiscall<void*>(a_CivByPol, nm(), pol)) {
            // BUG-0492: an owner this game has no civilization for: the city would belong to nobody
            // here (its menu and discovery card read the missing civilization)
            if (g_owner_unknown++ < 10)
                H->log(M, SK_INFO, "civ: the host's owner %08X of city %d is no civilization here: not changed (BUG-0492)", pol,
                       c0 + i);
        }
        else if (pol_of(c) != pol) { // captured on the host
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
    if (!g_veh_force) { // BUG-0647: the host's own design, sent by the host, under a key of ours
        bool pending = false;
        creations::Key hk = creations::host_design({key[0], key[1], key[2]}, creations::kMkVehicle, &pending);
        if (pending)
            return false; // asked the host: the mirror comes with its answer (the next state cycle)
        key[0] = hk.instance;
        key[1] = hk.type;
        key[2] = hk.group;
    }
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

// BUG-0647: a vehicle of this game's loaded copy of the host's world bound as a mirror: its model is the key
// the save names (the host's own design id), which this game resolves to its own creation of that id. The
// host's design under our key replaces it (the model object's SetModelKey, vt 0x94 of +0x34, as
// cVehicle::Load 0xCA67C0 sets it). false: asked the host, no answer yet (do not bind it yet).
static unsigned g_bound_redesigned;
static bool bound_design(void* v)
{
    uint32_t* mk = &F<uint32_t>(v, kVModelKey);
    if (!mk[0] || mk[0] == 0xFFFFFFFFu || mk[1] != 0x24682294)
        return true;
    bool pending = false;
    creations::Key hk = creations::host_design({mk[0], mk[1], mk[2]}, creations::kMkVehicle, &pending);
    if (pending)
        return false;
    if (hk.instance == mk[0])
        return true;
    creations::Model m = creations::ensure_model(hk, creations::kMkVehicle);
    if (!m.key.instance)
        return true; // cannot be built here: it keeps the model it has
    uint32_t k[3] = {m.key.instance, m.key.type, m.key.group}, was = mk[0];
    void* sp = (uint8_t*)v + kVSpatial;
    vcall<void>(sp, 0x94 / 4, (const uint32_t*)k);
    g_bound_redesigned++;
    H->log(M, SK_INFO, "civ: a bound vehicle's model %08X is the host's own design: %08X here (BUG-0647)", was, k[0]);
    return true;
}

// BUG-0670 (docs/re/civ.md#civ-host-buildings): a building's model is the design key cCity::SetBuildingModel
// (0xBDB940) reads from its civilization itself (GetDesignKey), then the building's model object's SetModelKey
// (vt 0x94 of +0x34, the key at +0x34 +0x90 as for vehicles). The host's player civilization names its building
// designs by keys of the host's own My Creations; the guest's copy of it names the same keys, which the guest's game
// resolved to the guest's own creation of that id: other city halls, houses, factories than the host's (the class of
// BUG-0647). On a guest every building of every city is checked once a second (built by build_in_city, by the
// guest's own planner, or loaded with the host's world): a design of the host's own (asked with
// creations::host_design, the BUG-0647 path) gets the host's records under a key of ours, and the building's model
// object that key (SetModelKey). The building's own key is written back while the game saves and when the session
// ends (no key of ours in a save). `coop.fix civ.host_buildings off`: the guest's own creation of that id (0.7.9).
static bool g_host_buildings_fix = true;
COOP_FIX("civ.host_buildings", "BUG-0670", g_host_buildings_fix,
         "civ: a guest's buildings of the host's civilization show the host's own building designs, not the guest's creation of the same id");
constexpr uintptr_t kBModelKey = 0x34 + 0x90; // (the vehicles' kVModelKey: the same spatial model object)
constexpr uint32_t kBuildingModelType = 0x2399BE55;
struct BldRekey {
    uint32_t orig[3];
    uint32_t ours;
};
static std::unordered_map<void*, BldRekey> g_bld_rekeyed; // guest: building -> its own key (live buildings only read)
static double g_bld_sweep_at = -100;
static unsigned g_bld_rekeys, g_bld_pending, g_bld_restored;
static std::vector<void*> city_buildings_all()
{
    std::vector<void*> out;
    if (!nm())
        return out;
    for (void* city : all_cities())
        for (void* b : vec_of((uint8_t*)city + kCityBuildings))
            if (b)
                out.push_back(b);
    return out;
}
static void set_building_key(void* b, const uint32_t* k)
{
    vcall<void>((uint8_t*)b + 0x34, 0x94 / 4, k);
}
static void host_buildings_tick(double now)
{
    if (!g_host_buildings_fix || !is_guest() || game::mode_id() != modes::kCiv || now - g_bld_sweep_at < 1.0)
        return;
    g_bld_sweep_at = now;
    unsigned pending = 0;
    for (void* b : city_buildings_all()) {
        uint32_t* mk = &F<uint32_t>(b, kBModelKey);
        if (!mk[0] || mk[0] == 0xFFFFFFFFu || mk[1] != kBuildingModelType)
            continue;
        auto it = g_bld_rekeyed.find(b);
        if (it != g_bld_rekeyed.end() && it->second.ours == mk[0])
            continue; // ours already
        bool wait = false;
        creations::Key hk = creations::host_design({mk[0], mk[1], mk[2]}, creations::kMkBuilding, &wait);
        if (wait) {
            pending++;
            continue;
        }
        if (hk.instance == mk[0])
            continue; // not the host's own (a premade, a generated design): as it is
        creations::Model m = creations::ensure_model(hk, creations::kMkBuilding);
        if (!m.key.instance)
            continue; // cannot be built here: it keeps the model it has
        BldRekey r{{mk[0], mk[1], mk[2]}, m.key.instance};
        uint32_t k[3] = {m.key.instance, m.key.type, m.key.group};
        set_building_key(b, k);
        g_bld_rekeyed[b] = r;
        g_bld_rekeys++;
        H->log(M, SK_INFO, "civ: a building's model %08X is the host's own design: %08X here (BUG-0670)", r.orig[0], k[0]);
    }
    g_bld_pending = pending;
}
// the buildings of ours get their own keys back (raw: the save writes the key, no model is rebuilt) / ours again
static void host_buildings_keys(bool own)
{
    if (g_bld_rekeyed.empty() || !nm())
        return;
    for (void* b : city_buildings_all()) {
        auto it = g_bld_rekeyed.find(b);
        if (it == g_bld_rekeyed.end())
            continue;
        uint32_t* mk = &F<uint32_t>(b, kBModelKey);
        if (own && mk[0] == it->second.ours) {
            mk[0] = it->second.orig[0];
            g_bld_restored++;
        } else if (!own && mk[0] == it->second.orig[0])
            mk[0] = it->second.ours;
    }
}
static void host_buildings_session_end()
{
    if (g_bld_rekeyed.empty() || !nm())
        return;
    for (void* b : city_buildings_all()) { // the guest's game plays on alone: its own designs
        auto it = g_bld_rekeyed.find(b);
        if (it != g_bld_rekeyed.end() && F<uint32_t>(b, kBModelKey) == it->second.ours)
            set_building_key(b, it->second.orig);
    }
    g_bld_rekeyed.clear();
}
static std::string host_buildings_list()
{
    std::string out;
    char b[200];
    int ci = 0;
    for (void* city : all_cities()) {
        for (void* bl : vec_of((uint8_t*)city + kCityBuildings)) {
            if (!bl)
                continue;
            const uint32_t* mk = &F<uint32_t>(bl, kBModelKey);
            auto it = g_bld_rekeyed.find(bl);
            snprintf(b, sizeof b, "  bld city=%d %p noun=%08X model=%08X/%08X/%08X own=%08X\n", ci, bl, game::noun_id(bl), mk[0],
                     mk[1], mk[2], it == g_bld_rekeyed.end() ? 0u : it->second.orig[0]);
            out += b;
        }
        ci++;
    }
    snprintf(b, sizeof b, "host buildings: fix=%d rekeys=%u pending=%u restored=%u now=%zu\n", (int)g_host_buildings_fix,
             g_bld_rekeys, g_bld_pending, g_bld_restored, g_bld_rekeyed.size());
    return out + b;
}

static void guest_state(net::Reader& r)
{
    uint16_t cycle = r.u16();
    uint8_t chunk = r.u8(), chunks = r.u8();
    if (!r.ok() || !net::cycle_chunk_ok(chunk, chunks))
        return;
    bool started = false;
    if (!g_cyc.begin(cycle, g_cycle_order_fix, &started))
        return; // a late chunk of an older cycle
    if (started)
        g_cycle_ids.clear();
    if (!guest_cities(r))
        return;
    guest_nodes_and_buildings(r);
    double now = H->time_seconds();
    uint8_t n = r.u8();
    const size_t body = (size_t)n * 41; // 41 bytes per vehicle
    double sent = net::snapshot_time(r, body);
    const uint8_t* tops = nullptr; // BUG-0787: the host vehicles' top speeds after the time (a host from 0.7.13 on)
    if (sent < 0 && r.ok() && n && r.left() == body + 4 + (size_t)n * 4) {
        uint32_t ms;
        memcpy(&ms, r.cur() + body, 4);
        sent = ms / 1000.0;
        tops = r.cur() + body + 4;
    }
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
        float top = alive_v ? F<float>(v, 0x218) : 0.f;
        g.host_top = -1.f;
        if (tops) {
            float ht;
            memcpy(&ht, tops + 4 * (size_t)i, 4);
            if (std::isfinite(ht) && ht > 0.f && ht < 10000.f) {
                g.host_top = ht;
                if (g_vel_cap_host_fix)
                    top = std::max(top, ht);
            }
        }
        g.est.max_speed = g_vel_cap_fix && alive_v ? 2.f * top + 10.f : 0.f;
        g.est.max_ahead = g_extrap_fix ? kVehAhead : 0.4;
        // BUG-0425 (drive.lead): moved on to the host's now as every remote body; at most kVehLead past its state:
        // its age is the send interval (0.1-0.13 s at 30 fps) plus the latency and a frame, which 0.15 s cut short
        // (the vehicle 0.1-0.24 s behind, p90 2.8-3.5 m at 15-25 m/s, body_trail_civ on a loaded lab). Longer is a
        // host hitch, when its vehicles stand (BUG-0186: 0.4 s threw one 10 m ahead)
        if (g_extrap_fix && net::g_lead_fix)
            g.est.max_ahead = kVehLead;
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
        if (v && !bound_design(v))
            continue; // BUG-0647: the host's answer for its model first (bound on a later cycle)
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
    g_cyc.add(chunk);
    if (chunk + 1 < chunks)
        return;
    // only a complete cycle tells which vehicles are gone: with a chunk lost (or late) every
    // vehicle of it was killed and made again on the next cycle
    if (!g_cyc.complete(chunks)) {
        g_incomplete++;
        return;
    }
    for (auto it = g_gv.begin(); it != g_gv.end();) {
        if (!g_cycle_ids.count(it->first)) {
            if (it->second.v && game::object_alive(it->second.v) && !F<uint8_t>(it->second.v, kVDead)) {
                if (hold_removal(it->second.v)) { // BUG-0493: removed when the planner (or a cinematic) lets it go
                    g_kill_later.insert(it->second.v);
                    g_planner_kills_held++;
                } else {
                    kill_local(it->second.v);
                    g_removed++;
                }
            }
            g_gv_of.erase(it->second.v);
            it = g_gv.erase(it);
        } else
            ++it;
    }
    // the guest's own vehicles that the host does not have (its local civ AI, the start vehicle)
    for (void* lv : all_vehicles())
        if (!g_gv_of.count(lv)) {
            if (g_kill_later.count(lv))
                continue;
            if (hold_removal(lv)) {
                g_kill_later.insert(lv);
                g_planner_kills_held++;
                continue;
            }
            kill_local(lv);
            g_removed++;
            g_removed_local++; // tests: a vehicle of this game's own, not a mirror the host no longer has
        }
}

static void guest_tick(double now)
{
    guest_fire(now); // BUG-0477
    host_buildings_tick(now); // BUG-0670
    for (auto it = g_kill_later.begin(); it != g_kill_later.end();) {
        void* v = *it;
        if (!exists(v) || F<uint8_t>(v, kVDead) || g_gv_of.count(v)) { // gone, or a mirror again
            it = g_kill_later.erase(it);
        } else if (!hold_removal(v)) {
            kill_local(v);
            g_removed++;
            it = g_kill_later.erase(it);
        } else
            ++it;
    }
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
    static double last_tick = 0; // BUG-0438: the last frame's time (the placing shows up a frame later)
    const double fdt = std::min(std::max(now - last_tick, 0.0), 0.1);
    last_tick = now;
    for (auto& [id, g] : g_gv) {
        if (!g.v || !game::object_alive(g.v) || now - g.seen > 3.0) {
            g.has_put = false;
            continue;
        }
        const net::Vec3 at = veh_pos(g.v);
        net::Vec3 cur = at;
        // moved on with the estimated velocity (no lag behind the host); BUG-0425 (drive.lead): by the one-way latency
        // the session measures (half the round trip), not a fixed 0.03 s
        net::Vec3 want = g.est.at(now + (g_place_now_fix ? fdt : 0.0), net::g_lead_fix ? net::g_one_way : 0.03);
        if (g_place_now_fix && g.has_put) // BUG-0438: on with the target's velocity, only the rest glided below
            cur = {cur.x + g.est.vel.x * (float)fdt, cur.y + g.est.vel.y * (float)fdt, cur.z + g.est.vel.z * (float)fdt};
        float d = net::dist(cur, want);
        if (g.has_put) {
            g.pushed = net::dist(at, g.put);
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
    if (r.ok() && ci == kRoadList) { // BUG-0494: the host's trade routes
        guest_roads(r);
        return;
    }
    uint32_t noun = r.u32();
    net::Vec3 p;
    p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
    if (r.ok() && ci == kNextCitySite && (noun == 0 || noun == kFoundInstant)) { // the host's planet grew a new city: the same site here
        if (void* ex = skg::cdecl_call<void*>(a_ExpansionGet)) {
            g_applying = true;
            bool ok = o_SpawnNextCity(ex, noun == kFoundInstant ? 1 : 0, nullptr); // BUG-0624: as the host founded it
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
    if (planner_holds(city)) { // BUG-0493: the snapshots' reconciliation builds it after the planner
        g_planner_held++;
        return;
    }
    g_applying = true;
    if (void* b = build_in_city(city, noun, p, false)) {
        g_builds_in++;
        to_host_spot(b, p); // BUG-0602
    }
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
    if (g_ok && C.sess && C.sess->connected())
        space_tick(now); // BUG-0766 / BUG-0884
    if (!active())
        return;
    prune_gone();
    g_cmd_world = modes::current_world();
    static double active_at, active_since;
    if (now - active_at > 1.0) // the stage just came back (a load, an editor): its world settles first
        active_since = now;
    active_at = now;
    if (is_host() && !g_cmd_held.q.empty() && now - active_since > net::HeldCommands::kSettle) // BUG-0693: the commands that came while this game was away
        g_cmd_held.drain(modes::current_world(), now, [](const net::HeldCommands::Cmd& c) {
            net::Reader r(c.bytes.data(), c.bytes.size());
            H->log(M, SK_INFO, "civ: a command of player %u held while away runs now", c.from);
            host_command(c.from, r);
        });
    if (is_host())
        host_tick(now);
    else if (is_guest())
        guest_tick(now);
}

static uint32_t g_founds_outside; // BUG-0319 diagnostics: city foundings received outside the Civ mode
// BUG-0751: guest, Space: a building bought in a colony's planner goes to the host with the planet it is on
static void send_space_build_to_host(uint32_t place, uint32_t noun, const net::Vec3& cp, const net::Vec3& pos)
{
    net::Writer w;
    w.u8(CMD_SPACE_BUILD);
    w.u32(place);
    w.u32(noun);
    w.f32(cp.x); w.f32(cp.y); w.f32(cp.z);
    w.f32(pos.x); w.f32(pos.y); w.f32(pos.z);
    C.sess->send_reliable_to(0, net::MSG_CIV_CMD, w);
    g_space_builds_sent++;
    H->log(M, SK_INFO, "civ: space planner: a building %08X in the city at (%.0f %.0f %.0f) on %08X sent to the host (BUG-0751)",
           noun, cp.x, cp.y, cp.z, place);
}

// the 0.7.11 send at AddBuilding (civ.space_planner_closed off)
static void send_space_build(void* city, void* b)
{
    uint32_t place = C.space_place ? C.space_place() : 0;
    if (!(place & 0x40000000u))
        return;
    // the building is not placed yet when the planner adds it (its position reads (0 0 0)): the city's position finds
    // the city on the host, its free slot takes the building there as here
    net::Vec3 pos;
    game::object_position(b, pos);
    send_space_build_to_host(place, game::noun_id(b), city_pos(city), pos);
}

static void* local_city_near(const net::Vec3& p);

// BUG-0766 / BUG-0884: a colony building for a planet this game is not on yet (host: a guest's; guest: the host's)
struct SpaceBuild {
    int from;
    uint32_t place, noun, world;
    net::Vec3 cp, p;
    double tried = 0;    // the first time it found no free plot here (0 = not yet)
    double next_try = 0; // then the next try
};
static std::vector<SpaceBuild> g_space_held;
static uint32_t g_space_held_built, g_space_relayed, g_space_relay_in;
// host: its own planner building (placed by the planner a moment after AddBuilding: its position reads 0 0 0 then)
struct SpaceOut {
    void* b;
    void* city;
    uint32_t place, noun;
    net::Vec3 cp;
    double t;
    int except;
    bool planner; // this game's own planner building (host: BUG-0884, guest: BUG-0902): sent when the planner closed
                  // with it (a drag that does not end on a free plot adds the building and takes it away again: lab
                  // 20261110-193142)
};
static std::vector<SpaceOut> g_space_out;
static uint32_t g_space_place_seen;
static double g_space_place_since;

static uint32_t space_place_now() { return C.space_place ? C.space_place() : 0; }

// the building made in this game's city near cp (not paid: the buyer paid; the shared money follows)
static void* space_build_here(const SpaceBuild& sb, bool quiet = false)
{
    void* city = local_city_near(sb.cp);
    g_build_from = sb.from; // the buyer already has it
    g_applying = true;      // not sent again by this game's h_AddBuilding
    void* b = city ? build_in_city(city, sb.noun, sb.p, false) : nullptr;
    if (b)
        space_write_city(city); // BUG-0901
    g_applying = false;
    g_build_from = -1;
    if (quiet && !b)
        return b;
    H->log(M, b ? SK_INFO : SK_WARN, "civ: space planner: %s's building %08X in the city at (%.0f %.0f %.0f) %s",
           sb.from >= 0 ? C.sess->name_of((uint8_t)sb.from).c_str() : "?", sb.noun, sb.cp.x, sb.cp.y, sb.cp.z,
           b ? "built" : city ? g_build_fail : "no city near it");
    return b;
}

static void space_send(const SpaceBuild& sb, int except)
{
    net::Writer w;
    w.u8(CMD_SPACE_BUILD);
    w.u32(sb.place);
    w.u32(sb.noun);
    w.f32(sb.cp.x); w.f32(sb.cp.y); w.f32(sb.cp.z);
    w.f32(sb.p.x); w.f32(sb.p.y); w.f32(sb.p.z);
    C.sess->broadcast_reliable(net::MSG_CIV_CMD, w, except);
    g_space_relayed++;
}

// host: a building it got in a colony city goes to the guests (BUG-0884), once the planner placed it; guest: its own
// planner building goes to the host (BUG-0902), once the planner closed with it
static void space_note_host_build(void* city, void* b, int except, bool planner)
{
    if (!g_fix_space_planner_relay || !city || !b || C.sess->players().size() < 2 + (except >= 0 ? 1 : 0))
        return;
    uint32_t place = space_place_now();
    if (!(place & 0x40000000u) || g_space_out.size() >= 64)
        return;
    g_space_out.push_back({b, city, place, game::noun_id(b), city_pos(city), H->time_seconds(), except, planner});
}

static void space_hold(const SpaceBuild& sb, uint32_t mine)
{
    if (g_space_held.size() >= 128)
        g_space_held.erase(g_space_held.begin());
    g_space_held.push_back(sb);
    H->log(M, SK_INFO, "civ: space planner: a building %08X for %08X kept until this game is there (now at %08X; %u kept)",
           sb.noun, sb.place, mine, (unsigned)g_space_held.size());
}

static bool read_space_build(net::Reader& r, SpaceBuild& sb, int from)
{
    r.u8(); // CMD_SPACE_BUILD
    sb.from = from;
    sb.place = r.u32();
    sb.noun = r.u32();
    sb.cp.x = r.f32(); sb.cp.y = r.f32(); sb.cp.z = r.f32();
    sb.p.x = r.f32(); sb.p.y = r.f32(); sb.p.z = r.f32();
    sb.world = modes::current_world();
    return r.ok();
}

// host, Space: a guest's colony planner building
static void host_space_build(uint8_t from, net::Reader& r)
{
    SpaceBuild sb;
    if (!read_space_build(r, sb, from))
        return;
    uint32_t mine = space_place_now();
    if (game::mode_id() != modes::kSpace || mine != sb.place) {
        g_space_builds_away++;
        H->log(M, SK_INFO, "civ: space planner: %s's building %08X on %08X not built: this game is at %08X (BUG-0751)",
               C.sess->name_of(from).c_str(), sb.noun, sb.place, mine);
        if (g_fix_space_build_hold) // BUG-0766
            space_hold(sb, mine);
        return;
    }
    void* b = space_build_here(sb);
    if (b)
        g_space_builds_in++;
    else
        g_space_builds_failed++;
    if (b)
        space_note_host_build(local_city_near(sb.cp), b, from); // BUG-0884: the other guests
}

// guest, Space: the host's colony building (its own or another guest's), BUG-0884
static void guest_space_build(net::Reader& r)
{
    SpaceBuild sb;
    if (!read_space_build(r, sb, 0))
        return;
    g_space_relay_in++;
    uint32_t mine = space_place_now();
    if (game::mode_id() != modes::kSpace || mine != sb.place || H->time_seconds() - g_space_place_since < 5.0)
        space_hold(sb, mine);
    else
        space_build_here(sb);
}

// every frame in any mode: the kept buildings of the planet this game is on now (5 s after it came: its cities are
// made), the host's own planner buildings once placed
static void space_tick(double now)
{
    uint32_t place = game::mode_id() == modes::kSpace ? space_place_now() : 0;
    if (place != g_space_place_seen) {
        g_space_place_seen = place;
        g_space_place_since = now;
    }
    if (!g_space_out.empty()) {
        std::vector<void*> cities = place ? all_cities() : std::vector<void*>{};
        for (size_t i = 0; i < g_space_out.size();) {
            SpaceOut& o = g_space_out[i];
            bool city_ok = o.place == place && std::find(cities.begin(), cities.end(), o.city) != cities.end();
            auto bl = city_ok ? vec_of((uint8_t*)o.city + kCityBuildings) : std::vector<void*>{};
            bool alive = city_ok && std::find(bl.begin(), bl.end(), o.b) != bl.end();
            net::Vec3 pos{};
            if (alive)
                game::object_position(o.b, pos);
            bool placed = alive && (pos.x != 0 || pos.y != 0 || pos.z != 0);
            bool open = alive && o.planner && planner_city() == o.city && now - o.t < 600.0;
            if (!alive || (!open && (placed || now - o.t > 3.0))) {
                if (alive && is_host()) {
                    SpaceBuild sb{0, o.place, o.noun, modes::current_world(), o.cp, pos};
                    space_send(sb, o.except);
                    H->log(M, SK_INFO, "civ: space planner: a building %08X in the city at (%.0f %.0f %.0f) on %08X sent to the guests (BUG-0884)",
                           o.noun, o.cp.x, o.cp.y, o.cp.z, o.place);
                } else if (alive && is_guest())
                    send_space_build_to_host(o.place, o.noun, o.cp, pos); // BUG-0902
                if (!alive && o.planner) {
                    g_space_planner_dropped++;
                    H->log(M, SK_INFO, "civ: space planner: a building %08X the planner added in the city at (%.0f %.0f %.0f) "
                           "was taken away again: not sent (BUG-0902)", o.noun, o.cp.x, o.cp.y, o.cp.z);
                }
                g_space_out.erase(g_space_out.begin() + i);
            } else
                i++;
        }
    }
    if (g_space_held.empty() || !(place & 0x40000000u) || now - g_space_place_since < 5.0)
        return;
    uint32_t world = modes::current_world();
    for (size_t i = 0; i < g_space_held.size();) {
        SpaceBuild sb = g_space_held[i];
        if (sb.world != world) { // another world loaded: not this galaxy's planet
            g_space_held.erase(g_space_held.begin() + i);
            continue;
        }
        if (sb.place != place) {
            i++;
            continue;
        }
        if (sb.tried && now < sb.next_try) {
            i++;
            continue;
        }
        g_space_held.erase(g_space_held.begin() + i);
        void* b = space_build_here(sb, sb.tried != 0);
        if (b)
            g_space_held_built++;
        else if (!strcmp(g_build_fail, "no free slot") && (!sb.tried || now - sb.tried < 60.0)) {
            // the planet's cities are made again from its record when it becomes the active planet: a plot freed in
            // this session may come back later, or the city is still settling: tried again every 2 s for 60 s
            sb.tried = sb.tried ? sb.tried : now;
            sb.next_try = now + 2.0;
            g_space_held.insert(g_space_held.begin() + i, sb);
            i++;
            continue;
        } else
            g_space_builds_failed++;
        if (b && is_host())
            space_note_host_build(local_city_near(sb.cp), b, sb.from);
    }
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (!active() && type == net::MSG_CIV_CMD && g_ok && is_host() && game::mode_id() == modes::kSpace && r.left()) {
        net::Reader peek = r;
        if (peek.u8() == CMD_SPACE_BUILD) {
            host_space_build(from, r); // BUG-0751
            return;
        }
    }
    if (!active() && type == net::MSG_CIV_CMD && g_ok && is_guest() && from == 0 && r.left()) { // BUG-0884
        net::Reader peek = r;
        if (peek.u8() == CMD_SPACE_BUILD) {
            guest_space_build(r);
            return;
        }
    }
    if (!active()) {
        if (type == net::MSG_CIV_CMD && g_fix_cmd_hold && g_ok && is_host() && r.left()) { // BUG-0693
            g_cmd_held.push(from, r.cur(), r.left(), g_cmd_world, H->time_seconds());
            H->log(M, SK_INFO, "civ: a command of player %u held: this game is not in Civ (mode %08X)", from, game::mode_id());
        }
        if (type == net::MSG_CIV_BUILD && g_ok && is_guest()) {
            net::Reader peek = r;
            uint8_t ci = peek.u8();
            uint32_t noun = peek.u32();
            if (peek.ok() && ci == kNextCitySite && (noun == 0 || noun == kFoundInstant)) {
                g_founds_outside++;
                H->log(M, SK_INFO, "civ: the host founded a city while this game is outside the Civ mode (mode %08X)",
                       game::mode_id());
            }
        }
        return;
    }
    prune_gone();
    if (type == net::MSG_CIV_STATE && is_guest()) {
        net::Reader peek = r;
        peek.u16();
        uint8_t chunk = peek.u8(), chunks = peek.u8();
        if (peek.ok() && (chunk == kShotChunk || chunk == kShotChunkFired) && chunks == 0) { // BUG-0477: the host's shooters
            if (g_fix_shots)
                guest_shots(peek, chunk == kShotChunkFired);
        } else if (peek.ok() && chunk == kHpChunk && chunks == 0) // BUG-0529: buildings' hit points
            guest_building_hp(peek);
        else if (peek.ok() && chunk == kTurretChunk && chunks == 0) // BUG-0484: turrets
            guest_turrets(peek);
        else if (peek.ok() && chunk == kTribeChunk && chunks == 0) // BUG-0539: old tribe villages
            guest_tribes(peek);
        else
            guest_state(r);
    }
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
    if (restore_speed && g_ok && game::mode_id() == modes::kCiv && nm()) {
        g_exist_frame = ~0ull; // the game's vehicle list of now (a load may have freed the ones listed this frame)
        for (auto& [v, b] : g_boost)
            if (g_fix_boost_restore ? boost_ours(v, b) : exists(v)) {
                F<float>(v, 0x218) = b.orig;
                g_boost_restored++;
            }
    }
    g_boost.clear();
    g_alarm_at.clear();
    g_siege.clear();
    g_joint_told.clear();
    g_city_gifted.clear();
    g_city_captured.clear(); // BUG-0875
    g_capture_by.clear();
    g_bfix_seen.clear();
    g_ui_city = nullptr;
    g_kill_later.clear();
    g_held_builds.clear();
    g_shots.clear(); // BUG-0477
    g_shots_in.clear();
    g_turret_shooters.clear();
    g_hp_sent.clear();
    g_turret_sent.clear();
    g_turret_seen.clear();
    g_tribe_extra_seen.clear();
}

void on_session_end()
{
    g_cmd_held.clear(); // BUG-0693
    g_space_held.clear(); // BUG-0766 / BUG-0884
    g_space_out.clear();
    host_buildings_session_end(); // BUG-0670
    caption_disarm(); // BUG-0224
    if (g_fix_boost)
        clear_coop_tables(true);
    g_shots.clear();
    g_shots_in.clear();
    g_turret_shooters.clear();
    g_hp_sent.clear();
    g_turret_sent.clear();
    g_turret_seen.clear();
    g_tribe_extra_seen.clear();
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
    g_world_resets++;
    g_bld_rekeyed.clear(); // BUG-0670 (the buildings are freed)
    caption_disarm();
    // BUG-0773: after a real load the vehicles are gone and nothing is written (boost_ours: in the game's list now,
    // the same nation, still the speed the mod wrote); a world change seen while they live gives their speed back
    clear_coop_tables(g_fix_boost_restore);
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
    g_cyc.reset();
    g_cycle_ids.clear();
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
                 "trade_pairs=%d trade_total=%d joint_hits=%u military_hits=%u religious=%u alarms=%u votes=%u sw_unvoted=%u boosts=%u "
                 "cities_founded=%u unselected=%u movie=%d buys_refused=%u chunks=%u max_chunk=%u siege_npc=%u\n",
                 (unsigned)g_hv.size(), (unsigned)g_host_cities.size(), (unsigned)g_city_owner.size(), g_cmds_done,
                 g_cmds_rejected, g_builds_sent, g_gifts, g_trade_pairs, g_trade_total, g_joint_hits, g_military_hits,
                 g_religious_boosts, g_alarms, g_votes, g_sw_unvoted, (unsigned)g_boost.size(), g_cities_founded, g_unselected, (int)movie_playing(), g_buys_refused, g_last_chunks,
                 g_max_chunk, g_siege_npc);
    else
        snprintf(b, sizeof b,
                 "civ(guest): mirrors=%u created=%u removed=%u cities=%u cmds_sent=%u builds_in=%u captures_in=%u "
                 "orders_dropped=%u growth_blocked=%u nodes_in=%u cities_in=%u cities_blocked=%u unselected=%u movie=%d "
                 "buildings_fixed=+%u/-%u off_slot=%u moved=%u ruins_in=%u ruins_differ=%u incomplete=%u older_chunks=%u income_blocked=%u cities_caught_up=%u ai_spawns_blocked=%u away_orders=%u local=%d founds_outside=%u mirror_place_skipped=%u removed_local=%u\n",
                 (unsigned)g_gv.size(), g_created, g_removed, (unsigned)g_host_cities.size(), g_cmds_sent, g_builds_in,
                 g_captures_in, g_orders_dropped, g_growth_blocked, g_nodes_in, g_cities_in, g_cities_blocked, g_unselected, (int)movie_playing(), g_bfix_added,
                 g_bfix_removed, g_bfix_off_slot, g_bfix_moved, g_ruins_in, g_ruins_differ, g_incomplete, g_cyc.older, g_income_blocked, g_cities_caught_up, g_ai_spawns_blocked, g_away_orders, (int)local_play(), g_founds_outside, g_mirror_place_skipped, g_removed_local);
    char pl[200];
    snprintf(pl, sizeof pl, "planner: open=%d held=%u kills_held=%u kills_waiting=%u picks_refused=%u builds_held=%u build_orphans=%u save_stripped=%u cards_skipped=%u owner_unknown=%u met=%d\n",
             planner_city() ? 1 : 0, g_planner_held, g_planner_kills_held, (unsigned)g_kill_later.size(),
             g_planner_picks_refused, (unsigned)g_held_builds.size(), g_build_orphans, g_save_stripped, g_cards_skipped, g_owner_unknown, g_met);
    char sh[320];
    snprintf(sh, sizeof sh, "shots: fix=%d seen=%u tx=%u rx=%u active=%u fired=%u no_mirror=%u no_tool=%u hits_dropped=%u hits=%u no_attacker=%u att_vt=%08X targets=%u/%u no_target=%u counted=%d tried=%u refused=%u stops=%u released=%u\n",
             (int)g_fix_shots, g_shots_seen, g_shots_tx, g_shots_rx, (unsigned)g_shots_in.size(), g_shots_fired,
             g_shots_nomirror, g_shots_notool, g_hits_dropped, g_hits_guest, g_hits_noatt, (unsigned)g_hit_att_vt, g_shots_tgt_found, g_shots_tgt_wanted, g_shots_notarget, (int)g_fix_shot_fired, g_shots_tried, g_shots_refused, g_shot_stops_seen, g_shot_released);
    char spb[240]; // BUG-0751
    snprintf(spb, sizeof spb, "space_planner: sent=%u built=%u away=%u failed=%u kept=%u kept_built=%u relayed=%u relay_in=%u dropped=%u city_written=%u\n",
             g_space_builds_sent, g_space_builds_in, g_space_builds_away, g_space_builds_failed,
             (unsigned)g_space_held.size(), g_space_held_built, g_space_relayed, g_space_relay_in,
             g_space_planner_dropped, g_space_city_written);
    char bh[160];
    snprintf(bh, sizeof bh, "building_hp: fix=%d tx=%u rx=%u lowered=%u raised=%u no_match=%u\n", (int)g_fix_building_hp,
             g_hp_tx, g_hp_rx, g_hp_lowered, g_hp_raised, g_hp_nomatch);
    std::string lf = "local_fire:";
    for (auto& [ra, e] : g_local_fire) {
        char x[64];
        snprintf(x, sizeof x, " %08X=%u/%u(vt %08X)", (unsigned)ra, e.fired, e.calls, (unsigned)e.owner_vt);
        lf += x;
    }
    lf += " tools:";
    for (auto& [k, n] : g_shot_tools) {
        char x[32];
        snprintf(x, sizeof x, " %08X=%u", k, n);
        lf += x;
    }
    lf += "\n";
    char tr[120];
    snprintf(tr, sizeof tr, "tribe_ruins: fix=%d tx=%u rx=%u removed=%u\n", (int)g_fix_tribe_ruins, g_tribes_tx, g_tribes_rx,
             g_tribes_removed);
    lf += tr;
    char tu[160];
    snprintf(tu, sizeof tu, "turrets: fix=%d tx=%u rx=%u blocked=%u added=%u removed=%u\n", (int)g_fix_turrets, g_turret_tx,
             g_turret_rx, g_turrets_blocked, g_turrets_added, g_turrets_removed);
    char ts[128];
    snprintf(ts, sizeof ts, "turret_shots: fix=%d seen=%u fired=%u bad_combatant=%u\n", (int)g_fix_turret_weapon,
             g_turret_shots_seen, g_turret_fired, g_bad_comb);
    return std::string(b) + pl + sh + ts + bh + tu + lf + spb;
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
                 "opos=(%.1f %.1f %.1f) scale=%.2f model=%08X\n",
                 id, v, pol_of(v), o == 0xFF ? -1 : (int)o, F<int>(v, kVPurpose), F<int>(v, kVLoco),
                 F<uint8_t>(v, kVSelected), F<float>(v, kVHp), p.x, p.y, p.z, otype, ox, oy, oz, game::object_scale(v),
                 F<uint32_t>(v, kVModelKey));
        out += b;
        auto gi = id && !(C.sess && C.sess->role() == net::Role::Host) ? g_gv.find(id) : g_gv.end();
        if (gi != g_gv.end()) { // BUG-0186: the guest's view of the mirror (on the same line)
            out.pop_back();
            const net::Vec3& ev = gi->second.est.vel;
            snprintf(b, sizeof b, " age=%.1f err=%.1f pushed=%.1f pushed5s=%.1f vel=%.1f top=%.1f jumps=%u htop=%.1f\n",
                     H->time_seconds() - gi->second.seen, gi->second.err, gi->second.pushed, gi->second.pushed_max,
                     std::sqrt(ev.x * ev.x + ev.y * ev.y + ev.z * ev.z), F<float>(v, 0x218), gi->second.est.capped,
                     gi->second.host_top);
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

bool near_vehicle(const net::Vec3& p, float radius)
{
    struct Ctx { const net::Vec3* p; float r; uint8_t me; bool hit; } ctx{&p, radius, (uint8_t)(C.sess ? C.sess->my_id() : 0), false};
    for_each_target([](const char* kind, int, int owner, const net::Vec3& pos, void* user) {
        auto* q = (Ctx*)user;
        if (!q->hit && owner == q->me && !strcmp(kind, "vehicle") && net::dist(pos, *q->p) < q->r)
            q->hit = true;
    }, &ctx);
    return ctx.hit;
}

bool center(net::Vec3& p)
{
    if (!g_ok || game::mode_id() != modes::kCiv)
        return false;
    void* pc = player_civ();
    auto cs = pc ? vec_of((uint8_t*)pc + kCivCities) : std::vector<void*>{};
    if (cs.empty())
        return false;
    p = city_pos(cs[0]);
    return true;
}

std::vector<net::Vec3> local_positions()
{
    std::vector<net::Vec3> v;
    uint8_t me = C.sess ? C.sess->my_id() : 0;
    for (void* x : all_vehicles())
        if (owner_of_vehicle(x) == me)
            v.push_back(game::position(x));
    return v;
}

std::string debug(const std::vector<std::string>& a)
{
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
        char b[420];
        snprintf(b, sizeof b,
                 "tables: hv=%u boost=%u alarm_at=%u siege=%u joint=%u gifted=%u bfix=%u host_cities=%u city_owner=%u "
                 "gv=%u world_resets=%u fix_boost=%d fix_world=%d next_id=%u world_changes=%u "
                 "mirror_designs=%u veh_fallback=%u build_skipped=%u bound_redesigned=%u last_order=%d/%u/%u/%08X\n",
                 (unsigned)g_hv.size(), (unsigned)g_boost.size(), (unsigned)g_alarm_at.size(), (unsigned)g_siege.size(),
                 (unsigned)g_joint_told.size(), (unsigned)g_city_gifted.size(), (unsigned)g_bfix_seen.size(),
                 (unsigned)g_host_cities.size(), (unsigned)g_city_owner.size(), (unsigned)g_gv.size(), g_world_resets,
                 (int)g_fix_boost, (int)g_fix_world, (unsigned)g_next_id, g_world_checks_fired, g_veh_design_checked,
                 g_veh_design_fallback, g_build_skipped, g_bound_redesigned, g_last_order.type, g_last_order.kind,
                 g_last_order.id, g_last_order.noun);
        char c[200]; // BUG-0775 / 0875 / 0877 / 0878
        snprintf(c, sizeof c,
                 "captures: captured=%u credit_cmd=%u credit_hit=%u credit_siege=%u credit_nearest=%u buys_sent=%u "
                 "buys_done=%u refused=%u away_sent=%u\n",
                 (unsigned)g_city_captured.size(), g_credit_by[0], g_credit_by[1], g_credit_by[2], g_credit_by[3],
                 g_city_buys_sent, g_city_buys_done, g_city_buys_refused, g_away_captures_sent);
        return std::string(b) + c;
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
    if (a.size() >= 5 && a[0] == "setdesign") { // setdesign <hex pol> <purpose> <loco> <hex inst>: tests (BUG-0647)
        // the civ's design key of that purpose and locomotion names this vehicle design (a player's own creation)
        void* civ = thiscall<void*>(a_CivByPol, nm(), (uint32_t)strtoul(a[1].c_str(), nullptr, 16));
        uint32_t* k = civ ? thiscall<uint32_t*>(a_DesignKey, civ,
                                                skg::cdecl_call<int>(a_DesignId, atoi(a[2].c_str()), atoi(a[3].c_str())))
                          : nullptr;
        if (!k)
            return "no design slot";
        k[0] = (uint32_t)strtoul(a[4].c_str(), nullptr, 16);
        k[1] = 0x24682294;
        k[2] = 0x40646200;
        return "design set";
    }
    if (!a.empty() && a[0] == "bmodels") // tests (BUG-0670): every city's buildings and their model keys
        return host_buildings_list();
    if (a.size() >= 4 && a[0] == "bdesign") { // bdesign <hex pol> <house|factory|fun|hall> <hex inst>: tests (BUG-0670)
        void* civ = thiscall<void*>(a_CivByPol, nm(), (uint32_t)strtoul(a[1].c_str(), nullptr, 16));
        uint32_t did = a[2] == "factory" ? 0x47C10953 : a[2] == "fun" ? 0x72C49181 : a[2] == "hall" ? 0x99E92F05 : 0x4E3F7777;
        uint32_t* k = civ ? thiscall<uint32_t*>(a_DesignKey, civ, (int)did) : nullptr;
        if (!k)
            return "no design slot";
        k[0] = (uint32_t)strtoul(a[3].c_str(), nullptr, 16);
        k[1] = kBuildingModelType;
        k[2] = 0x40636200;
        return "design set";
    }
    if (a.size() >= 4 && a[0] == "vdesign") { // vdesign <hex pol> <purpose> <loco>: that civ's design key (BUG-0647)
        void* civ = thiscall<void*>(a_CivByPol, nm(), (uint32_t)strtoul(a[1].c_str(), nullptr, 16));
        if (!civ)
            return "no civilization of that political id";
        uint32_t* k = thiscall<uint32_t*>(a_DesignKey, civ,
                                          skg::cdecl_call<int>(a_DesignId, atoi(a[2].c_str()), atoi(a[3].c_str())));
        if (!k)
            return "no design";
        char b[160];
        snprintf(b, sizeof b, "design %08X/%08X/%08X known=%d\n", k[0], k[1], k[2],
                 (int)creations::key_known({k[0], k[1], k[2]}));
        return b;
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
    // siege <city> <player> [hits [pc]]: capture damage by that player (host; a guest while the host is away, its own
    // game); `pc`: the attacker is the player civilization, so the game's own CheckMilitaryCapture takes the city for
    // it at 100 (BUG-0775 / BUG-0878 tests)
    if (a.size() >= 3 && a[0] == "siege" && (is_host() || local_play())) {
        size_t ci = (size_t)atoi(a[1].c_str());
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        uintptr_t ctxf = H->sym("Simulator::GetActivePlanetRecord");
        if (!city)
            return "no such city";
        void* pc = player_civ();
        int hits = a.size() > 3 ? std::clamp(atoi(a[3].c_str()), 1, 200) : 1;
        uint32_t att = a.size() > 4 && a[4] == "pc" && pc ? pol_of(pc) : 0x0BADF00Du;
        float before = F<float>(city, 0x748);
        uint32_t pol0 = pol_of(city);
        for (int i = 0; i < hits && pol_of(city) == pol0; i++) {
            g_hit_by = (uint8_t)atoi(a[2].c_str());
            h_AddCapture(skg::cdecl_call<void*>(ctxf), city, (uint8_t*)city + 0x2F0, pol_of(city), 10.f, att, false);
            g_hit_by = 0xFF;
        }
        char b[128];
        snprintf(b, sizeof b, "capture %.2f -> %.2f pol %08X -> %08X", before, F<float>(city, 0x748), pol0, pol_of(city));
        return b;
    }
    // bhp <city> <hp>: every building of the city but its hall down to that health at most (host; tests, BUG-0775:
    // a real siege that ends in minutes)
    if (a.size() >= 3 && a[0] == "bhp" && is_host()) {
        size_t ci = (size_t)atoi(a[1].c_str());
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        float hp = std::max(1.f, (float)atof(a[2].c_str()));
        int n = 0;
        for (void* bl : vec_of((uint8_t*)city + kCityBuildings))
            if (bl && bl != F<void*>(city, 0x320) /* the hall */ && F<int>(bl, kBuildingState) != 2 && F<float>(bl, kBuildingState + 4) > hp) {
                F<float>(bl, kBuildingState + 4) = hp;
                n++;
            }
        return std::to_string(n) + " buildings at " + std::to_string((int)hp) + " hp";
    }
    // vhp <id> <hp>: that vehicle's health (host; tests, BUG-0775: the besiegers live through the siege)
    if (a.size() >= 3 && a[0] == "vhp" && is_host()) {
        void* v = local_vehicle((uint16_t)atoi(a[1].c_str()));
        if (!v || !exists(v))
            return "no such vehicle";
        F<float>(v, kVHp) = std::max(1.f, (float)atof(a[2].c_str()));
        return "hp " + std::to_string((int)F<float>(v, kVHp));
    }
    // vput <id> <x> <y> <z>: that vehicle put there (spatial SetPosition vt 0x38; host; tests, BUG-0775: a host vehicle
    // standing nearer the city than the besiegers)
    if (a.size() >= 5 && a[0] == "vput" && is_host()) {
        void* v = local_vehicle((uint16_t)atoi(a[1].c_str()));
        if (!v || !exists(v))
            return "no such vehicle";
        float p[3] = {(float)atof(a[2].c_str()), (float)atof(a[3].c_str()), (float)atof(a[4].c_str())};
        vcall<void>((uint8_t*)v + kVSpatial, 0x38 / 4, (const float*)p);
        net::Vec3 q = veh_pos(v);
        char b[96];
        snprintf(b, sizeof b, "put at (%.1f %.1f %.1f)", q.x, q.y, q.z);
        return b;
    }
    // buycity <city> [price]: the game's purchase of a city for the player civilization, as FinishBuyCity makes it
    // (cCity buy 0xBE99A0; 0 = the game's GetBuyPrice) (tests, BUG-0877)
    if (a.size() >= 2 && a[0] == "buycity") {
        size_t ci = (size_t)atoi(a[1].c_str());
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        void* pc = player_civ();
        uintptr_t fb = H->sym("SporeCoop::cCity_Buy");
        if (!city || !pc || !fb)
            return "no such city / civ / symbol";
        float money = thiscall<float>(a_GetMoney, pc);
        uint32_t pol0 = pol_of(city);
        thiscall<void>(fb, city, pol_of(pc), a.size() > 2 ? (float)atof(a[2].c_str()) : 0.f); // through our hook
        char b[160];
        snprintf(b, sizeof b, "buycity %zu pol %08X -> %08X money %.0f -> %.0f sent=%u", ci, pol0, pol_of(city), money,
                 thiscall<float>(a_GetMoney, pc), g_city_buys_sent);
        return b;
    }
    if (a.size() >= 2 && a[0] == "alarm" && is_host()) { // alarm <city>: as if an enemy attacked it
        size_t ci = (size_t)atoi(a[1].c_str());
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        g_alarm_at.erase(city);
        if (a.size() >= 3 && a[2] == "expire") { // BUG-0771: every boost has just run out, boost_tick not run yet
            double now = H->time_seconds();
            for (auto& [v, bo] : g_boost)
                bo.until = now - 0.001;
        }
        h_OnAttacked(city, 0, 0x0BADF00D);
        return "alarm raised";
    }
    if (a.size() >= 2 && a[0] == "hvdrop" && is_host()) { // hvdrop <net id>: out of the host's table, it lives (BUG-0773)
        auto it = g_hv_by_id.find((uint16_t)atoi(a[1].c_str()));
        if (it == g_hv_by_id.end())
            return "no such vehicle";
        g_hv.erase(it->second);
        g_hv_by_id.erase(it);
        return "dropped from the table";
    }
    if (!a.empty() && a[0] == "world_change") { // the tables' reset of a world change, the world staying (BUG-0773)
        on_world_change();
        return "world change run";
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
            snprintf(b, sizeof b, "boost %p orig=%.3f left=%.1f owner=%d\n", v, bo.orig, bo.until - now,
                     g_hv.count(v) ? (int)g_hv[v].owner : -1);
            out += b;
        }
        snprintf(b, sizeof b, "boosts=%zu restored=%u\n", g_boost.size(), g_boost_restored);
        return (out.empty() ? std::string("no boosts\n") : out) + b;
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
    if (a.size() >= 3 && a[0] == "attack" && (is_host() || is_guest())) { // on a guest: the game's call of the guest (BUG-0648)
        // attack <id> <host city index | v<vehicle id>> [type]: the game's own attack order (tests, BUG-0477):
        // type 2 the military attack of a city (default for a city), 1 a unit (default for a vehicle)
        void* v = local_vehicle((uint16_t)atoi(a[1].c_str()));
        void* t = nullptr;
        int type = 2;
        if (a[2][0] == 'v') {
            t = local_vehicle((uint16_t)atoi(a[2].c_str() + 1));
            type = 1;
        } else {
            int ci = atoi(a[2].c_str());
            t = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        }
        if (a.size() >= 4)
            type = atoi(a[3].c_str());
        if (!v || !t)
            return "no such vehicle or target";
        thiscall<int>(a_SetOrder, v, t, type, 1);
        thiscall<void>(a_OrderStamp, v, 0);
        return "attack ordered";
    }
    if (!a.empty() && (a[0] == "tribes" || a[0] == "razetribe")) {
        // tribes: the old Tribe stage tribes in this game; razetribe <i>: the host's game destroys tribe i of that
        // list (DestroyInstance, the last step of its vehicles' raid 0xDCAAB0; tests BUG-0539)
        auto ts = tribes_here();
        if (a[0] == "razetribe") {
            size_t i = a.size() >= 2 ? (size_t)atoi(a[1].c_str()) : 0;
            if (!is_host() || i >= ts.size() || !a_DestroyInst)
                return "no such tribe (host only)";
            thiscall<void>(a_DestroyInst, nm(), ts[i].first);
            return "tribe destroyed";
        }
        std::string out;
        for (auto& [t, p] : ts) {
            char line[96];
            snprintf(line, sizeof line, "tribe %p pos=(%.1f %.1f %.1f)\n", t, p.x, p.y, p.z);
            out += line;
        }
        return out + "tribes " + std::to_string(ts.size());
    }
    if (a.size() >= 2 && (a[0] == "turrets" || a[0] == "addturret" || a[0] == "budget")) {
        // turrets <host city index>: the city's turrets; addturret <ci>: one more in the next free slot (the game's
        // City_AddTurret, tests BUG-0484); budget <ci> <value>: the city's civilization's growth budget (+0x4AC,
        // what the city growth 0xBE5890 buys turrets and buildings with; tests BUG-0484)
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        if (a[0] == "addturret") {
            if (!a_AddTurret)
                return "no turret hook";
            return o_AddTurret(city) ? "turret added" : "no free turret slot";
        }
        if (a[0] == "budget" && a.size() >= 3 && a[2] == "attacked") {
            // budget <ci> attacked: the city counts as attacked now (city +0x98 / +0x9C, the time 0xC00600 compares
            // with the game time: under attack in the last 20 s, the growth buys turrets first; tests BUG-0484)
            uintptr_t tg = H->sym("Simulator::cGameTimeManager::Get"), tf = H->sym("cGameTimeManager_TimeAtStartOfFrame");
            void* mgr = tg ? skg::cdecl_call<void*>(tg) : nullptr;
            if (!mgr || !tf)
                return "no game time";
            uint64_t t = thiscall<uint64_t>(tf, mgr);
            F<uint32_t>(city, 0x98) = (uint32_t)t;
            F<uint32_t>(city, 0x9C) = (uint32_t)(t >> 32);
            return "attacked at " + std::to_string(t);
        }
        if (a[0] == "budget") {
            void* civ = F<void*>(city, kCityCiv);
            if (!civ)
                return "no civilization";
            if (a.size() >= 3)
                F<float>(civ, 0x4AC) = (float)atof(a[2].c_str());
            return "budget " + std::to_string(F<float>(civ, 0x4AC));
        }
        std::string out;
        for (auto& [t, p] : turrets_of(city)) {
            char line[96];
            snprintf(line, sizeof line, "turret %p pos=(%.1f %.1f %.1f)\n", t, p.x, p.y, p.z);
            out += line;
        }
        return out + "turrets " + std::to_string(turrets_of(city).size());
    }
    if (a.size() >= 4 && a[0] == "spawnveh" && is_host()) {
        // spawnveh <host city index> <purpose 0 military | 1 religious | 2 economic> <loco>: a vehicle of the
        // player's civilization at that city, free (tests, BUG-0530: religious shots); the civ's own design for
        // that purpose and locomotion, else the game's generated one (key 0, as cVehicle::Load makes it)
        int ci = atoi(a[1].c_str()), purpose = atoi(a[2].c_str()), loco = atoi(a[3].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        void* civ = player_civ();
        if (!city || !civ)
            return "no such city";
        if (thiscall<int>(a_SlotsLeft, civ, purpose) <= 0)
            return "no vehicle slot left";
        uint32_t* key = thiscall<uint32_t*>(a_DesignKey, civ, skg::cdecl_call<int>(a_DesignId, purpose, loco));
        uint32_t k[3] = {0, 0, 0};
        if (key)
            k[0] = key[0], k[1] = key[1], k[2] = key[2];
        void* v = thiscall<void*>(a_SpawnVehicle, city, purpose, loco, k[0], k[1], k[2], 0); // the city's spawn point
        if (!v)
            return "the game made no vehicle";
        thiscall<int>(a_PushEvent, v, 10);
        char b[96];
        snprintf(b, sizeof b, "spawned %p purpose=%d loco=%d design=%08X", v, F<int>(v, kVPurpose), F<int>(v, kVLoco), k[0]);
        return b;
    }
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
            auto send = [&] {
                net::Writer w;
                w.u8(CMD_BUILD);
                w.u8((uint8_t)ci);
                w.u32(noun);
                w.f32(p.x); w.f32(p.y); w.f32(p.z);
                send_cmd(w);
            };
            if (!g_fix_build_money) // BUG-0613 off: sent before the guest knows it can pay
                send();
            g_applying = true; // the guest's own copy, as its planner would place it
            void* b = build_in_city(city, noun, p, true);
            g_applying = false;
            if (g_fix_build_money && b)
                send(); // only what the guest could build and pay for
            return b ? "built locally, sent to the host" : g_build_fail;
        }
        return build_in_city(city, noun, p, a.size() < 4 || a[3] != "nopay") ? "built" : g_build_fail; // nopay: tests
    }
    if (!a.empty() && a[0] == "roads") { // roads [a b [type]]: every city's trade routes; host (tests): AddTradeRoute a b
        std::string out;
        char b[160];
        if (a.size() >= 3 && is_host() && a_AddTradeRoute) {
            size_t i = (size_t)atoi(a[1].c_str()), j = (size_t)atoi(a[2].c_str());
            if (i >= g_host_cities.size() || j >= g_host_cities.size() || i == j)
                return "no such cities";
            thiscall<void>(a_AddTradeRoute, g_host_cities[i], g_host_cities[j], a.size() > 3 ? atoi(a[3].c_str()) : 0);
            snprintf(b, sizeof b, "route %zu-%zu added: %s\n", i, j, has_route(g_host_cities[i], g_host_cities[j]) ? "yes" : "no");
            out += b;
        }
        for (size_t i = 0; i < g_host_cities.size(); i++) {
            void* c = g_host_cities[i];
            if (!c || !city_listed(c))
                continue;
            for (const Route& r : routes_of(c)) {
                auto it = std::find(g_host_cities.begin(), g_host_cities.end(), r.partner);
                snprintf(b, sizeof b, "route %zu %d type=%d obj=%p noun=%08X\n", i,
                         it == g_host_cities.end() ? -1 : (int)(it - g_host_cities.begin()), r.type, r.obj,
                         r.obj ? game::noun_id(r.obj) : 0);
                out += b;
            }
        }
        snprintf(b, sizeof b, "roads sent=%u in=%u added=%u failed=%u fix=%d\n", g_roads_sent, g_roads_in, g_roads_added,
                 g_roads_failed, (int)g_fix_roads);
        return out + b;
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
                snprintf(line, sizeof line, "obj %p %.0f %.0f %.0f %d %d pol=%08X%s%s\n", o, p.x, p.y, p.z, (int)sc[0],
                         (int)sc[1], pol_of(o), q->type && !game::cast(o, q->type) ? " (not that type)" : "",
                         n == 0x18C6DE8 && F<uint8_t>(o, kVDead) ? " dead" : "");
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
    if (a.size() >= 2 && a[0] == "buildings") { // buildings <host city index | s<i>: Space, all cities>: object, noun, state, hp, position
        int ci = atoi(a[1].c_str() + (a[1][0] == 's'));
        std::vector<void*> sc = a[1][0] == 's' ? all_cities() : std::vector<void*>();
        void* city = a[1][0] == 's' ? (ci >= 0 && ci < (int)sc.size() ? sc[ci] : nullptr)
                                    : ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
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
    if (!a.empty() && a[0] == "civlists") {
        // civlists (tests, BUG-0470): every civilization's vehicle vector (+0xB0) and selectable vector
        // (+0xD4): entries and nulls (a stripped vehicle read back as null)
        std::string out;
        for (void* civ : vec_of(thiscall<void*>(a_GetAllCivs, nm()))) {
            int n[2] = {0, 0}, z[2] = {0, 0};
            const uintptr_t offs[2] = {0xB0, 0xD4};
            for (int k = 0; k < 2; k++) {
                void** b = F<void**>(civ, offs[k]);
                void** e = F<void**>(civ, offs[k] + 4);
                for (void** it = b; it && it < e && n[k] < 512; ++it) {
                    n[k]++;
                    z[k] += *it == nullptr;
                }
            }
            char line[128];
            snprintf(line, sizeof line, "civ %p pol=%08X vehicles=%d nulls=%d selectable=%d nulls=%d\n", civ, pol_of(civ), n[0],
                     z[0], n[1], z[1]);
            out += line;
        }
        return out.empty() ? "no civilizations\n" : out;
    }
    if (a.size() >= 2 && a[0] == "discover") {
        // discover <hex pol> (tests, BUG-0492): the game meets that civilization outside the frame (0xC746F0
        // on the planet context, what a first click on one of its cities does: the discovery card);
        // the result is in the `planner:` status line (met=)
        static uint32_t pol;
        pol = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        if (!H->sym("SporeCoop::CivDiscovery_Meet") || !H->sym("SporeCoop::TimeOfDay_GetObject"))
            return "no symbol";
        g_met = -1;
        H->run_outside_frame(
            [](void*) {
                void* ctx = skg::cdecl_call<void*>(H->sym("SporeCoop::TimeOfDay_GetObject"));
                g_met = ctx ? (int)thiscall<char>(H->sym("SporeCoop::CivDiscovery_Meet"), ctx, &pol) : -2;
            },
            nullptr);
        char b[96];
        snprintf(b, sizeof b, "meeting %08X (civ=%p)", pol, thiscall<void*>(a_CivByPol, nm(), pol));
        return b;
    }
    if (a.size() >= 2 && a[0] == "remove" && is_host()) { // remove <id>: the game removes it (disband path, tests)
        void* v = local_vehicle((uint16_t)atoi(a[1].c_str()));
        if (!v || !exists(v) || !a_VehicleRemove)
            return "no such vehicle";
        thiscall<void>(a_VehicleRemove, v, 0);
        g_exist_frame = ~0ull;
        return "removed";
    }
    if (a.size() >= 2 && a[0] == "kill" && is_host()) { // kill <id>: the vehicle dies (Die with effects, tests)
        void* v = local_vehicle((uint16_t)atoi(a[1].c_str()));
        if (!v || !exists(v))
            return "no such vehicle";
        thiscall<void>(a_Die, v, 1, 0xFFFFFFFFu);
        return "died";
    }
    if (!a.empty() && a[0] == "planner_probe") {
        if (a.size() > 1)
            g_planner_probe = a[1] == "on" || a[1] == "1";
        char line[200];
        snprintf(line, sizeof line, "planner_probe %s frames=%u editor_seen=%u stale_reads=%u stale_cities=%u refused=%u fix=%d\n",
                 g_planner_probe ? "on" : "off", g_probe_frames, g_probe_seen, g_probe_stale_reads, g_probe_stale_cities,
                 g_planner_stale_refused, g_fix_planner_stale ? 1 : 0);
        return line;
    }
    if (!a.empty() && a[0] == "planner") {
        // planner [pick <obj>]: the open city planner (BUG-0491/0493 tests): its city, the objects it moves,
        // the buildings in the world outside every city list (orphans); pick: the planner's own pick-up
        // of that object (what a click on it does)
        std::string out;
        char line[160];
        void* pc = planner_city();
        snprintf(line, sizeof line, "planner editor=%p city=%p host_index=%d\n", g_editor, pc, pc ? host_index_of_city(pc) : -1);
        out += line;
        std::vector<void*> moved;
        planner_moved(moved);
        for (void* sp : moved) {
            void* v = sp ? (uint8_t*)sp - kVSpatial : nullptr;
            bool veh = v && exists(v);
            snprintf(line, sizeof line, "moved %p %s%s\n", sp, veh ? "vehicle" : "object",
                     veh && g_gv_of.count(v) ? " (mirror)" : "");
            out += line;
        }
        if (pc)
            for (void* bl : vec_of((uint8_t*)pc + kCityBuildings)) {
                snprintf(line, sizeof line, "city building %p noun=%08X pick_ok=%d\n", bl, game::noun_id(bl),
                         (int)planner_pick_ok(g_editor, (uint8_t*)bl + 0x34));
                out += line;
            }
        std::unordered_set<void*> in_city;
        for (void* c : all_cities())
            for (void* bl : vec_of((uint8_t*)c + kCityBuildings))
                in_city.insert(bl);
        struct O {
            std::unordered_set<void*>* in;
            std::string* out;
        } o{&in_city, &out};
        game::for_each_object(
            [](void* x, uint32_t n, void* u) {
                auto* o = (O*)u;
                net::Vec3 p;
                if (type_for_noun(n) && game::object_alive(x) && !o->in->count(x) && game::object_position(x, p) &&
                    o->out->size() < 4000) {
                    char l[96];
                    snprintf(l, sizeof l, "orphan %p noun=%08X pos=(%.1f %.1f %.1f)\n", x, n, p.x, p.y, p.z);
                    *o->out += l;
                }
                return true;
            },
            &o);
        if (a.size() >= 3 && a[1] == "pick") {
            void* x = (void*)(uintptr_t)strtoul(a[2].c_str(), nullptr, 16);
            if (!pc || !x || !a_EditorPickUp)
                return out + "no planner open / no object\n";
            int r = thiscall<int>(a_EditorPickUp, g_editor, (uint8_t*)x + 0x34);
            snprintf(line, sizeof line, "pick -> %d held=%p\n", r, F<void*>(g_editor, 0xC0));
            out += line;
        }
        return out;
    }
    if (!a.empty() && a[0] == "scities") { // tests (BUG-0751): every city of this game (Space: s<i>), position, buildings
        std::string out;
        auto sc = all_cities();
        for (size_t i = 0; i < sc.size(); i++) {
            net::Vec3 p = city_pos(sc[i]);
            char line[128];
            snprintf(line, sizeof line, "s%u pos=(%.0f %.0f %.0f) buildings=%u\n", (unsigned)i, p.x, p.y, p.z,
                     (unsigned)vec_of((uint8_t*)sc[i] + kCityBuildings).size());
            out += line;
        }
        return out.empty() ? "no cities\n" : out;
    }
    if (a.size() >= 3 && a[0] == "unbuild" && a[1][0] == 's') {
        // unbuild s<i> <k> (tests, BUG-0751): the game's DestroyBuilding of building k of city s<i> (a free plot)
        auto sc = all_cities();
        int ci = atoi(a[1].c_str() + 1);
        void* city = ci >= 0 && ci < (int)sc.size() ? sc[ci] : nullptr;
        if (!city || !a_DestroyBuilding)
            return "no such city";
        auto bl = vec_of((uint8_t*)city + kCityBuildings);
        size_t k = (size_t)atoi(a[2].c_str());
        if (k >= bl.size())
            return "no such building";
        uint32_t noun = game::noun_id(bl[k]);
        thiscall<void>(a_DestroyBuilding, city, bl[k]);
        char l[96];
        snprintf(l, sizeof l, "destroyed building %u (%08X) of s%d", (unsigned)k, noun, ci);
        return l;
    }
    if (a.size() >= 3 && a[0] == "sbuild" && a[1][0] == 's') {
        // sbuild s<i> <house|factory|fun> (tests, BUG-0901): the mod's build_in_city into city s<i> (not paid), the
        // path of every Space colony building the mod makes (BUG-0751 / 0766 / 0884)
        auto sc = all_cities();
        int ci = atoi(a[1].c_str() + 1);
        void* city = ci >= 0 && ci < (int)sc.size() ? sc[ci] : nullptr;
        if (!city)
            return "no such city";
        uint32_t noun = a[2] == "factory" ? 0x18EA2CC : a[2] == "fun" ? 0x1A56ABA : 0x18EB106;
        g_applying = true;
        void* b = build_in_city(city, noun, city_pos(city), false);
        if (b)
            space_write_city(city); // BUG-0901: as space_build_here
        g_applying = false;
        char l[96];
        snprintf(l, sizeof l, "%s %08X in s%d", b ? "built" : g_build_fail, noun, ci);
        return l;
    }
    if (a.size() >= 3 && a[0] == "unslot") {
        // unslot <host city index> <k> (tests, BUG-0491): the state an orphan has: building k of the city
        // leaves its layout slot (cLayoutSlot::RemoveObject) and stays in the world
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        auto bl = vec_of((uint8_t*)city + kCityBuildings);
        size_t k = (size_t)atoi(a[2].c_str());
        uint8_t* b = F<uint8_t*>(city, kCitySlots);
        uint8_t* e = F<uint8_t*>(city, kCitySlots + 4);
        if (k >= bl.size())
            return "no such building";
        for (uint8_t* s = b; s && s + 0x18 <= e; s += 0x18)
            if (F<void*>(s, 4) == bl[k]) {
                F<uint8_t>(s, 0) = 0; // the slot's flag and occupant as RemoveObject leaves them
                F<int>(s, 8) = 0;
                F<void*>(s, 4) = nullptr; // the city's list keeps its reference
                char l[96];
                snprintf(l, sizeof l, "building %p out of its slot", bl[k]);
                return l;
            }
        return "that building is in no slot";
    }
    if (a.size() >= 3 && a[0] == "vscale") { // vscale <net id> <scale> (tests, BUG-0549): this game's vehicle of that id
        uint16_t id = (uint16_t)atoi(a[1].c_str());
        void* v = nullptr;
        if (is_host()) {
            auto it = g_hv_by_id.find(id);
            v = it == g_hv_by_id.end() ? nullptr : it->second;
        } else
            v = veh_mirror(id);
        if (!v)
            return "no such vehicle";
        game::set_object_scale(v, (float)atof(a[2].c_str()));
        char l[64];
        snprintf(l, sizeof l, "vehicle %u scale %.2f", (unsigned)id, game::object_scale(v));
        return l;
    }
    if (a.size() >= 6 && a[0] == "bmove") { // bmove <city> <k> <x> <y> <z> (tests, BUG-0602): building k of the city to a spot, its slot kept
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        auto bl = vec_of((uint8_t*)city + kCityBuildings);
        size_t k = (size_t)atoi(a[2].c_str());
        if (k >= bl.size())
            return "no such building";
        float q[3] = {(float)atof(a[3].c_str()), (float)atof(a[4].c_str()), (float)atof(a[5].c_str())};
        vcall<void>((uint8_t*)bl[k] + 0x34, 0x38 / 4, (const float*)q);
        net::Vec3 bp{};
        game::object_position(bl[k], bp);
        char l[128];
        snprintf(l, sizeof l, "building %08X at (%.1f %.1f %.1f)", game::noun_id(bl[k]), bp.x, bp.y, bp.z);
        return l;
    }
    if (a.size() >= 2 && a[0] == "blist") { // blist <host city index>: the game's buildings of the city (BUG-0602)
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        std::string out;
        uint8_t* sb = F<uint8_t*>(city, kCitySlots);
        uint8_t* se = F<uint8_t*>(city, kCitySlots + 4);
        for (void* b : vec_of((uint8_t*)city + kCityBuildings)) {
            net::Vec3 p{};
            game::object_position(b, p);
            int slot = -1;
            for (int k = 0; sb && sb + (k + 1) * 0x18 <= se && k < 64; k++)
                if (F<void*>(sb + k * 0x18, 4) == b)
                    slot = k;
            char line[160];
            snprintf(line, sizeof line, "b %08X pos=(%.1f %.1f %.1f) state=%d slot=%d\n", game::noun_id(b), p.x, p.y, p.z,
                     F<int>(b, kBuildingState), slot);
            out += line;
        }
        return out;
    }
    if (a.size() >= 2 && a[0] == "slots") { // slots <host city index | s<i>: Space>: building plots, where they are on screen
        int ci = atoi(a[1].c_str() + (a[1][0] == 's'));
        std::vector<void*> sc = a[1][0] == 's' ? all_cities() : std::vector<void*>();
        void* city = a[1][0] == 's' ? (ci >= 0 && ci < (int)sc.size() ? sc[ci] : nullptr)
                                    : ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
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

void for_each_diag(void (*fn)(const char* key, const net::Vec3& p, void* user), void* user)
{
    if (!active() || is_host())
        return;
    double now = H->time_seconds();
    char k[8];
    for (auto& [id, g] : g_gv) {
        if (!g.v || !game::object_alive(g.v) || now - g.seen > 3.0)
            continue;
        snprintf(k, sizeof k, "w%u", (unsigned)id);
        fn(k, g.est.at(now, net::g_lead_fix ? net::g_one_way : 0.03), user);
        snprintf(k, sizeof k, "e%u", (unsigned)id);
        fn(k, net::Vec3{(float)(now - g.est.t), net::dist(net::Vec3{}, g.est.vel), g.err}, user);
    }
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
