#include "civ.h"
#include "civ_internal.h"
#include "party_difficulty.h"
#include "civ_wallet.h"
#include "own_world.h"
#include "host_busy.h"

namespace civ {

const SkHost* H;
SkMod* M;
Ctx C;
bool g_ok;

// ---- layout (docs/re/civ.md) ----
// game addresses that are not functions (vtable, return addresses): resolved by name in init()
static uintptr_t a_VehicleVt, a_PlannerAddBuildingRet, a_GrowthRetBudget, a_GrowthRetNew;

uintptr_t a_GetAllCivs, a_GetAllCities, a_GetAllVehicles, a_CivByPol, a_PlayerCiv, a_GetMoney, a_SpendMoney,
    a_CreateVehicle, a_SlotsLeft, a_VehicleCost, a_DesignKey, a_DesignId, a_SpawnVehicle, a_AddBuilding,
    a_SetBuildingModel, a_SnapToSlot, a_ChangeOwner, a_Capture, a_SetOrder, a_SetMoveOrder, a_OrderStamp, a_Die,
    a_SetSelected, a_PushEvent, a_BTTick, a_CreateInstance, a_UIMessage, a_CreateBuilding,
    a_NodeClaim, a_NodeUnclaim, a_SetCapture, a_SpawnNextCity, a_ExpansionGet, a_CinematicMgr, a_DestroyBuilding,
    a_CombatantDamage, a_AddMoney;


Counters g_n;

bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
// BUG-0151: while the host is away (main menu) the guest's vehicles run their own AI and the guest's
// orders go to its own game (the host sends no states and drops orders outside Civ).
// `coop.civ fix away 0`: the old frozen vehicles (tests).
bool g_fix_away = true;
COOP_FIX("civ.away", "BUG-0151", g_fix_away, "civ: while the host is away the guest's vehicles run their own AI");
bool local_play() { return civlogic::local_play(g_fix_away, is_guest(), C.host_away && C.host_away()); }
bool active() { return g_ok && C.sess && C.sess->connected() && game::mode_id() == modes::kCiv; }
// BUG-1478 (CIV-5): the guest plays the host's Civ world here; its hooks that hand the player civilization's income, the
// NPC growth and founding, the AI's vehicles, captures, city purchases and superweapons to the host hold only then
// (civlogic::guest_holds). A guest in another Civ world of its own plays it with its own game. Off: they hold in every
// Civ world, the host takes those commands from a guest elsewhere too (0.7.16.1).
bool g_local_play_gate_fix = true;
COOP_FIX("civ.local_play_gate", "BUG-1478", g_local_play_gate_fix,
         "civ: a guest playing another Civ world of its own keeps its own income, growth, captures and superweapons");
bool guest_holds() { return civlogic::guest_holds(g_local_play_gate_fix, is_guest(), game::mode_id() == modes::kCiv, own_world::elsewhere()); }

void* nm() { return game::noun_manager(); }
std::vector<void*> vec_of(void* v)
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
std::vector<void*> all_cities() { return vec_of(thiscall<void*>(a_GetAllCities, nm())); }
std::vector<void*> all_vehicles()
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
uint64_t g_exist_frame = ~0ull;
bool exists(void* v)
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
void* player_civ() { return thiscall<void*>(a_PlayerCiv, nm()); }

// ---------------- the local city planner (BUG-0491, BUG-0493) ----------------
// The city planner (cCommunityEditor, docs/re/civ.md#civ-planner-lifetime) pauses this game and keeps
// raw pointers into the city it edits: the building in the hand (+0xC0), the city's layout slots
// (+0xF4) and a map of objects it moves out of the way (+0x118, vehicles too) that its update
// (0xD124E0) reads every frame. The game itself changes nothing while it is open; the mod did (the
// host's buildings, ruins, captures and mirror removals arrived): a guest crashed in the planner at
// cLayoutSlot::RemoveObject on a garbage slot and on a freed object of that map (0.7.3.6).
constexpr uintptr_t kEdCity = 0x40, kEdMoved = 0x118, kEdMovedSize = 0x128;
constexpr uint32_t kCityType = 0xEE9B2232;
uintptr_t a_EditorUpdate, a_EditorPickUp, a_DestroyInstance, a_VehicleRemove;
void* g_editor;                  // the editor whose update ran last
static uint64_t g_editor_frame = ~0ull; // the frame of that update
// BUG-0682: the editor of the last update is freed with the city input strategy by the stage teardown (the quit to
// the galaxy menu, a load): its memory stays readable, +0x40 garbage, and the next planner_city() within 3 frames
// cast it (a guest's caught fault in game::cast from wsync's city row, campaign_space). Now the editor is trusted
// only in the mode of its update and not while the stage is leaving, and +0x40 must be an object of the game's.
bool g_fix_planner_stale = true;
COOP_FIX("civ.planner_stale", "BUG-0682", g_fix_planner_stale,
         "civ: the city planner's editor is forgotten at the quit / a mode change; its city must have a game vtable");
static uint32_t g_editor_mode;     // the mode of the last update
static bool g_planner_read;        // planner_city() read the editor's +0x40 (BUG-0682 probe)
bool vtable_in_game(const void* obj);

// the city the local planner edits now, or null (the update runs every frame while it is open)
void* planner_city()
{
    if (!g_editor || H->frame_index() - g_editor_frame > 3 || !skg::readable(g_editor, kEdMovedSize + 4))
        return nullptr;
    if (g_fix_planner_stale && (game::leaving_stage() || game::mode_id() != g_editor_mode)) {
        g_editor = nullptr; // (the update sets it again while the planner lives)
        g_n.planner_stale_refused++;
        return nullptr;
    }
    g_planner_read = true;
    void* o = F<void*>(g_editor, kEdCity);
    if (g_fix_planner_stale && o && (!skg::readable(o, 0x10) || !vtable_in_game(o))) {
        g_n.planner_stale_refused++;
        return nullptr;
    }
    return o ? game::cast(o, kCityType) : nullptr;
}
bool planner_holds(void* city) { return city && city == planner_city(); }
bool planner_holds_city(void* city) { return planner_holds(city); }

// BUG-0682 test probe (`coop.civ planner_probe on|off`): planner_city() once a frame in every mode, as wsync's city rows
// and the guest's messages call it; counts the frames in which it read the editor of an update of another mode or
// while the stage was leaving (the freed editor of the teardown) and the cities it returned then
bool g_planner_probe;
void planner_probe_tick()
{
    if (!g_planner_probe || !g_ok)
        return;
    g_n.probe_frames++;
    bool stale = g_editor && (game::leaving_stage() || game::mode_id() != g_editor_mode);
    if (g_editor)
        g_n.probe_seen++;
    g_planner_read = false;
    void* c = planner_city();
    if (stale && g_planner_read) {
        g_n.probe_stale_reads++;
        if (c)
            g_n.probe_stale_cities++;
    }
}
// the spatial objects in the planner's moved-objects map (an eastl rbtree: anchor right +0, left +4,
// root +8; node value at +0x10 = the object's spatial interface, then its target position)
void planner_moved(std::vector<void*>& out)
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
bool planner_moves(void* v)
{
    if (!v)
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
bool planner_pick_ok(void* ed, void* sp)
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
        if (g_n.planner_picks_refused++ < 10)
            H->log(M, SK_INFO, "civ: the city planner picked up a building that is in no slot of its own: refused (BUG-0491)");
        return 2;
    }
    return o_EditorPickUp(ed, sp);
}
// BUG-0492: the "new discovery" card (cUIAssetDiscoveryManager, 0xE30CF0 thiscall(mgr, data) ret 4)
// reads data+0xC at once; the civilization's card data (0xBA5B60) is null when the political id has
// no civilization in this game (GetCivilizationByPoliticalID null): a guest clicked a city whose owner
// its game does not know (0.7.3.6 crash at SetImageForWindow+0x21F). No data: no card.
int g_met = -1; // tests: the last `coop.civ discover`
using DiscoveryCard_t = void(SK_THISCALL*)(void*, void*);
static DiscoveryCard_t o_DiscoveryCard;
static void SK_THISCALL h_DiscoveryCard(void* mgr, void* data)
{
    if (!data) {
        if (g_n.cards_skipped++ < 10)
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
uint32_t pol_of(void* o) { return F<uint32_t>(o, kPol); }
net::Vec3 city_pos(void* c)
{
    auto& p = F<float[3]>(c, kCityPos);
    return {p[0], p[1], p[2]};
}
net::Vec3 veh_pos(void* v)
{
    auto& p = F<float[3]>(v, kVPos);
    return {p[0], p[1], p[2]};
}
net::Quat veh_rot(void* v)
{
    auto& q = F<float[4]>(v, kVRot);
    return {q[0], q[1], q[2], q[3]};
}

// ---------------- ownership ----------------
std::unordered_map<void*, uint8_t> g_city_owner; // both: player city -> player id
std::vector<void*> g_host_cities;                // host: snapshot order; guest: host index -> local
std::unordered_set<void*> g_city_gifted;         // host: cities given away keep their owner
// BUG-0875: a city a player captured keeps its captor when the cities are dealt out again (the shared capital's
// threshold, a join or a leave). `coop.fix civ.capture_keeps off`: every city but the gifts dealt in turns (0.7.12).
bool g_fix_capture_keeps = true;
COOP_FIX("civ.capture_keeps", "BUG-0875", g_fix_capture_keeps,
         "civ: a city a player captured stays that player's when the cities are dealt out again");
std::unordered_set<void*> g_city_captured; // host: cities a player captured
// host: the captor of a capture whose ChangeOwner waits for its cinematic (Capture plays CVG_Capture and the game
// changes the owner at its end, docs/re/civ.md#civ-capture-keeps): the city is not the player civilization's yet
// when h_Capture names the captor, so the drop loop of host_assign_cities forgot it and dealt the city out anew
std::unordered_map<void*, PendingCapture> g_capture_by;
// BUG-0950: the owner tables live only in the host's memory and are cleared by every world change, so a host that
// loaded its Civ world again (the same save after the menu, a load while connected) dealt every city out anew: a
// city a guest captured, bought or was given became another player's. The host keeps the owners of the players'
// cities per world (world::current_game_key) by position and owner name, written every 2 s while it plays, and
// gives them back to the cities of that world after its load (the owner must be in the session).
// `coop.fix civ.owners_kept off`: dealt anew (0.7.12).
bool g_fix_owners_kept = true;
COOP_FIX("civ.owners_kept", "BUG-0950", g_fix_owners_kept,
         "civ: the players' city owners (captured, bought and given cities too) are kept when the host loads the same world again");
std::map<std::string, std::vector<KeptCity>> g_kept_cities; // host: world key -> its players' cities
std::string g_kept_key;                                     // host: the key of the world played now
double g_kept_at = -1;
bool g_kept_tried;   // host: the restore after this world change was tried
static void* g_ui_city;                               // the city whose menu was opened last

// ---------------- host ----------------
// E1 (BUG-1153): the owners of the players' cities and vehicles across sessions. They lived only in the host's memory
// (BUG-0950 kept them for a reload in the same session, by the owner's name): every new session dealt the cities anew
// in turns, a guest's army went to the owner of the nearest city, and a guest that left lost its captured and given
// cities for good. Now every player city and vehicle has a claim: its owner's stable key (store.h key_of; the host is
// "host"). A claim's object goes to that player whenever it is here; while it is away the host holds it for it (never
// another guest), and its join gives it back. The claims go into the per-player store on the host's disk with every
// save (owner_claims.h: by position: world record civ.owners) and come back to the same cities and vehicles after the
// world's load. `coop.fix store.civ_owners off`: dealt as before (0.7.14).
bool g_store_fix = true;
COOP_FIX("store.civ_owners", "BUG-1153", g_store_fix,
         "civ: the players' cities and vehicles keep their owners across sessions (kept on the host's disk) and wait for an absent owner");
std::unordered_map<void*, std::string> g_city_claim, g_veh_claim; // host: object -> its owner's stable key
std::vector<claims::Spot> g_city_spots, g_veh_spots; // host: read from the store, not matched yet
uint32_t g_claims_serial;                             // host: the store read (0: read again)
bool g_cities_matched, g_vehs_matched;
std::map<std::string, uint8_t> g_present;             // host: stable key -> session id, this tick
int present_id(const std::string& key)
{
    auto it = g_present.find(key);
    return it == g_present.end() ? -1 : it->second;
}
// BUG-0040: boosts only on listed vehicles, cleared with the session; BUG-0059: every pointer table cleared when the
// world changes
// BUG-0186 (b): the guest moves a mirror at most 0.15 s ahead of its last state (was 0.4 s)
bool g_extrap_fix = true;
COOP_FIX("civ.extrap", "BUG-0186", g_extrap_fix,
         "civ: a mirrored vehicle is moved on at most 0.15 s past its last state (a host hitch no longer throws it ahead)");
// BUG-0438 (K3): a mirrored vehicle is placed where its target is when the frame is drawn. guest_tick moved it half
// way from where it stood to the target every frame (StopMovement + Teleport: the game does not move it), so a vehicle
// at 15-20 m/s was drawn a frame's glide plus the frame the placing shows up in behind its own target: 0.4-1.8 m (p50)
// between the drawn vehicle and the target, more than the target's own error (coop.bodyrec diag rows, body_trail_civ_loss,
// lab 20261107-074040). With the fix the vehicle goes on with the target's velocity and only the rest (a new state's
// correction, a push) is glided half per frame, aimed one frame ahead (the last frame's time, at most 0.1 s). Off: 0.7.9.
bool g_place_now_fix = true;
COOP_FIX("civ.place_now", "BUG-0438", g_place_now_fix,
         "civ: a mirrored vehicle is drawn on its target, not half a step and a frame behind it");
bool g_vel_cap_fix = true;
COOP_FIX("civ.vel_cap", "BUG-0186", g_vel_cap_fix,
         "civ: a vehicle step faster than twice its top speed is taken as a jump, not a speed to extrapolate");
// BUG-0787: the jump limit came from the guest mirror's own top speed (+0x218), which the host's alarm boost (or a
// faster host design) does not change: a host vehicle over twice as fast had every step taken as a jump, the mirror
// stood and jumped. The host now sends each vehicle's top speed after the snapshot time (a tail an older guest
// ignores) and the guest takes the larger of the two.
bool g_vel_cap_host_fix = true;
COOP_FIX("civ.vel_cap_host", "BUG-0787", g_vel_cap_host_fix,
         "civ: a guest's jump limit for a host vehicle uses the host vehicle's top speed (a boosted vehicle glides, it does not stand and jump)");
// BUG-1053 (class BUG-0426, the live pointer package L2 of 0.7.14): the host's tables (vehicles: g_hvr) and g_host_cities
// are rebuilt from the game's lists every kHostInterval; a guest's command (and the held commands drained before
// host_tick) ran SetOrder / SetMoveOrder on a vehicle or a target the host's game removed since (disbanded, destroyed
// by a superweapon: still in the table, destroyed and freed a few frames later), and the game's hooks inside its update
// (h_Capture, h_Convert) and the mod's walks (for_each_owned, for_each_target, own_selection) read such an entry's
// memory. Now an entry is used only while it is the same object in the game's list (game::alive with the serial the
// table kept; a city: game::listed), inside a game::LiveScope (one walk). `coop.fix civ.cmd_listed off`: as 0.7.13.
static bool g_cmd_listed_fix = true;
COOP_FIX("civ.cmd_listed", "BUG-1053", g_cmd_listed_fix,
         "civ: the host never runs a guest's order on a vehicle, target or city its game removed since the last table cycle");
// host: the vehicle of an entry is the one it was made for and in the game's list now
bool hv_live(const HvRef& x)
{
    if (!g_cmd_listed_fix || game::alive(game::LiveRef{x.v, x.serial, kVehicleNoun}))
        return true;
    g_n.cmd_gone++;
    return false;
}
// host: city i of g_host_cities while the game's list has it (null otherwise or out of range)
void* host_city(size_t i)
{
    void* c = i < g_host_cities.size() ? g_host_cities[i] : nullptr;
    if (c && g_cmd_listed_fix && !game::listed(c)) {
        g_n.cmd_gone++;
        return nullptr;
    }
    return c;
}
uint16_t g_next_id = 1;
double g_last_send;
uint16_t g_cycle;
int g_build_from = -1; // guest whose build is being executed (not echoed back to it)
int g_pending_owner = -1; // owner of the vehicle the host is about to spawn for a guest
std::vector<uint8_t> g_split_for;
// BUG-0031: vehicles of a player who left get a present owner; a capture never names an absent
// player

// ---------------- guest ----------------
// BUG-0951 (class BUG-0426, as BUG-0844 for the Tribe mirrors): a mirror's vehicle is kept across frames and was checked
// by its own memory (object_alive). The game frees a removed vehicle later and gives the block to the next vehicle it
// makes (2 tries in the lab, civ_mirror_lifetime). prune_gone (every Civ frame: the game's vehicle list) drops a mirror
// whose vehicle left the list before it is freed, so the old check was not reached (the test passes with the switch
// off too: lab 20261112-050242); outside the Civ mode prune_gone does not run. Now a mirror's vehicle is used only
// while it is the object bound (the object list and its serial), and the host's table gives a new vehicle at a known
// address of the same nation a new id. Old (`coop.fix civ.mirror_lifetime off`): object_alive.
bool g_veh_lifetime_fix = true;
COOP_FIX("civ.mirror_lifetime", "BUG-0951", g_veh_lifetime_fix, "civ: a guest never drives or removes a vehicle of its own game that the game built where a removed copy of a host's vehicle was");
bool g_veh_hold; // tests (BUG-0951): the guest ignores the host's vehicle states (as during the host's editor)


// BUG-1051 (the Civ part): civ::on_world_change resets the tracker, the session end did not: a guest that joined
// another (or a restarted) host without a world change in between dropped its Civ snapshots as older. Off: 0.7.13.
static bool g_cycle_reset_fix = true;
COOP_FIX("civ.cycle_reset", "BUG-1051", g_cycle_reset_fix, "civ: a guest's Civ snapshot cycle starts over with every session");
SESSION_STATE("civ.cycle", &g_cycle_reset_fix, [] { g_rx.reset(); /* (its counters stay) */ }, []() -> size_t { return g_rx.held(); });
size_t g_chunk_budget = net::kMaxPacket - 48; // host: bytes per chunk (coop.civ chunk <bytes>, tests)
bool& g_cycle_order_fix = tribe::g_cycle_order_fix; // tribe.cpp (net.cycle_order, BUG-0612)
bool g_applying; // executing a host message: the hooks let the game act
CALL_GUARD("civ.applying", g_applying); // BUG-1047

// BUG-0186: Vehicle_KeepOnValidGround (0xCA7430, thiscall(v), from the vehicle's movement updates)
// checks the planet cell under a vehicle against its locomotion (land / sea / air flags) and, when the
// cell is not valid, puts it on the nearest valid spot (SetPosition). The host's NPC vehicle stood
// on a spice node the guest's planet flags as invalid ground: the guest's game threw the mirror 13 m
// away every frame and guest_tick put it back (13-15 m off in every play_civ seed 9 / 11 run).
// A mirror stands where the host's vehicle stands: the check is skipped for mirrors on a guest.
using KeepGround_t = void(SK_THISCALL*)(void*);
static KeepGround_t o_KeepGround;
// BUG-1056: while the host is away (local_play, BUG-0151) the guest's own game drives its mirrors and nothing puts
// them back where the host's vehicle is: the game's own ground check runs for them again (a mirror driven onto ground
// its locomotion does not allow is moved off it, as in the host's game). `coop.fix civ.away_ground off`: skipped
// also while the host is away (0.7.13).
static bool g_away_ground_fix = true;
COOP_FIX("civ.away_ground", "BUG-1056", g_away_ground_fix,
         "civ: while the host is away the guest's game keeps its mirror vehicles on valid ground (its own ground check)");
static void SK_THISCALL h_KeepGround(void* v)
{
    if (g_away_ground_fix && local_play() && gv_bound(v)) {
        g_n.away_ground_checks++;
        o_KeepGround(v);
        return;
    }
    if (!g_applying && is_guest() && game::mode_id() == modes::kCiv && gv_bound(v)) {
        g_n.mirror_place_skipped++;
        return;
    }
    o_KeepGround(v);
}
uint8_t owner_of_vehicle(void* v)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        HvRef it = hv_find(v);
        return it ? it.h->owner : 0xFF;
    }
    GvRef it = gv_of(v);
    return it ? it.g->owner : 0xFF;
}

uint8_t owner_of_city(void* c)
{
    auto it = g_city_owner.find(c);
    return it == g_city_owner.end() ? 0xFF : it->second;
}

int host_index_of_city(void* c)
{
    for (size_t i = 0; i < g_host_cities.size(); i++)
        if (g_host_cities[i] == c)
            return (int)i;
    return -1;
}

void* nearest_player_city(const net::Vec3& p, uint8_t* owner)
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


static Target encode_target(void* t)
{
    Target r;
    if (!t)
        return r;
    if (GvRef it = gv_of(t)) {
        r.kind = 1;
        r.id = it.id;
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

void write_target(net::Writer& w, const Target& t)
{
    w.u8(t.kind); w.u16(t.id); w.u32(t.noun);
    w.f32(t.pos.x); w.f32(t.pos.y); w.f32(t.pos.z);
}

Target read_target(net::Reader& r)
{
    Target t;
    t.kind = r.u8(); t.id = r.u16(); t.noun = r.u32();
    t.pos.x = r.f32(); t.pos.y = r.f32(); t.pos.z = r.f32();
    return t;
}


static void say_host_away(const char* what); // BUG-0693
bool local_play();
void send_cmd(net::Writer& w)
{
    if (local_play())
        say_host_away("your command");
    C.sess->send_reliable_to(0, net::MSG_CIV_CMD, w);
    g_n.cmds_sent++;
}

SetOrder_t o_SetOrder;
LastOrder g_last_order;
static int SK_THISCALL h_SetOrder(void* v, void* target, int type, int flag)
{
    if (local_play() && !g_applying)
        g_n.away_orders++;
    else if (!g_applying && is_guest() && game::mode_id() == modes::kCiv) {
        if (GvRef it = gv_of(v)) {
            // a mirror of another player's or nation's vehicle: the guest's own civ AI orders the
            // other nations' vehicles every few frames (hundreds of rejected commands a minute)
            if (!civlogic::owns(it.g->owner, C.sess->my_id())) {
                g_n.orders_dropped++;
                return 0;
            }
            net::Writer w;
            w.u8(CMD_ORDER);
            w.u16(it.id);
            w.u32((uint32_t)type);
            w.u32((uint32_t)flag);
            Target et = encode_target(target);
            g_last_order = {type, et.kind, et.id, et.noun}; // BUG-0648: what the guest's game ordered
            write_target(w, et);
            send_cmd(w);
            return 0;
        }
    } else if (!g_applying && is_host() && game::mode_id() == modes::kCiv) {
        if (HvRef it = hv_find(v)) {
            if (ff_try_order(it.id, v, it.h->owner, target, type))
                return 0;
        }
    }
    return o_SetOrder(v, target, type, flag);
}

// BUG-0899: find another player's live vehicle within `max_d` metres of `(x,y,z)` (for right-click move -> FF attack).
static void* ff_vehicle_near(uint8_t my_owner, float x, float y, float z, float max_d = 5.0f)
{
    net::Vec3 p{x, y, z};
    void* best = nullptr;
    float bd = max_d;
    for (void* cand : all_vehicles()) {
        if (!cand || F<uint8_t>(cand, kVDead) || F<float>(cand, kVHp) <= 0)
            continue;
        uint8_t co = owner_of_vehicle(cand);
        if (co >= net::kMaxPlayers || co == my_owner)
            continue;
        net::Vec3 cp{};
        if (!game::object_position(cand, cp))
            continue;
        float d = net::dist(cp, p);
        if (d < bd) {
            bd = d;
            best = cand;
        }
    }
    return best;
}

SetMoveOrder_t o_SetMoveOrder;
static int SK_THISCALL h_SetMoveOrder(void* v, float x, float y, float z, int flag)
{
    if (local_play() && !g_applying)
        g_n.away_orders++;
    else if (!g_applying && is_guest() && game::mode_id() == modes::kCiv) {
        if (GvRef it = gv_of(v)) {
            if (!civlogic::owns(it.g->owner, C.sess->my_id())) {
                g_n.orders_dropped++;
                return 0;
            }
            if (ff_on() && F<int>(v, kVPurpose) == 0) {
                if (void* partner_v = ff_vehicle_near(it.g->owner, x, y, z))
                    return h_SetOrder(v, partner_v, 1, flag);
            }
            net::Writer w;
            w.u8(CMD_MOVE);
            w.u16(it.id);
            w.f32(x); w.f32(y); w.f32(z);
            w.u32((uint32_t)flag);
            send_cmd(w);
            return 0;
        }
    } else if (!g_applying && is_host() && game::mode_id() == modes::kCiv) {
        if (HvRef it = hv_find(v)) {
            if (ff_on() && F<int>(v, kVPurpose) == 0) {
                if (void* partner_v = ff_vehicle_near(it.h->owner, x, y, z))
                    return h_SetOrder(v, partner_v, 1, flag);
            }
            ff_cancel_vehicle(it.id);
        }
    }
    return o_SetMoveOrder(v, x, y, z, flag);
}

// BUG-0693: a guest's command that reaches a host whose Civ is not running (an editor, a menu, a load) is held and run
// when the host is back in the same Civ world (net/held_cmds.h); the guest is told that it waits. Off: dropped (0.7.10).
static bool g_fix_cmd_hold = true;
COOP_FIX("civ.cmd_hold", "BUG-0693", g_fix_cmd_hold,
         "civ: a guest's purchase, planner building, gift or superweapon request made while the host is away runs when it is back");
// C3 (BUG-1142): the one held-command pump of busy_model.h (Civ's row: held, run after kSettle s back in the stage,
// the guest told at most every 5 s that its command waits)
static busy_model::HeldPump g_held;
static const busy_model::Row& held_row() { return *busy_model::row_of(stage_epoch::index_of(modes::kCiv)); }
PLAYER_STATE("civ.held_cmds", [](uint8_t id) { g_held.forget(id); }, // BUG-0422 / BUG-0693: a leaver's held commands
             [](uint8_t id) -> size_t { return g_held.held_of(id); });
static uint32_t g_cmd_world; // BUG-0693: the save this game last played the stage in (modes::current_world)
static void say_host_away(const char* what)
{
    double t = H ? H->time_seconds() : 0;
    if (!g_fix_cmd_hold || !C.feed || !g_held.say_due(held_row(), t))
        return;
    C.feed(std::string(what) + " waits: " + C.sess->name_of(0) + " (host) is away, it is done when the host is back");
}

void request_buy(void* city, int loco)
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

uint32_t type_for_noun(uint32_t noun)
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
uint32_t building_design_id(uint32_t type)
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
static std::unordered_set<uint32_t> g_build_skipped_logged;
bool building_model_ok(void* city, uint32_t type, uint32_t noun)
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
    g_n.build_skipped++;
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
    g_n.builds_sent++;
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
// BUG-0901: a colony city is made again from its cCityData (city +0x2F0; buildings +0x7C, 0x38 bytes each) when its
// planet becomes active (0xFF6900); the game writes the live city into it through City_WriteCityData (0xBD9FA0: the
// planner's exit 0xD100B0, a building's damage, turrets), cCity::AddBuilding does not. A colony building the mod built
// (build_in_city: BUG-0751 / 0766 / 0884) was gone after that game's ship left the planet and landed again (lab
// 20261110-201410). Now the city is written after it. Off: not written (0.7.11).
static bool g_fix_space_city_data = true;
COOP_FIX("civ.space_city_data", "BUG-0901", g_fix_space_city_data,
         "civ: a Space colony building the mod builds is written into the city's data (kept when the planet is left)");
static uintptr_t a_WriteCityData;
void space_write_city(void* city)
{
    if (!g_fix_space_city_data || !a_WriteCityData || !city || !F<void*>(city, 0x2F0))
        return;
    thiscall<void>(a_WriteCityData, city); // fastcall(ecx = city), plain ret: as thiscall with no stack arguments
    g_n.space_city_written++;
}
static void space_note_host_build(void* city, void* b, int except, bool planner = false);
static void space_tick(double now);

// BUG-1417 (audit CIV-7, the Civ half of BUG-0902): the guest's CMD_BUILD (BUG-0189) went at the planner's
// AddBuilding; a drop that does not end on a free plot adds the building and takes it away again, so the host built
// (unpaid) a building the guest's planner never kept, or refunded its claimed price when it could not. The guest now
// sends it once the planner closed with the building still in the city (or after 10 min), with its placed position;
// one taken away again is not sent. Off: sent at AddBuilding (0.7.16).
static bool g_fix_planner_closed = true;
COOP_FIX("civ.planner_closed", "BUG-1417", g_fix_planner_closed,
         "civ: a guest's city planner building goes to the host only when the planner closed with it");
struct PlannerOut {
    void* city;
    void* b;
    uint32_t noun;
    float price; // GetCost when the planner added it (BUG-0807)
    double t;
    bool refunded; // BUG-1532: the planner sold it back and its refund went through
};
static std::vector<PlannerOut> g_planner_out;
static std::unordered_map<void*, double> g_planner_sent_at; // city -> when its building went to the host
uint32_t g_planner_adds, g_planner_queued, g_planner_sent, g_planner_dropped, g_planner_refunds;
static uintptr_t a_PlannerMoneyAddRet; // SporeCoop::CommunityEditor_Money_AddRet (optional)
// BUG-1532: the planner sells a dropped object back off the offered plots (CommunityEditor_Sell + a refund through
// CommunityEditor_Money -> AddMoney); a guest's Civ income block dropped that refund, so a palette building taken away
// again cost its price (0.7.16 hid it: the host built the house sent at AddBuilding). With civ.planner_closed the refund
// of a building this planner added and has not sent goes through (the shared money brings it to the host); the sale of
// a building the host has stays blocked. True for that AddMoney (and marks the building refunded).
static bool planner_refund_ours(void* ret)
{
    if (!a_PlannerMoneyAddRet || (uintptr_t)ret != a_PlannerMoneyAddRet || !g_editor || !planner_city())
        return false;
    void* hand = F<void*>(g_editor, kEdHand);
    for (PlannerOut& o : g_planner_out)
        if (hand && (uint8_t*)o.b + kVSpatial == hand && !o.refunded) {
            o.refunded = true;
            return true;
        }
    return false;
}
static void send_build_cmd(void* city, uint32_t noun, const net::Vec3& pos, float price)
{
    int ci = host_index_of_city(city);
    if (ci < 0)
        return;
    net::Writer w;
    w.u8(CMD_BUILD);
    w.u8((uint8_t)ci);
    w.u32(noun);
    w.f32(pos.x); w.f32(pos.y); w.f32(pos.z);
    w.f32(price); // BUG-0807: its price (GetCost), paid in this planner
    send_cmd(w);
}
// a city whose planner building was sent a moment ago: the guest's reconciliation waits for the host's echo (the host's
// snapshot of before the send does not have it yet)
bool planner_send_holds(void* city)
{
    auto it = g_planner_sent_at.find(city);
    return it != g_planner_sent_at.end() && H->time_seconds() - it->second < 5.0;
}
static void planner_out_tick(double now)
{
    if (g_planner_out.empty())
        return;
    auto cities = all_cities();
    void* open = planner_city();
    for (size_t i = 0; i < g_planner_out.size();) {
        PlannerOut& o = g_planner_out[i];
        bool city_ok = std::find(cities.begin(), cities.end(), o.city) != cities.end();
        bool still_open = city_ok && open == o.city && now - o.t < 600.0;
        if (still_open) {
            i++;
            continue;
        }
        auto bl = city_ok ? vec_of((uint8_t*)o.city + kCityBuildings) : std::vector<void*>{};
        bool in = std::find(bl.begin(), bl.end(), o.b) != bl.end(); // (pointers only: a removed one may be freed)
        bool kept = civlogic::planner_kept(city_ok, in, in && game::noun_id(o.b) == o.noun);
        if (kept) {
            net::Vec3 pos{};
            game::object_position(o.b, pos);
            send_build_cmd(o.city, o.noun, pos, o.price);
            g_planner_sent_at[o.city] = now;
            g_planner_sent++;
            H->log(M, SK_INFO, "civ: planner: the building %08X the planner kept goes to the host (BUG-1417)", o.noun);
        } else {
            g_planner_dropped++;
            H->log(M, SK_INFO, "civ: planner: a building %08X the planner added was taken away again: not sent (BUG-1417)%s",
                   o.noun, o.refunded ? "; sold back, its price came back (BUG-1532)" : "");
        }
        g_planner_out.erase(g_planner_out.begin() + i);
    }
}
void planner_out_clear()
{
    g_planner_out.clear();
    g_planner_sent_at.clear();
}
std::string planner_out_status()
{
    char b[180];
    snprintf(b, sizeof b, "planner_out: fix=%d adds=%u waiting=%u queued=%u sent=%u dropped=%u refunds=%u\n",
             (int)g_fix_planner_closed, g_planner_adds, (unsigned)g_planner_out.size(), g_planner_queued, g_planner_sent,
             g_planner_dropped, g_planner_refunds);
    return b;
}

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
        float price = vcall<float>(b, 0x5C / 4); // BUG-0807: its price (GetCost), paid in this planner
        g_planner_adds++;                        // (tests: the planner's AddBuilding ran, fix on or off)
        if (g_fix_planner_closed) { // BUG-1417: once the planner closed with it
            if (host_index_of_city(city) >= 0 && g_planner_out.size() < 64) {
                auto it = std::find_if(g_planner_out.begin(), g_planner_out.end(), [&](const PlannerOut& o) { return o.b == b; });
                if (it != g_planner_out.end()) // the same block again (taken away, a new one there): the newest
                    *it = {city, b, noun, price, H->time_seconds(), false};
                else
                    g_planner_out.push_back({city, b, noun, price, H->time_seconds(), false});
                g_planner_queued++;
            }
        } else
            send_build_cmd(city, noun, pos, price);
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
    if (!g_applying && guest_holds() && (ret == a_GrowthRetBudget || ret == a_GrowthRetNew)) {
        g_n.growth_blocked++;
        return nullptr; // both callers ignore the result (the host's growth comes as MSG_CIV_BUILD)
    }
    return o_CreateBuilding(city, noun, type);
}

// New cities of other nations appear over time at the planet's city sites (0xAE3A80 picks the next
// free site, the city materializes a few seconds later through a timed callback, docs/re/civ.md 9).
// The guest's own planet tick founded cities the host never had (and the host's came without their
// first buildings): guests only found the cities the host founds, in the same order.
// (ex, bool instant, void* who): the runtime-checked order (docs/re/civ.md, BUG-0134)
SpawnNextCity_t o_SpawnNextCity;
// BUG-0120: a founding broadcast while a guest was not in Civ (receiving or loading the world) was
// lost. The guest compares the host's city count of every snapshot with its own and founds the next
// site itself when it stays behind (`coop.civ fix cities 0`: only the event, the code before).
bool g_fix_cities = true;
COOP_FIX("civ.cities", "BUG-0120", g_fix_cities, "civ: a guest founds an NPC city it missed from the host's count");
double g_city_short_since = -1, g_last_city_spawn = -1e9;
// BUG-0624: an instant founding of the host (SpawnNextCity(ex, 1, 0): the campaign's shortcut, test tools) was not
// broadcast: guests found it only through the catch-up (2 s short, then a founding with the appearance timer, ~3 s
// at 25 fps), so the city and every vehicle of its nation were missing there 4-7 s and the sync-check reported
// "v: 1 missing on the guest" whenever the host ran at low fps (civ_city_layout, 3 of 3 on a loaded PC). Now the
// host broadcasts it too (noun 1 = instant) and a guest founds it instantly at once; the catch-up founds instantly
// as well (the city is late already). Off: not broadcast, the catch-up with the timer (0.7.8).
bool g_instant_found_fix = true;
COOP_FIX("civ.instant_found", "BUG-0624", g_instant_found_fix,
         "civ: a city the host founds instantly is founded instantly on the guests at once, not by the catch-up seconds later");
static bool SK_THISCALL h_SpawnNextCity(void* ex, int instant, void* who)
{
    if (!g_applying && guest_holds()) { // (BUG-1478: a guest in its own world founds its own)
        g_n.cities_blocked++;
        return false; // both callers ignore the result
    }
    bool r = o_SpawnNextCity(ex, instant, who);
    if (r && is_host() && game::mode_id() == modes::kCiv && !who && (!instant || g_instant_found_fix)) {
        net::Writer w;
        w.u8(kNextCitySite);
        w.u32(instant ? kFoundInstant : 0);
        w.f32(0); w.f32(0); w.f32(0);
        C.sess->broadcast_reliable(net::MSG_CIV_BUILD, w);
        g_n.cities_founded++;
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
constexpr uintptr_t kCityRoutes = 0x55C; // vector<{cCity* partner, route object*, int type}>, stride 0xC
uintptr_t a_AddTradeRoute;
static double g_roads_sent_at = -100;
static std::vector<std::array<uint8_t, 3>> g_roads_last; // host: the list sent last

std::vector<Route> routes_of(void* city)
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

void host_send_roads(double now)
{
    if (!a_AddTradeRoute)
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
    g_n.roads_sent++;
}

bool city_listed(void* c)
{
    auto all = all_cities();
    return c && std::find(all.begin(), all.end(), c) != all.end();
}

bool has_route(void* a, void* b)
{
    for (const Route& r : routes_of(a))
        if (r.partner == b)
            return true;
    return false;
}

bool planner_holds(void* city);
void guest_roads(net::Reader& r)
{
    uint8_t n = r.u8();
    std::vector<std::array<uint8_t, 3>> list;
    for (int i = 0; i < n && r.ok(); i++) {
        std::array<uint8_t, 3> x{r.u8(), r.u8(), r.u8()};
        list.push_back(x);
    }
    if (!r.ok())
        return;
    g_n.roads_in++;
    if (!a_AddTradeRoute || game::mode_id() != modes::kCiv)
        return;
    // BUG-1287: a route between two of the host's cities that the host's list does not have any more goes here too
    // (the game removes routes in its AI and diplomacy, which run on the host only); a full list (200) may be cut short
    if (list.size() < 200) {
        size_t n = std::min<size_t>(g_host_cities.size(), 250);
        for (size_t i = 0; i < n; i++) {
            void* a = g_host_cities[i];
            if (!a || !city_listed(a) || planner_holds(a))
                continue;
            for (const Route& rt : routes_of(a)) {
                auto it = std::find(g_host_cities.begin(), g_host_cities.begin() + n, rt.partner);
                size_t j = (size_t)(it - g_host_cities.begin());
                if (j >= n || j == i || planner_holds(rt.partner))
                    continue;
                uint8_t lo = (uint8_t)std::min(i, j), hi = (uint8_t)std::max(i, j);
                bool host_has = std::any_of(list.begin(), list.end(), [&](const std::array<uint8_t, 3>& x) {
                    return x[0] == lo && x[1] == hi;
                });
                if (!host_has && remove_route(a, rt.partner)) {
                    H->log(M, SK_INFO, "civ: the trade route %u-%u the host's game removed goes here too (BUG-1287)", lo, hi);
                    break; // (this city's route list changed: its next route at the next list)
                }
            }
        }
    }
    for (auto& x : list) {
        void* a = x[0] < g_host_cities.size() ? g_host_cities[x[0]] : nullptr;
        void* b = x[1] < g_host_cities.size() ? g_host_cities[x[1]] : nullptr;
        if (!a || !b || a == b || !city_listed(a) || !city_listed(b) || planner_holds(a) || planner_holds(b) || has_route(a, b))
            continue;
        {
            callguard::Hold applying(g_applying);
            thiscall<void>(a_AddTradeRoute, a, b, (int)x[2]);
        }
        if (has_route(a, b)) {
            g_n.roads_added++;
            H->log(M, SK_INFO, "civ: the host's trade route %u-%u (type %u) added here (BUG-0494)", x[0], x[1], x[2]);
        } else if (g_n.roads_failed++ < 5)
            H->log(M, SK_WARN, "civ: the host's trade route %u-%u (type %u) did not take here", x[0], x[1], x[2]);
    }
}

// Money is the host's: the guest's local economy (its own copy of the claimed spice derricks, city
// income) ran too and every local gain went to the host as a shared-money delta (+80..+100 every few
// seconds: the treasury grew about twice as fast with a guest). Guests ignore AddMoney for the player
// civilization; their spending still reaches the host (SpendMoney is another function).
// BUG-1236: the game's passive income of the player civilization on the host (the 6 s city and spice derrick tick, the
// two automatic trade deliveries: docs/re/civ.md#civ-income-addmoney) is told to shared.cpp so that it is no
// "<host> earned N money (shared)" notice on every guest every 6 s; the total still carries it. The same three callers
// are the income civ.own_money splits by cities (BUG-1468, civ_wallet.cpp): one classifier for both.
// `coop.fix civ.income_cause off`: the passive income counted as the host's earning (0.7.16.1).
bool g_income_cause_fix = true;
COOP_FIX("civ.income_cause", "BUG-1236", g_income_cause_fix,
         "civ: the cities' passive income on the host is not shown on the guests as the host's earning every 6 s");
static constexpr uintptr_t kCivWealth = 0x98;
static uintptr_t g_passive_income_ret[3]; // SporeCoop::CivIncome_AddMoney_Ret, CivTrade_CityAddMoney_Ret, CivTrade_TraderAddMoney_Ret
static uint32_t g_passive_noted;          // tests
static bool is_passive_income_ret(uintptr_t ret)
{
    for (uintptr_t a : g_passive_income_ret)
        if (a && a == ret)
            return true;
    return false;
}
AddMoney_t o_AddMoney;
static void SK_THISCALL h_AddMoney(void* civ, float amount, int difficulty)
{
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    // BUG-1478: a guest in another Civ world of its own keeps its own income
    bool in_civ = game::mode_id() == modes::kCiv, guest = is_guest(), holds = guest_holds();
    if (civlogic::guest_income_blocked(g_applying, guest, holds, guest && in_civ && civ == player_civ(), amount)) {
        if (!civlogic::planner_refund_passes(g_fix_planner_closed, ret == a_PlannerMoneyAddRet,
                                             g_fix_planner_closed && planner_refund_ours((void*)ret))) {
            g_n.income_blocked++;
            return;
        }
        g_planner_refunds++; // BUG-1532: the sell-back of a building this guest's planner added and did not send
        H->log(M, SK_INFO, "civ: planner: a building the planner added was sold back: its refund %.0f goes through (BUG-1532)",
               amount);
    }
    bool passive = !g_applying && in_civ && is_host() && civ && is_passive_income_ret(ret) && civ == player_civ() &&
                   skg::readable(civ, kCivWealth + sizeof(float));
    if (!passive) {
        o_AddMoney(civ, amount, difficulty);
        return;
    }
    double before = (double)F<float>(civ, kCivWealth);
    o_AddMoney(civ, amount, difficulty);
    double gain = (double)F<float>(civ, kCivWealth) - before; // the game's difficulty and its cap are in it
    if (!std::isfinite(gain) || gain == 0.0)
        return;
    double out = civ_wallet::host_income(civ, gain); // BUG-1468: the guests' parts by their cities
    if (g_income_cause_fix) {
        shared::note_passive(2, gain - out); // the guests' parts leave the money: what stays is still passive
        g_passive_noted++;
    }
}

// BUG-1461: an NPC nation's AI update (Civilization_AI 0xBFBD30, every second) moves all of its money (+0x98) into its
// domestic and expansion budgets (+0x4AC / +0x4B0) in CivAI_WealthToBudgets (docs/re/civ.md#civ-npc-money-budgets). On a
// guest the money is the host's (the wsync row civ_money writes +0x98 every 0.5 s); while the host's world stands (a
// caption) the host's AI does not run, the guest's does: it moved the copied money into its budgets at once (the
// guest read 0, the host 1500) and the row put it back again and again. On: while the host's world stands, a guest's
// NPC nations keep their money where the row put it. Off: they move it (0.7.16.1).
static bool g_npc_money_hold_fix = true;
COOP_FIX("civ.npc_money_hold", "BUG-1461", g_npc_money_hold_fix,
         "civ: while the host's world stands a guest's NPC nations keep the money the host's game gave them");
static uint32_t g_npc_money_held; // tests
using WealthToBudgets_t = void(__fastcall*)(void*);
static WealthToBudgets_t o_WealthToBudgets;
static void __fastcall h_WealthToBudgets(void* civ)
{
    if (g_npc_money_hold_fix && !g_applying && guest_holds() && civ && civ != player_civ() && host_busy::host_stands()) {
        g_npc_money_held++;
        return;
    }
    o_WealthToBudgets(civ);
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
std::map<uintptr_t, uint32_t> g_local_spawns;
bool g_fix_aispawn = true;
COOP_FIX("civ.aispawn", "BUG-0188", g_fix_aispawn, "civ: the NPC AI spawns no vehicles on guests");
static uintptr_t g_ai_spawn_ret[6];
static void* SK_THISCALL h_SpawnVehicle(void* city, int purpose, int loco, uint32_t k0, uint32_t k1, uint32_t k2, int space)
{
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    if (!g_applying && guest_holds()) { // (BUG-1478)
        g_local_spawns[ret - H->image_base() + 0x400000]++;
        if (g_fix_aispawn)
            for (uintptr_t a : g_ai_spawn_ret)
                if (a && a == ret) {
                    g_n.ai_spawns_blocked++;
                    return nullptr;
                }
    }
    return o_SpawnVehicle(city, purpose, loco, k0, k1, k2, space);
}

void caption_arm(); // BUG-0224 (below)
using Capture_t = int(SK_THISCALL*)(void*, uint32_t, int, int, int, int);
static Capture_t o_Capture;
// BUG-0775: a capture is credited to the player whose hit took the city (the capture runs inside that hit:
// building / hall TakeDamage -> AddCaptureDamage -> CheckMilitaryCapture -> Capture), else the player who hit it
// last within the joint-siege window; the nearest vehicle only when neither is known (a conversion, a test).
// `coop.fix civ.capture_credit off`: always the nearest vehicle (0.7.12).
static bool g_fix_capture_credit = true;
COOP_FIX("civ.capture_credit", "BUG-0775", g_fix_capture_credit,
         "civ: a captured city goes to the player whose vehicles took it, not to the one whose vehicle is nearest");
int g_capture_for = -1; // host: the player a capture made for a command is credited to (BUG-0877, BUG-0878)
static uint8_t capture_credit(void* city); // (below the siege)
// BUG-0878: a capture the guest's own game makes while the host is away (local play: the guest's vehicles fight
// in its game) went nowhere: the game showed "won", Capture was refused here and the host never knew. It goes to
// the host as CMD_CAPTURE (held there until the host is back, civ.cmd_hold). `coop.fix civ.away_capture off`: 0.7.12.
bool g_fix_away_capture = true;
COOP_FIX("civ.away_capture", "BUG-0878", g_fix_away_capture,
         "civ: a city a guest captures while the host is away is captured in the host's game when it is back");
static std::unordered_set<void*> g_away_capture_sent; // guest: cities sent this absence
int SK_THISCALL h_Capture(void* city, uint32_t pol, int type, int a3, int a4, int a5)
{
    if (guest_holds() && !g_applying) { // (BUG-1478: a guest in its own world captures there)
        void* pc = player_civ();
        if (g_fix_away_capture && local_play() && pc && pol == pol_of(pc) && !g_away_capture_sent.count(city)) {
            int ci = host_index_of_city(city);
            if (ci >= 0) {
                g_away_capture_sent.insert(city);
                g_n.away_captures_sent++;
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
            game::LiveScope live; // BUG-1053: one walk of the object list (no game code runs in the loop)
            hv_each([&](const HvRef& x) {
                const HV& hv = *x.h;
                if (hv.owner == 0xFE || !net::is_player(*C.sess, hv.owner))
                    return; // BUG-0031: never a player who is not here
                if (g_cmd_listed_fix && !hv_live(x))
                    return; // BUG-1053: removed by the game since the last cycle
                float d = net::dist(veh_pos(x.v), city_pos(city));
                if (d < bd) {
                    bd = d;
                    owner = hv.owner;
                }
            });
            uint8_t credit = capture_credit(city);
            if (credit != 0xFF)
                owner = credit;
            else
                g_n.credit_by[3]++;
            g_city_owner[city] = owner;
            g_city_captured.insert(city); // BUG-0875 (used with civ.capture_keeps)
            g_capture_by[city] = {owner, H->time_seconds()};
            if (g_store_fix && !store::key_of(owner).empty())
                g_city_claim[city] = store::key_of(owner); // E1 (BUG-1153): the captor's
            autosave::milestone("a city captured"); // E2 (BUG-0864)
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
uint8_t g_hit_by = 0xFF;                         // the player whose vehicle is hitting right now
std::unordered_map<void*, std::unordered_map<uint8_t, double>> g_siege; // city -> player -> last hit
PLAYER_STATE("civ.siege", [](uint8_t id) { for (auto& [c, m] : g_siege) m.erase(id); }, // BUG-0422
             [](uint8_t id) -> size_t {
                 size_t n = 0;
                 for (auto& [c, m] : g_siege)
                     n += m.count(id);
                 return n;
             });
std::unordered_set<void*> g_joint_told;
static bool g_siege_players_fix = true;
COOP_FIX("civ.siege_players", "BUG-0420", g_siege_players_fix, "civ: an NPC nation's army attacking the same city is not a second player of a joint siege");

static uint8_t capture_credit(void* city) // 0xFF: not known (the nearest vehicle then)
{
    auto here = [](int p) { return p >= 0 && p < net::kMaxPlayers && net::is_player(*C.sess, (uint8_t)p); };
    if (here(g_capture_for)) { // a guest's purchase or away capture (or the host's own purchase)
        g_n.credit_by[0]++;
        return (uint8_t)g_capture_for;
    }
    if (!g_fix_capture_credit)
        return 0xFF;
    if (here(g_hit_by)) { // the hit that took it
        g_n.credit_by[1]++;
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
        g_n.credit_by[2]++;
    return who;
}

// BUG-0877: the purchase of an NPC city (cCivilization::FinishBuyCity -> cCity buy 0xBE99A0: SpendMoney, then
// Capture type 2). On a guest the money went (its spending reaches the shared money) and the Capture was refused:
// the city changed hands nowhere. A guest's purchase now goes to the host as CMD_BUYCITY and the host's game buys
// it (its money, its Capture), credited to that guest. `coop.fix civ.buy_city off`: 0.7.12.
bool g_fix_buy_city = true;
COOP_FIX("civ.buy_city", "BUG-0877", g_fix_buy_city,
         "civ: a guest's purchase of another nation's city is made in the host's game and the city is that guest's");
CityBuy_t o_CityBuy;
static void SK_THISCALL h_CityBuy(void* city, uint32_t pol, float price)
{
    void* pc = g_fix_buy_city && game::mode_id() == modes::kCiv && !g_applying ? player_civ() : nullptr;
    if (pc && pol == pol_of(pc) && guest_holds()) { // (BUG-1478: a guest in its own world buys there)
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
        g_n.city_buys_sent++;
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

// BUG-0982: the specialty question of a city the player civilization took (docs/re/civ.md#civ-stage-end): at the end
// of the capture cinematic the game asks GameCiv_CaptureNeedsSpecialty (0xCF7A30) for the player civilization's last
// captured city (+0x44C) and, when its capture type (+0x29C) is not its specialty (+0x540), shows the box
// (cCity_ShowSpecialtyBox: the question and Pause 0x4BF38A7). A guest's purchase (civ.buy_city) or away capture
// runs in the host's game, so the box and its pause came up on the host and the buyer was never asked. Now the host
// sends the question to the city's owner (CMD_SPECIALTY: city, capture type) and goes on unpaused; the guest's game
// shows the game's own box for its copy of the city; its answer (the listener's message 0x40FAFDF: 0x5107B1A takes
// the capture type, 0x5107B19 keeps the specialty) comes back and the host's game sets the specialty.
// `coop.fix civ.specialty_buyer off`: the host's game asks its own player (0.7.13).
bool g_fix_specialty_buyer = true;
COOP_FIX("civ.specialty_buyer", "BUG-0982", g_fix_specialty_buyer,
         "civ: the specialty question of a city a guest bought or captured goes to that guest; the host's game does not pause");
constexpr uintptr_t kCivLastCaptured = 0x44C, kCityCaptureType = 0x29C;
constexpr uint32_t kSpecialtyAnswerMsg = 0x40FAFDF, kSpecialtyTake = 0x5107B1A;
using NeedsSpecialty_t = bool(__cdecl*)();
static NeedsSpecialty_t o_NeedsSpecialty;
using CityListener_t = uint32_t(SK_THISCALL*)(void*, uint32_t, void*);
static CityListener_t o_CityListener;
static uintptr_t a_ShowSpecialtyBox, a_SetSpecialty, a_TimeMgr, a_TimeResume;
std::deque<SpecialtyAsk> g_spec_queue; // guest: questions of the host not shown yet
int g_spec_open = -1;                  // guest: the city index whose box is up (-1: none)
static int g_spec_open_type = 0;
static void* g_spec_answer_data;              // guest: the answer message taken (every city's listener gets it)
// BUG-1479 (audit CIV-10): the first city listener closes the question (g_spec_open = -1), so the guard against the
// same answer at the next listeners sat inside `g_spec_open >= 0` and never ran: every other city's listener passed the
// answer to the game's, which acts for the player civ's last captured city (+0x44C; on a guest the loaded save's):
// SetSpecialty with that city's stale capture type and a second Resume of the box's pause. Now the answer taken is
// dropped at every later listener of the same dispatch (the data is cleared at the next civ::tick, so a later box of
// the game's own with a reused data pointer is the game's). Off (`coop.fix civ.specialty_once off`): 0.7.16.1.
static bool g_fix_specialty_once = true;
COOP_FIX("civ.specialty_once", "BUG-1479", g_fix_specialty_once,
         "civ: a guest's specialty answer is taken once; the other cities' listeners do not apply it to another city");
void* g_spec_took_at;                         // guest, tests: the city whose listener took the last answer
// BUG-1175: the box's own pause (Pause 0x4BF38A7 in cCity_ShowSpecialtyBox) stopped the guest's game time while the
// question was up: the cities the host's game founded meanwhile (the next site after a purchase or a capture) waited
// for the game time (their appearance is a timed callback, docs/re/civ.md 9.3), and so did every vehicle of their
// nations (no civilization here, no mirror): "v: 2 missing on the guest" until the answer. The host's world does not
// stand for one player's question (the host's game is not paused either, BUG-0982): the guest releases the pause the
// box took at once and its answer does not resume it again. Off (`coop.fix civ.specialty_runs off`): the guest's game
// stands until the answer (0.7.14).
static bool g_fix_specialty_runs = true;
COOP_FIX("civ.specialty_runs", "BUG-1175", g_fix_specialty_runs,
         "civ: the guest's world goes on while its specialty question is up (the host's new cities and their vehicles appear)");
constexpr uint32_t kSpecialtyPause = 0x4BF38A7;
static int g_spec_released;                   // guest: the box's pauses released at its showing (not resumed again)
static int pause_count(uint32_t reason)
{
    for (auto& [r, c] : world::pause_reasons())
        if (r == reason)
            return c;
    return 0;
}
LIFECYCLE_STATE("civ.specialty_questions", lifecycle::kSession | lifecycle::kWorld, &g_fix_specialty_buyer,
                [] {
                    g_spec_queue.clear();
                    g_spec_open = -1;
                    g_spec_answer_data = nullptr;
                    g_spec_took_at = nullptr;
                    g_spec_released = 0;
                },
                []() -> size_t { return g_spec_queue.size() + (g_spec_open >= 0); });

static bool __cdecl h_NeedsSpecialty()
{
    if (g_fix_specialty_buyer && is_host() && game::mode_id() == modes::kCiv && C.sess) {
        void* pc = player_civ();
        void* city = pc ? F<void*>(pc, kCivLastCaptured) : nullptr;
        if (city && game::listed(city)) {
            uint8_t owner = owner_of_city(city);
            if (owner == 0xFF) {
                auto it = g_capture_by.find(city);
                owner = it == g_capture_by.end() ? 0xFF : it->second.owner;
            }
            int type = F<int>(city, kCityCaptureType), spec = F<int>(city, kCitySpecialty);
            int ci = host_index_of_city(city);
            if (owner != 0xFF && owner != C.sess->my_id() && net::is_player(*C.sess, owner) && type >= 0 && type <= 2 &&
                type != spec && ci >= 0) {
                net::Writer w;
                w.u8(kCmdSpecialty);
                w.u8((uint8_t)ci);
                w.u32((uint32_t)type);
                C.sess->send_reliable_to(owner, net::MSG_CIV_CMD, w);
                g_n.spec_asked++;
                H->log(M, SK_INFO, "civ: the specialty question of city %d (specialty %d, taken as %d) goes to its owner, "
                       "player %u (BUG-0982)", ci, spec, type, (unsigned)owner);
                return false; // no box, no pause here: the game goes on to its planet check
            }
        }
    }
    return o_NeedsSpecialty();
}

static void spec_show_next()
{
    if (g_spec_open >= 0 || g_spec_queue.empty())
        return;
    H->run_outside_frame(
        [](void*) {
            while (g_spec_open < 0 && !g_spec_queue.empty()) {
                SpecialtyAsk a = g_spec_queue.front();
                g_spec_queue.pop_front();
                void* city = a.ci < g_host_cities.size() ? g_host_cities[a.ci] : nullptr;
                if (!is_guest() || game::mode_id() != modes::kCiv || !city || !game::listed(city) || !a_ShowSpecialtyBox)
                    continue;
                F<int>(city, kCityCaptureType) = a.type; // (the box's text and the answer read it)
                g_spec_open = a.ci;
                g_spec_open_type = a.type;
                g_spec_answer_data = nullptr;
                int before = pause_count(kSpecialtyPause);
                thiscall<void>(a_ShowSpecialtyBox, city);
                g_n.spec_shown++;
                H->log(M, SK_INFO, "civ: the host asks this game for the specialty of city %u (BUG-0982)", (unsigned)a.ci);
                g_spec_released = 0;
                if (g_fix_specialty_runs) { // BUG-1175: the box stays up, the game time goes on
                    int took = pause_count(kSpecialtyPause) - before;
                    for (int k = 0; k < took; k++)
                        world::time_pause(kSpecialtyPause, false);
                    g_spec_released = took > 0 ? took : 0;
                    g_n.spec_unpaused += g_spec_released;
                    H->log(M, SK_INFO, "civ: the specialty box of city %u took %d pause(s): released, the world goes on "
                           "(BUG-1175)", (unsigned)a.ci, took);
                }
            }
        },
        nullptr);
}

static uint32_t SK_THISCALL h_CityListener(void* self, uint32_t msg, void* data)
{
    civlogic::SpecAnswer act = msg == kSpecialtyAnswerMsg && is_guest() && data
                                   ? civlogic::spec_answer(g_fix_specialty_once, g_spec_open >= 0,
                                                           data == g_spec_answer_data)
                                   : civlogic::SpecAnswer::Pass;
    if (act == civlogic::SpecAnswer::Drop)
        return 0; // the same answer reaching the next city's listener: taken already (BUG-1479: also once closed)
    if (act == civlogic::SpecAnswer::Take && !IsBadReadPtr(data, 12)) {
        g_spec_answer_data = data;
        g_spec_took_at = (uint8_t*)self - 0x20C; // (the listener is the city's +0x20C)
        uint32_t button = *(uint32_t*)((uint8_t*)data + 8);
        bool take = button == kSpecialtyTake;
        uint8_t ci = (uint8_t)g_spec_open;
        int type = g_spec_open_type;
        g_spec_open = -1;
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (take && city && game::listed(city) && a_SetSpecialty)
            thiscall<void>(a_SetSpecialty, city, type, 1); // this game's copy at once (the host's follows)
        if (g_spec_released > 0) // BUG-1175: released at its showing already (a second Resume would go under 0)
            g_spec_released = 0;
        else if (a_TimeMgr && a_TimeResume)
            if (void* tm = skg::cdecl_call<void*>(a_TimeMgr))
                thiscall<int>(a_TimeResume, tm, kSpecialtyPause); // the box's own pause
        net::Writer w;
        w.u8(kCmdSpecialty);
        w.u8(ci);
        w.u8(take ? 1 : 0);
        w.u32((uint32_t)type);
        send_cmd(w);
        g_n.spec_answered++;
        H->log(M, SK_INFO, "civ: the specialty of city %u answered (%s): sent to the host (BUG-0982)", (unsigned)ci,
               take ? "take the new one" : "keep");
        spec_show_next();
        return 0;
    }
    return o_CityListener(self, msg, data);
}

static void guest_specialty_ask(net::Reader& r)
{
    uint8_t ci = r.u8();
    int type = (int)r.u32();
    if (!r.ok() || type < 0 || type > 2)
        return;
    g_spec_queue.push_back({ci, type});
    spec_show_next();
}

void host_specialty_answer(uint8_t from, net::Reader& r)
{
    uint8_t ci = r.u8();
    bool take = r.u8() != 0;
    int type = (int)r.u32();
    void* city = host_city(ci);
    void* pc = player_civ();
    if (!r.ok() || !city || !pc || pol_of(city) != pol_of(pc) || owner_of_city(city) != from || type < 0 || type > 2) {
        g_n.spec_rejected++;
        H->log(M, SK_INFO, "civ: a specialty answer of player %u for city %u not taken (not that player's city)",
               (unsigned)from, (unsigned)ci);
        return;
    }
    if (take && a_SetSpecialty)
        thiscall<void>(a_SetSpecialty, city, type, 1);
    g_n.spec_applied++;
    H->log(M, SK_INFO, "civ: player %u answered the specialty of city %u: %s (BUG-0982)", (unsigned)from, (unsigned)ci,
           take ? "the new one" : "kept");
}

uint8_t player_of_combatant(void* att)
{
    if (!att || IsBadReadPtr(att, 4))
        return 0xFF;
    void* v = vcall<void*>(att, 0x5C / 4, 0x137E8E0u); // the vehicle behind a combatant
    if (!v)
        return 0xFF;
    HvRef it = hv_find(v);
    return it ? it.h->owner : 0xFF;
}

// F3 (BUG-0768): civ.roles off: nobody has a role, no role bonus
uint8_t role(uint8_t p) { return C.role_of && p != 0xFF && settings::on(settings::kCivRoles) ? C.role_of(p) : 0; }

// BUG-0899: Civ friendly fire (all.friendly_fire)
bool g_fix_civ_ff = true;
COOP_FIX("civ.friendly_fire", "BUG-0899", g_fix_civ_ff,
         "civ: with all.friendly_fire on, players' military vehicles can attack and damage other players' vehicles and cities");
bool ff_on() { return g_fix_civ_ff && settings::on(settings::kFriendlyFire); }

static void* city_of_building(void* b)
{
    if (!b)
        return nullptr;
    for (void* city : all_cities()) {
        if (F<void*>(city, 0x320) == b)
            return city;
        for (void* bl : vec_of((uint8_t*)city + kCityBuildings))
            if (bl == b)
                return city;
    }
    return nullptr;
}

// building / city hall TakeDamage (this = building+0x120): a military governor's vehicles hit harder;
// the player is remembered for the capture damage the original adds
using BTakeDamage_t = int(SK_THISCALL*)(void*, float, uint32_t, int, const void*, void*);
static BTakeDamage_t o_BTakeDamage, o_HallTakeDamage;
static int b_damage(BTakeDamage_t orig, void* self, float dmg, uint32_t pol, int type, const void* dir, void* att)
{
    if (cosmetic_hit(att)) // BUG-0477: a relayed shot of the host's
        return 0;
    uint8_t p = is_host() && active() ? player_of_combatant(att) : 0xFF;
    bool is_ff_hit = false;
    if (is_host() && active() && p < net::kMaxPlayers && self) {
        if (void* city = city_of_building((uint8_t*)self - 0x120)) {
            uint8_t co = owner_of_city(city);
            auto gate = civlogic::ff_damage_gate(ff_on(), p, co, (uint8_t)net::kMaxPlayers);
            if (gate == civlogic::FfGate::Block) {
                g_n.ff_blocked++;
                return 0;
            }
            if (gate == civlogic::FfGate::Allow) {
                pol = 0xFFFFFFFFu;
                g_n.ff_hits++;
                is_ff_hit = true;
            }
        }
    }
    if (p != 0xFF && role(p) == 1) {
        dmg *= 1.25f;
        g_n.military_hits++;
    }
    // WP-D1 (BUG-1473): all.party_difficulty: a player's vehicle hits a building (the players share one nation: an NPC's).
    // An NPC's hit on the players' buildings keeps the game's damage (the building's owner is not read here)
    if (p < net::kMaxPlayers && !is_ff_hit)
        dmg = party::player_hits_npc(dmg);
    uint8_t prev = g_hit_by;
    size_t mark = game::swap_open(game::swap_undo_u8, &g_hit_by, prev, "capture attribution"); // BUG-0110
    g_hit_by = is_ff_hit ? 0xFF : p;
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
void ff_deal_hall_hit(void* hall, void* attacker_veh, float dmg)
{
    if (!hall || !attacker_veh || !o_HallTakeDamage)
        return;
    static const float zero[3] = {0, 0, 0};
    h_HallTakeDamage((uint8_t*)hall + 0x120, dmg, 0xFFFFFFFFu, 0, zero, (uint8_t*)attacker_veh + 0x508);
}

// capture damage: the city is taken faster when vehicles of two or more players attack it
using AddCapture_t = void(SK_CDECL*)(void*, void*, void*, uint32_t, float, uint32_t, bool);
static AddCapture_t o_AddCapture;
void SK_CDECL h_AddCapture(void* ctx, void* city, void* meter, uint32_t owner, float dmg, uint32_t att, bool f)
{
    // BUG-0420: the owner of an NPC nation's vehicle is 0xFE: it counted as a second player (one guest and an NPC
    // army gave the joint-siege bonus). Only a player's vehicles count now; `coop.fix civ.siege_players off`: 0.7.10.
    bool player = g_hit_by != 0xFF && (!g_siege_players_fix || g_hit_by < net::kMaxPlayers);
    if (!player && g_hit_by != 0xFF)
        g_n.siege_npc++;
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
            g_n.joint_hits++;
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
// BUG-1284 (found by the Lineages fork (PR #196), its BUG-9078): the bonus looked for a governor's vehicle of purpose 2
// (economic) of any nation: the game's purposes are 0 military, 1 religious (cultural), 2 economic (docs/re/civ.md 1),
// so a trader gave the religious bonus and the preachers did not. Now a religious vehicle (1) of the converting nation.
// `coop.fix civ.religious_attribution off`: purpose 2, any nation (0.7.15).
static bool g_religious_attr_fix = true;
COOP_FIX("civ.religious_attribution", "BUG-1284", g_religious_attr_fix,
         "civ: the religious role's conversion bonus comes from its own preachers (religious vehicles), not from traders");
static void SK_THISCALL h_Convert(void* city, uint32_t pol, int count)
{
    if (!g_applying && active() && guest_converts_blocked())
        return; // BUG-1285: the host's game converts; its follower maps come (civ_influence.cpp)
    if (is_host() && active() && city && count > 0) {
        net::Vec3 cp = city_pos(city);
        uint8_t preacher = 0xFF;
        game::LiveScope live; // BUG-1053: one walk of the object list (no game code runs in the loop)
        hv_each([&](const HvRef& x) {
            if (preacher == 0xFF && role(x.h->owner) == 3 && (!g_cmd_listed_fix || hv_live(x)) &&
                (g_religious_attr_fix ? F<int>(x.v, kVPurpose) == 1 && pol_of(x.v) == pol : F<int>(x.v, kVPurpose) == 2) &&
                net::dist(veh_pos(x.v), cp) < 200.f)
                preacher = x.h->owner; // (the first one)
        });
        if (preacher != 0xFF) {
            float k = 1.5f;
            double now = H->time_seconds();
            auto it = g_siege.find(city);
            if (it != g_siege.end())
                for (auto& [p, t] : it->second)
                    if (p != preacher && now - t < kJointWindow)
                        k = 2.f; // combined arms
            count = (int)std::ceil(count * k);
            g_n.religious_boosts++;
        }
    }
    o_Convert(city, pol, count);
}

// a player's city is attacked: everybody sees it (beacon + notice), and the other players'
// vehicles near it rush there faster for a while
std::unordered_map<void*, Boost> g_boost;
std::unordered_map<void*, double> g_alarm_at;
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
void SK_THISCALL h_OnAttacked(void* city, int kind, uint32_t att)
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
    g_n.alarms++;
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
    hv_each([&](const HvRef& x) {
        void* v = x.v;
        if (!exists(v)) // the table is rebuilt every 0.1 s: a vehicle freed meanwhile
            return;
        if (x.h->owner == owner || net::dist(veh_pos(v), cp) > 600.f)
            return;
        if (g_fix_boost_players && x.h->owner >= 0xFE) // BUG-0876: an NPC nation's vehicle (the attackers)
            return;
        auto [bi, fresh] = g_boost.try_emplace(v);
        Boost& b = bi->second;
        if (g_fix_boost_base ? fresh : b.until < now) { // BUG-0771: the own speed once per entry
            b.orig = F<float>(v, 0x218);
            b.pol = pol_of(v);
        }
        b.until = now + 20.0;
    });
}

void boost_tick(double now)
{
    for (auto it = g_boost.begin(); it != g_boost.end();) {
        void* v = it->first;
        // BUG-0040: object_alive reads the vehicle's own memory and cannot tell a freed one: only a
        // vehicle in the game's own list is written to
        bool alive = hv_find(v) && exists(v);
        if (!alive) {
            if (g_fix_boost_restore && boost_ours(v, it->second)) { // BUG-0773: it lives, out of the table only
                F<float>(v, 0x218) = it->second.orig;
                g_n.boost_restored++;
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
CALL_GUARD("civ.sw_bypass", g_sw_bypass); // BUG-1047
// BUG-0117: the vote's weapon and target are its own (captured), not statics a second request
// overwrote
int g_sw_last_req = -1, g_sw_fired = -1; // host: last guest request, weapon type the last guest vote fired
bool SK_THISCALL h_FireSW(void* civ, int type, const float* target)
{
    // F3 (BUG-0768): civ.superweapon_vote off: the host's (and a guest's own) superweapon fires at once
    if (g_sw_bypass || !active() || C.sess->players().size() < 2)
        return o_FireSW(civ, type, target);
    if (is_host() && !settings::on(settings::kCivSwVote)) {
        g_n.sw_unvoted++;
        return o_FireSW(civ, type, target);
    }
    float pos[3] = {target ? target[0] : 0, target ? target[1] : 0, target ? target[2] : 0};
    if (is_guest() && !guest_holds()) // BUG-1478: a guest in another Civ world of its own fires its own
        return o_FireSW(civ, type, target);
    if (is_guest()) { // the host asks everybody
        net::Writer w;
        w.u8(5 /* CMD_SUPER */);
        w.u32((uint32_t)type);
        w.f32(pos[0]); w.f32(pos[1]); w.f32(pos[2]);
        send_cmd(w);
        return false;
    }
    // BUG-0117: the vote fires what was asked in it; a request during another vote changes nothing
    std::array<float, 3> p{pos[0], pos[1], pos[2]};
    bool has_target = target != nullptr;
    if (C.vote && C.vote(C.sess->name_of(C.sess->my_id()) + " wants to fire a superweapon", "the superweapon fires",
                         [civ, type, p, has_target] {
                             {
                                 callguard::Hold sw_bypass(g_sw_bypass);
                                 o_FireSW(civ, type, has_target ? p.data() : nullptr);
                             }
                         }))
        g_n.votes++;
    else {
        g_n.sw_refused++;
        if (C.feed)
            C.feed("superweapon: another vote is running, ask again when it is over");
    }
    return false;
}

// BUG-1468: a guest's superweapon is paid from its own money (civ.own_money; FireSuperweapon takes its cost from
// mWealth +0x98 itself, docs/re/civ.md): the game's own check refuses it when that money is short
static uint32_t g_sw_paid_own, g_sw_short_own; // tests
static void sw_fire_for(uint8_t from, void* pc, int type, const float* pos)
{
    civ_wallet::PayAs pay(from);
    bool fired = o_FireSW(pc, type, pos);
    if (!pay.paying())
        return;
    (fired ? g_sw_paid_own : g_sw_short_own)++;
    if (!fired && C.sess) {
        net::Writer nw;
        nw.str8("superweapon: not fired (not enough money of your own, or it cannot fire now)");
        C.sess->send_reliable_to(from, net::MSG_NOTICE, nw);
    }
}
void host_superweapon_request(uint8_t from, net::Reader& r)
{
    int type = (int)r.u32();
    float pos[3] = {r.f32(), r.f32(), r.f32()};
    void* pc = player_civ();
    // the game's superweapon types are 0..11 (GetSuperweaponCost, docs/re/civ.md)
    if (!r.ok() || !pc || !C.vote || type < 0 || type > 11 || !net::finite3(pos))
        return;
    g_sw_last_req = type;
    if (!settings::on(settings::kCivSwVote)) { // F3 (BUG-0768): no vote, the guest's superweapon fires now
        {
            callguard::Hold sw_bypass(g_sw_bypass);
            g_sw_fired = type;
            sw_fire_for(from, pc, type, pos);
        }
        g_n.sw_unvoted++;
        return;
    }
    std::array<float, 3> p{pos[0], pos[1], pos[2]};
    if (C.vote(C.sess->name_of(from) + " wants to fire a superweapon", "the superweapon fires", [pc, type, p, from] {
            {
                callguard::Hold sw_bypass(g_sw_bypass);
                g_sw_fired = type;
                sw_fire_for(from, pc, type, p.data());
            }
        }))
        g_n.votes++;
    else { // tell the sender its request was not taken
        g_n.sw_refused++;
        net::Writer nw;
        nw.str8("superweapon: another vote is running, ask again when it is over");
        C.sess->send_reliable_to(from, net::MSG_NOTICE, nw);
    }
}

// ---- BUG-0484: city turrets ----
// A city's turrets (cCity +0x354, noun 0x436F342) are made by City_AddTurret 0xBE39A0 (the next free turret slot):
// the city growth 0xBE5890 buys them from the city's budget (two calls), so a guest's game, whose growth runs
// on its own copy of an NPC city, put turrets there that the host's never had (and the other way round), and
// the snapshot carried none. Now the guest's growth makes none (like its buildings, CivGrowth_*_Ret) and the
// host sends the places of one city's turrets per snapshot cycle (chunk 0xFB, when they changed and every 3 s);
// a guest adds the ones it lacks with City_AddTurret and removes the ones the host does not have (two looks in
// a row, as reconcile_buildings).
constexpr uint8_t kTurretChunk = 0xFB;
constexpr uintptr_t kCityTurrets = 0x354;
uintptr_t a_AddTurret, a_RemoveTurret, a_TurretRet1, a_TurretRet2;
AddTurret_t o_AddTurret;
static std::unordered_map<void*, std::pair<uint32_t, double>> g_turret_sent; // host: city -> hash, when
static std::unordered_map<void*, int> g_turret_seen;                          // guest: city -> mismatches in a row
static int __fastcall h_AddTurret(void* city)
{
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    if (!g_applying && is_guest() && game::mode_id() == modes::kCiv && !local_play() &&
        (ret == a_TurretRet1 || ret == a_TurretRet2)) {
        g_n.turrets_blocked++;
        return 0; // both growth calls ignore the result (the host's turrets come with the snapshot)
    }
    return o_AddTurret(city);
}
std::vector<std::pair<void*, net::Vec3>> turrets_of(void* city)
{
    std::vector<std::pair<void*, net::Vec3>> out;
    for (void* t : vec_of((uint8_t*)city + kCityTurrets)) {
        net::Vec3 p;
        if (t && game::object_position(t, p) && out.size() < 40)
            out.push_back({t, p});
    }
    return out;
}
void host_send_turrets(size_t ci, void* city, double now)
{
    if (!a_AddTurret)
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
    g_n.turret_tx++;
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
    g_n.turret_rx++;
    void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
    if (!a_AddTurret || !city || local_play() || planner_holds(city) || game::mode_id() != modes::kCiv)
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
    callguard::Hold applying(g_applying);
    for (void* t : extra) { // the host has no turret there
        thiscall<void>(a_RemoveTurret, city, t, 0);
        g_n.turrets_removed++;
    }
    for (size_t i = 0; i < missing; i++) // the next free slot: the order the host's game fills them in
        if (o_AddTurret(city))
            g_n.turrets_added++;
}

bool save_filter(void* o, uint32_t noun);
void save_start();
void save_end();
// BUG-0549: a guest's mirror vehicle drawn at scale 10 where the host's vehicle has 0.81 (play_civ detector
// `Civilization:scale:vehicle`). The guest's game itself gives the mirror the 10: the model-ready handler of the
// vehicle (0xCA76B0, message 0x2B978C46) runs 0xCA40B0, which sets loco SetScale(10.0) (return address 0xCA41DA,
// loco vtable 0x1474BE8) when it finds its ground object; the host's vehicle never took that path (the
// BUG-0485 diagnostic logged the caller on guests only). The vehicle's scale is a replicated field now
// (fields.h, the class of BUG-0461 / BUG-0475): the host sends its vehicles' scale, a guest's mirror that
// differs gets it.
static const fields::Field kVehFields[] = {fields::kScale};
static void veh_each(void (*cb)(uint16_t, void*, void*), void* u)
{
    auto alive = all_vehicles();
    for (void* v : alive) {
        HvRef it = hv_find(v);
        if (it && it.id)
            cb(it.id, v, u);
    }
}
static uint64_t g_veh_live_frame = ~0ull;
static std::unordered_set<void*> g_veh_live;
void* veh_mirror(uint16_t id)
{
    GvRef it = gv_find(id);
    if (!it || !it.v || (g_veh_lifetime_fix && !gv_ok(it))) // (BUG-0951)
        return nullptr;
    if (H->frame_index() != g_veh_live_frame) {
        g_veh_live_frame = H->frame_index();
        g_veh_live.clear();
        for (void* v : all_vehicles())
            g_veh_live.insert(v);
    }
    return g_veh_live.count(it.v) ? it.v : nullptr;
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
    a_PlannerMoneyAddRet = H->sym("SporeCoop::CommunityEditor_Money_AddRet"); // optional (BUG-1532, GOG 2024)
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
    { // BUG-0982 (optional)
        uintptr_t ns = H->sym("SporeCoop::GameCiv_CaptureNeedsSpecialty"), cl = H->sym("SporeCoop::cCity_MessageListener");
        a_ShowSpecialtyBox = H->sym("SporeCoop::cCity_ShowSpecialtyBox");
        a_SetSpecialty = H->sym("SporeCoop::cCity_SetSpecialty");
        a_TimeMgr = H->sym("Simulator::cGameTimeManager::Get");
        a_TimeResume = H->sym("Simulator::cGameTimeManager::Resume");
        if (ns && cl && a_ShowSpecialtyBox && a_SetSpecialty) {
            H->hook_create(M, (void*)ns, (void*)h_NeedsSpecialty, (void**)&o_NeedsSpecialty);
            H->hook_create(M, (void*)cl, (void*)h_CityListener, (void**)&o_CityListener);
        }
    }
    H->hook_create(M, (void*)a_CreateBuilding, (void*)h_CreateBuilding, (void**)&o_CreateBuilding);
    H->hook_create(M, (void*)a_SpawnNextCity, (void*)h_SpawnNextCity, (void**)&o_SpawnNextCity);
    H->hook_create(M, (void*)a_AddMoney, (void*)h_AddMoney, (void**)&o_AddMoney);
    g_passive_income_ret[0] = H->sym("SporeCoop::CivIncome_AddMoney_Ret"); // BUG-1236 (docs/re/civ.md#civ-income-addmoney)
    g_passive_income_ret[1] = H->sym("SporeCoop::CivTrade_CityAddMoney_Ret");
    g_passive_income_ret[2] = H->sym("SporeCoop::CivTrade_TraderAddMoney_Ret");
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
    if (uintptr_t a = H->sym("SporeCoop::CivAI_WealthToBudgets")) // BUG-1461 (optional)
        H->hook_create(M, (void*)a, (void*)h_WealthToBudgets, (void**)&o_WealthToBudgets);
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
    own_designs_init(); // BUG-1274
    influence_init(); // BUG-1285, BUG-1286, BUG-1287 (optional)
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
    fields::add_kind({fields::K_CIV_VEHICLE, "civ_vehicle", nullptr, kVehFields, 1, veh_each, veh_mirror,
                      [] { return g_ok && game::mode_id() == modes::kCiv; }}); // BUG-0549
    species_init(); // WP-C1 (BUG-1273, optional)
    return true;
}

void tick(double now)
{
    veh_hunt_tick();
    if (g_fix_specialty_once) // BUG-1479: the answer's dispatch is over (every city's listener had it)
        g_spec_answer_data = nullptr;
    if (g_ok && C.sess && C.sess->connected())
        space_tick(now); // BUG-0766 / BUG-0884
    if (!active())
        return;
    prune_gone();
    species_tick(now); // WP-C1 (BUG-1273)
    g_cmd_world = modes::current_world();
    // BUG-0693: the commands that came while this game was away run once the stage settled again (a load, an editor)
    g_held.tick(held_row(), true, modes::current_world(), now, [](const net::HeldCommands::Cmd& c) {
        net::Reader r(c.bytes.data(), c.bytes.size());
        H->log(M, SK_INFO, "civ: a command of player %u held while away runs now", c.from);
        host_command(c.from, r);
    });
    if (is_host())
        host_tick(now);
    else if (is_guest()) {
        planner_out_tick(now); // BUG-1417
        guest_tick(now);
    }
    own_designs_tick(now); // BUG-1274
}

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
    g_n.space_builds_sent++;
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

void* local_city_near(const net::Vec3& p);

static std::vector<SpaceBuild> g_space_held;
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
static double g_space_place_since;

static uint32_t space_place_now() { return C.space_place ? C.space_place() : 0; }

// the building made in this game's city near cp (not paid: the buyer paid; the shared money follows)
void* space_build_here(const SpaceBuild& sb, bool quiet)
{
    void* city = local_city_near(sb.cp);
    g_build_from = sb.from; // the buyer already has it
    void* b{};
    {
        callguard::Hold applying(g_applying);      // not sent again by this game's h_AddBuilding
        b = city ? build_in_city(city, sb.noun, sb.p, false) : nullptr;
        if (b)
            space_write_city(city); // BUG-0901
    }
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
    g_n.space_relayed++;
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
        g_n.space_builds_away++;
        H->log(M, SK_INFO, "civ: space planner: %s's building %08X on %08X not built: this game is at %08X (BUG-0751)",
               C.sess->name_of(from).c_str(), sb.noun, sb.place, mine);
        if (g_fix_space_build_hold) // BUG-0766
            space_hold(sb, mine);
        return;
    }
    void* b = space_build_here(sb);
    if (b)
        g_n.space_builds_in++;
    else
        g_n.space_builds_failed++;
    if (b)
        space_note_host_build(local_city_near(sb.cp), b, from); // BUG-0884: the other guests
}

// guest, Space: the host's colony building (its own or another guest's), BUG-0884
static void guest_space_build(net::Reader& r)
{
    SpaceBuild sb;
    if (!read_space_build(r, sb, 0))
        return;
    g_n.space_relay_in++;
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
    if (place != g_n.space_place_seen) {
        g_n.space_place_seen = place;
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
                    g_n.space_planner_dropped++;
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
            g_n.space_held_built++;
        else if (!strcmp(g_build_fail, "no free slot") && (!sb.tried || now - sb.tried < 60.0)) {
            // the planet's cities are made again from its record when it becomes the active planet: a plot freed in
            // this session may come back later, or the city is still settling: tried again every 2 s for 60 s
            sb.tried = sb.tried ? sb.tried : now;
            sb.next_try = now + 2.0;
            g_space_held.insert(g_space_held.begin() + i, sb);
            i++;
            continue;
        } else
            g_n.space_builds_failed++;
        if (b && is_host())
            space_note_host_build(local_city_near(sb.cp), b, sb.from);
    }
}

// BUG-1480 (audit CIV-11): a guest's CMD_SPACE_BUILD went the Space way only while the host's game was in Space; in an
// editor (the captain's outfitter) or a load it fell into the Civ command hold, which runs only in Civ: the building the
// guest paid for never came to the host's colony, and host_space_build's own hold (BUG-0766) was never reached. Now it
// is routed by the command in any mode outside Civ (host_space_build keeps it until this game is on that planet); its
// first byte is also the superweapon request of a Civ guest, so outside Space it is told by its size (civ_logic.h).
// Off (`coop.fix civ.space_build_route off`): 0.7.16.1.
static bool g_fix_space_build_route = true;
COOP_FIX("civ.space_build_route", "BUG-1480", g_fix_space_build_route,
         "civ: a guest's colony building that reaches the host in an editor or a load is kept until the host is on that planet");

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (!active() && type == net::MSG_CIV_CMD && g_ok && is_host() &&
        civlogic::space_build_routed(g_fix_space_build_route, game::mode_id() == modes::kSpace, r.left()) && r.left()) {
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
            g_held.hold(held_row(), from, r.cur(), r.left(), g_cmd_world, H->time_seconds());
            H->log(M, SK_INFO, "civ: a command of player %u held: this game is not in Civ (mode %08X)", from, game::mode_id());
        }
        if (type == net::MSG_CIV_BUILD && g_ok && is_guest()) {
            net::Reader peek = r;
            uint8_t ci = peek.u8();
            uint32_t noun = peek.u32();
            if (peek.ok() && ci == kNextCitySite && (noun == 0 || noun == kFoundInstant)) {
                g_n.founds_outside++;
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
            guest_shots(peek, chunk == kShotChunkFired);
        } else if (peek.ok() && chunk == kHpChunk && chunks == 0) // BUG-0529: buildings' hit points
            guest_building_hp(peek);
        else if (peek.ok() && chunk == kTurretChunk && chunks == 0) // BUG-0484: turrets
            guest_turrets(peek);
        else if (peek.ok() && chunk == kTribeChunk && chunks == 0) // BUG-0539: old tribe villages
            guest_tribes(peek);
        else if (peek.ok() && chunk == kInfluenceChunk && chunks == 0) // BUG-1285, BUG-1287: religion, economy
            guest_influence(peek);
        else if (!g_veh_hold) // (tests, BUG-0951: `coop.civ vehhold`)
            guest_state(r);
    }
    else if (type == net::MSG_CIV_CMD && is_host())
        host_command(from, r);
    else if (type == net::MSG_CIV_CMD && is_guest() && from == 0 && r.left()) { // BUG-0982: the host's question
        if (r.u8() == kCmdSpecialty)
            guest_specialty_ask(r);
    }
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
                g_n.boost_restored++;
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
    took_clear(); // BUG-1416
    planner_out_clear(); // BUG-1417
    g_held.clear(); // BUG-0693
    g_space_held.clear(); // BUG-0766 / BUG-0884
    g_space_out.clear();
    own_designs_session_end(); // BUG-1274 (before BUG-0670's: the culture keys first)
    host_buildings_session_end(); // BUG-0670
    caption_disarm(); // BUG-0224
    clear_coop_tables(true);
    g_shots.clear();
    g_shots_in.clear();
    g_turret_shooters.clear();
    g_hp_sent.clear();
    g_turret_sent.clear();
    g_turret_seen.clear();
    g_tribe_extra_seen.clear();
    hv_clear();
    gv_clear();
    g_city_owner.clear();
    g_host_cities.clear();
    g_split_for.clear();
    g_city_short_since = -1;
}
LIFECYCLE_HOOK("civ.session", lifecycle::kSessionEnd, -44, [](const lifecycle::Ctx&) { on_session_end(); }, true); // C2 (BUG-1141): end_session

// a load (or a stage change): every vehicle and city of the old world is freed. Before BUG-0059 civ
// had no such reset: the host kept the old vehicle table (a new vehicle at a reused address kept an
// old id and a player owner, even an NPC nation's one) and the guest the old host-city table.
void on_world_change()
{
    // the vehicle / building editors have no co-op adapter either (coop.cpp sees "no local" as a
    // world change): the world stays, and the owners must not be lost with it
    uint32_t m = game::mode_id();
    if (m == modes::kEdit || m == modes::kEditor)
        return;
    g_n.world_resets++;
    took_clear(); // BUG-1416: the taken mirrors' civilizations are freed with the world
    planner_out_clear(); // BUG-1417 (the cities are freed)
    g_bld_rekeyed.clear(); // BUG-0670 (the buildings are freed)
    caption_disarm();
    // BUG-0773: after a real load the vehicles are gone and nothing is written (boost_ours: in the game's list now,
    // the same nation, still the speed the mod wrote); a world change seen while they live gives their speed back
    clear_coop_tables(g_fix_boost_restore);
    // BUG-0223: the tables are forgotten here, not through on_session_end (its restoring clear
    // would write +0x218 into the old world's vehicles if the order ever changed)
    hv_clear();
    gv_clear();
    g_city_owner.clear();
    g_host_cities.clear();
    g_split_for.clear();
    g_kept_tried = false; // BUG-0950: the next split gives the new world's kept owners back
    g_claims_serial = 0; // E1 (BUG-1153): the claims of the world loaded now come from the store again
    g_city_claim.clear();
    g_veh_claim.clear();
    g_cities_matched = g_vehs_matched = false;
    g_kept_key.clear();
    g_kept_at = H->time_seconds() + 3.0; // nothing is written over the kept owners before that
    g_city_short_since = -1;
    g_cities_waiting_told = 0;
    g_rx.reset();
    g_exist_frame = ~0ull;
}

// BUG-0223: which Civ world is open. Before, a single frame without the stage's local entity (an
// anchor built from the player civ's first city) counted as a world change: the host dropped the
// vehicle table, every city's owner and the boosts while the world was still there. C2 (BUG-1141):
// world_id.h is the one answer (the stage counter, leaving Civ for a mode that is not an editor,
// another player civilization object or save file); coop.cpp fires the hook below.
// C2 (BUG-1141): the Civ world that was open changed or closed (world_id.h fires it; an editor
// visit keeps it, decision 2)
LIFECYCLE_HOOK("civ.world", lifecycle::kWorldChange, -44, [](const lifecycle::Ctx& c) {
    if (c.from_stage != stage_epoch::index_of(modes::kCiv))
        return;
    g_n.world_checks_fired++;
    if (g_ok)
        H->log(M, SK_INFO, "civ: the world changed (%s, epoch %u)", world_id::why_name((world_id::Why)c.why),
               game::stage_epoch(modes::kCiv));
    on_world_change();
}, true);
// the Civ world's root object for world_id.h (coop.cpp): the player civilization, null outside Civ
const void* world_anchor() { return game::mode_id() == modes::kCiv && g_ok && nm() ? player_civ() : nullptr; }


void for_each_owned(void (*fn)(void* obj, const net::Vec3& pos, uint8_t owner, bool city, void* user), void* user)
{
    if (!active())
        return;
    if (g_cmd_listed_fix) { // BUG-1053: only objects the game's list has now (one walk), the callbacks after it
        struct E {
            void* o;
            net::Vec3 p;
            uint8_t owner;
            bool city;
        };
        std::vector<E> es;
        {
            game::LiveScope live;
            for (auto& [c, o] : g_city_owner)
                if (c && o != kShared && game::listed(c))
                    es.push_back({c, city_pos(c), o, true});
            if (is_host()) {
                hv_each([&](const HvRef& x) {
                    if (x.h->owner != 0xFE && hv_live(x))
                        es.push_back({x.v, veh_pos(x.v), x.h->owner, false});
                });
            } else
                gv_each([&](const GvRef& x) {
                    if (x.v && x.g->owner != 0xFE && gv_ok(x))
                        es.push_back({x.v, veh_pos(x.v), x.g->owner, false});
                });
        }
        for (auto& e : es)
            fn(e.o, e.p, e.owner, e.city, user);
        return;
    }
    for (auto& [c, o] : g_city_owner)
        if (c && game::object_alive(c) && o != kShared)
            fn(c, city_pos(c), o, true, user);
    if (is_host()) {
        hv_each([&](const HvRef& x) {
            if (x.h->owner != 0xFE)
                fn(x.v, veh_pos(x.v), x.h->owner, false, user);
        });
    } else {
        gv_each([&](const GvRef& x) {
            if (x.v && x.g->owner != 0xFE)
                fn(x.v, veh_pos(x.v), x.g->owner, false, user);
        });
    }
}

std::string status()
{
    char b[768];
    if (C.sess && C.sess->role() == net::Role::Host)
        snprintf(b, sizeof b,
                 "civ(host): vehicles=%u cities=%u owned_cities=%u cmds_done=%u rejected=%u builds_sent=%u gifts=%u "
                 "trade_pairs=%d trade_total=%d joint_hits=%u military_hits=%u religious=%u alarms=%u votes=%u sw_unvoted=%u boosts=%u "
                 "cities_founded=%u unselected=%u movie=%d buys_refused=%u chunks=%u max_chunk=%u siege_npc=%u creature_orders=%u/%u cmd_gone=%u\n",
                 (unsigned)hv_count(), (unsigned)g_host_cities.size(), (unsigned)g_city_owner.size(), g_n.cmds_done,
                 g_n.cmds_rejected, g_n.builds_sent, g_n.gifts, g_trade_pairs, g_trade_total, g_n.joint_hits, g_n.military_hits,
                 g_n.religious_boosts, g_n.alarms, g_n.votes, g_n.sw_unvoted, (unsigned)g_boost.size(), g_n.cities_founded, g_n.unselected, (int)movie_playing(), g_n.buys_refused, g_n.last_chunks,
                 g_n.max_chunk, g_n.siege_npc, g_n.creature_near, g_n.creature_moves, g_n.cmd_gone);
    else
        snprintf(b, sizeof b,
                 "civ(guest): mirrors=%u created=%u removed=%u cities=%u cmds_sent=%u builds_in=%u captures_in=%u "
                 "orders_dropped=%u growth_blocked=%u nodes_in=%u cities_in=%u cities_blocked=%u unselected=%u movie=%d "
                 "buildings_fixed=+%u/-%u off_slot=%u moved=%u ruins_in=%u ruins_differ=%u incomplete=%u older_chunks=%u income_blocked=%u cities_caught_up=%u ai_spawns_blocked=%u away_orders=%u local=%d founds_outside=%u mirror_place_skipped=%u away_ground=%u removed_local=%u\n",
                 (unsigned)gv_count(), g_n.created, g_n.removed, (unsigned)g_host_cities.size(), g_n.cmds_sent, g_n.builds_in,
                 g_n.captures_in, g_n.orders_dropped, g_n.growth_blocked, g_n.nodes_in, g_n.cities_in, g_n.cities_blocked, g_n.unselected, (int)movie_playing(), g_n.bfix_added,
                 g_n.bfix_removed, g_n.bfix_off_slot, g_n.bfix_moved, g_n.ruins_in, g_n.ruins_differ, g_rx.incomplete, g_rx.older, g_n.income_blocked, g_n.cities_caught_up, g_n.ai_spawns_blocked, g_n.away_orders, (int)local_play(), g_n.founds_outside, g_n.mirror_place_skipped, g_n.away_ground_checks, g_n.removed_local);
    char pl[260];
    snprintf(pl, sizeof pl, "planner: open=%d held=%u kills_held=%u kills_waiting=%u picks_refused=%u builds_held=%u build_orphans=%u save_stripped=%u cards_skipped=%u owner_unknown=%u met=%d build_refunds=%u/%.0f\n",
             planner_city() ? 1 : 0, g_n.planner_held, g_n.planner_kills_held, (unsigned)kill_waiting_count(),
             g_n.planner_picks_refused, (unsigned)g_held_builds.size(), g_n.build_orphans, g_n.save_stripped, g_n.cards_skipped, g_n.owner_unknown, g_met,
             g_n.build_refunds, g_build_refunded);
    char sh[320];
    snprintf(sh, sizeof sh, "shots: seen=%u tx=%u rx=%u active=%u fired=%u no_mirror=%u no_tool=%u hits_dropped=%u hits=%u no_attacker=%u att_vt=%08X targets=%u/%u no_target=%u counted=%d tried=%u refused=%u stops=%u released=%u\n",
             g_shots_seen, g_shots_tx, g_shots_rx, (unsigned)g_shots_in.size(), g_shots_fired,
             g_shots_nomirror, g_shots_notool, g_hits_dropped, g_n.hits_guest, g_n.hits_noatt, (unsigned)g_hit_att_vt, g_n.shots_tgt_found, g_n.shots_tgt_wanted, g_n.shots_notarget, (int)g_fix_shot_fired, g_n.shots_tried, g_n.shots_refused, g_n.shot_stops_seen, g_n.shot_released);
    char spb[240]; // BUG-0751
    snprintf(spb, sizeof spb, "space_planner: sent=%u built=%u away=%u failed=%u kept=%u kept_built=%u relayed=%u relay_in=%u dropped=%u city_written=%u\n",
             g_n.space_builds_sent, g_n.space_builds_in, g_n.space_builds_away, g_n.space_builds_failed,
             (unsigned)g_space_held.size(), g_n.space_held_built, g_n.space_relayed, g_n.space_relay_in,
             g_n.space_planner_dropped, g_n.space_city_written);
    char bh[160];
    snprintf(bh, sizeof bh, "building_hp: tx=%u rx=%u lowered=%u raised=%u no_match=%u\n", g_n.hp_tx, g_n.hp_rx, g_n.hp_lowered,
             g_n.hp_raised, g_n.hp_nomatch);
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
    snprintf(tr, sizeof tr, "tribe_ruins: tx=%u rx=%u removed=%u\n", g_n.tribes_tx, g_n.tribes_rx, g_n.tribes_removed);
    lf += tr;
    lf += ff_status();
    char mo[320]; // BUG-1236, BUG-1461, BUG-1478
    snprintf(mo, sizeof mo, "money: npc_money_held=%u passive_noted=%u income_cause=%d local_play_gate=%d holds=%d world_cmds_dropped=%u sw_paid_own=%u sw_short_own=%u\n",
             g_npc_money_held, g_passive_noted, (int)g_income_cause_fix, (int)g_local_play_gate_fix, (int)guest_holds(), g_n.world_cmds_dropped,
             g_sw_paid_own, g_sw_short_own);
    lf += mo;
    char tu[160];
    snprintf(tu, sizeof tu, "turrets: tx=%u rx=%u blocked=%u added=%u removed=%u\n", g_n.turret_tx, g_n.turret_rx,
             g_n.turrets_blocked, g_n.turrets_added, g_n.turrets_removed);
    char ts[128];
    snprintf(ts, sizeof ts, "turret_shots: fix=%d seen=%u fired=%u bad_combatant=%u\n", (int)g_fix_turret_weapon,
             g_n.turret_shots_seen, g_n.turret_fired, g_n.bad_comb);
    return std::string(b) + pl + sh + ts + bh + tu + lf + spb;
}

void for_each_target(void (*fn)(const char* kind, int id, int owner, const net::Vec3& pos, void* user), void* user)
{
    if (!active())
        return;
    if (g_cmd_listed_fix) { // BUG-1053: only objects the game's list has now (one walk), the callbacks after it
        struct E {
            const char* kind;
            int id, owner;
            net::Vec3 p;
        };
        std::vector<E> es;
        {
            game::LiveScope live;
            for (size_t i = 0; i < g_host_cities.size(); i++)
                if (void* c = g_host_cities[i]; c && game::listed(c)) {
                    uint8_t o = owner_of_city(c);
                    es.push_back({"city", (int)i, o == 0xFF ? -1 : (int)o, city_pos(c)});
                }
            if (is_host()) {
                hv_each([&](const HvRef& x) {
                    if (x.h->owner != 0xFE && hv_live(x))
                        es.push_back({"vehicle", x.id, x.h->owner, veh_pos(x.v)});
                });
            } else
                gv_each([&](const GvRef& x) {
                    if (x.g->owner != 0xFE && gv_ok(x)) // (BUG-0951)
                        es.push_back({"vehicle", x.id, x.g->owner, veh_pos(x.v)});
                });
        }
        for (auto& e : es)
            fn(e.kind, e.id, e.owner, e.p, user);
        return;
    }
    for (size_t i = 0; i < g_host_cities.size(); i++)
        if (void* c = g_host_cities[i]) {
            uint8_t o = owner_of_city(c);
            fn("city", (int)i, o == 0xFF ? -1 : (int)o, city_pos(c), user);
        }
    if (is_host()) {
        hv_each([&](const HvRef& x) {
            if (x.h->owner != 0xFE && game::object_alive(x.v))
                fn("vehicle", x.id, x.h->owner, veh_pos(x.v), user);
        });
    } else {
        gv_each([&](const GvRef& x) {
            if (x.g->owner != 0xFE && gv_ok(x)) // (BUG-0951)
                fn("vehicle", x.id, x.g->owner, veh_pos(x.v), user);
        });
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
                 "ruins=%d hp=%.0f spec=%d\n",
                 host_index_of_city(c), c, pol_of(c), pc && pol_of(c) == pol_of(pc) ? " (player)" : "",
                 o == 0xFF ? -1 : (int)o, (unsigned)blds.size(), p.x, p.y, p.z, F<float>(c, 0x748),
                 F<uint32_t>(c, 0x74C), F<float>(c, 0x750), F<float>(c, 0x758), ruins, hp, F<int>(c, kCitySpecialty));
        out += b;
    }
    for (void* v : all_vehicles()) {
        net::Vec3 p = veh_pos(v);
        uint16_t id = 0;
        if (C.sess && C.sess->role() == net::Role::Host) {
            HvRef it = hv_find(v);
            id = it ? it.id : 0;
        } else {
            GvRef it = gv_of(v);
            id = it ? it.id : 0;
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
                 "opos=(%.1f %.1f %.1f) scale=%.2f model=%08X serial=%u\n",
                 id, v, pol_of(v), o == 0xFF ? -1 : (int)o, F<int>(v, kVPurpose), F<int>(v, kVLoco),
                 F<uint8_t>(v, kVSelected), F<float>(v, kVHp), p.x, p.y, p.z, otype, ox, oy, oz, game::object_scale(v),
                 F<uint32_t>(v, kVModelKey), (unsigned)game::object_serial(v)); // (serial: the game's object, BUG-1481)
        out += b;
        GvRef gx = id && !(C.sess && C.sess->role() == net::Role::Host) ? gv_find(id) : GvRef{};
        if (gx) { // BUG-0186: the guest's view of the mirror (on the same line)
            out.pop_back();
            const GV& gi = *gx.g;
            const net::Vec3& ev = gi.est.vel;
            snprintf(b, sizeof b, " age=%.1f err=%.1f pushed=%.1f pushed5s=%.1f vel=%.1f top=%.1f jumps=%u htop=%.1f\n",
                     H->time_seconds() - gi.seen, gi.err, gi.pushed, gi.pushed_max,
                     std::sqrt(ev.x * ev.x + ev.y * ev.y + ev.z * ev.z), F<float>(v, 0x218), gi.est.capped,
                     gi.host_top);
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
            HvRef it = hv_find(v);
            r.id = it ? it.id : -1;
        } else {
            GvRef it = gv_of(v);
            r.id = it ? it.id : -1;
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
void* local_vehicle(uint16_t id)
{
    if (C.sess && C.sess->role() == net::Role::Host)
        return hv_by_id(id).v;
    return gv_live(id); // (BUG-0951)
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


void for_each_diag(void (*fn)(const char* key, const net::Vec3& p, void* user), void* user)
{
    if (!active() || is_host())
        return;
    double now = H->time_seconds();
    char k[8];
    gv_each([&](const GvRef& x) {
        const GV& g = *x.g;
        if (!gv_ok(x) || now - g.seen > 3.0) // (BUG-0951)
            return;
        snprintf(k, sizeof k, "w%u", (unsigned)x.id);
        fn(k, g.est.at(now, net::g_lead_fix ? net::g_one_way : 0.03), user);
        snprintf(k, sizeof k, "e%u", (unsigned)x.id);
        fn(k, net::Vec3{(float)(now - g.est.t), net::dist(net::Vec3{}, g.est.vel), g.err}, user);
    });
}

std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    auto alive = all_vehicles();
    auto live = [&](void* x) { return std::find(alive.begin(), alive.end(), x) != alive.end(); };
    if (C.sess && C.sess->role() == net::Role::Host) {
        hv_each([&](const HvRef& x) {
            if (live(x.v))
                v.push_back({"v" + std::to_string(x.id), veh_pos(x.v), veh_rot(x.v)});
        });
    } else {
        gv_each([&](const GvRef& x) {
            if (x.v && live(x.v))
                v.push_back({"v" + std::to_string(x.id), veh_pos(x.v), veh_rot(x.v)});
        });
    }
    return v;
}

// own selected vehicles (ids as the host knows them); none: the own city whose menu was opened
static void own_selection(std::vector<uint16_t>& vehicles, int& city)
{
    uint8_t me = C.sess->my_id();
    city = -1;
    game::LiveScope live; // (BUG-1053: one walk)
    if (C.sess->role() == net::Role::Host) {
        hv_each([&](const HvRef& x) {
            if (x.h->owner == me && (!g_cmd_listed_fix || hv_live(x)) && F<uint8_t>(x.v, kVSelected))
                vehicles.push_back(x.id);
        });
    } else
        gv_each([&](const GvRef& x) {
            if (x.v && x.g->owner == me && (g_cmd_listed_fix ? gv_ok(x) : true) && F<uint8_t>(x.v, kVSelected)) // (BUG-1053)
                vehicles.push_back(x.id);
        });
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
