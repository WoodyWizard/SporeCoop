// The internals of the Civ module (civ.cpp) that its other files share: civ_tables.cpp (the vehicle tables and
// the snapshot receiver, C1 / C4 of 0.7.15) and civ_debug.cpp (the coop.civ console command). Included only by
// mods/coop/civ*.cpp. C8 of 0.7.15 (BUG-1164): moved out of civ.cpp without edits;
// docs/01-architecture.md#arch-civ-registry.
#pragma once

#include "civ.h"
#include "call_guard.h"
#include "civ_logic.h"
#include "squadron.h"
#include "lifecycle.h"
#include "busy_model.h"
#include "world_id.h"
#include "mirror_registry.h"
#include "player_life.h"
#include "census.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
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
#include "net/snapshot_rx.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "settings.h"
#include "fields.h"
#include "creations.h"
#include "savestrip.h"
#include "world.h"
#include "owner_claims.h"
#include "autosave_glue.h"
#include "store.h"

using skg::thiscall;
using skg::vcall;

namespace tribe {
extern bool g_cycle_order_fix; // net.cycle_order (BUG-0612)
}

namespace civ {

extern const SkHost* H;
extern SkMod* M;
extern Ctx C;
extern bool g_ok;

// ---- layout (docs/re/civ.md) ----
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

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

// the diagnostic counters (coop.civ, coop.civ tables / status lines, the tests read them; never reset by a session
// or world change, as before C8 of 0.7.15, BUG-1164)
struct Counters {
    uint32_t away_orders = 0;
    uint32_t planner_held = 0, planner_kills_held = 0, planner_picks_refused = 0;
    uint32_t planner_stale_refused = 0;
    uint32_t probe_frames = 0, probe_stale_reads = 0, probe_stale_cities = 0, probe_seen = 0;
    uint32_t cards_skipped = 0, owner_unknown = 0;
    uint32_t kept_restored = 0, kept_saved = 0; // tests
    uint32_t gifts = 0;
    uint32_t claim_cities = 0, claim_vehs = 0, claims_held = 0, claims_back = 0;
    uint32_t world_resets = 0;
    uint32_t cmd_gone = 0;
    uint32_t cmds_done = 0, cmds_rejected = 0, builds_sent = 0, unselected = 0, buys_refused = 0;
    uint32_t orphans_given = 0;
    uint32_t veh_stale = 0;
    uint32_t last_chunks = 0, max_chunk = 0; // host: chunks per cycle, largest chunk (bytes)
    uint32_t mirror_place_skipped = 0, away_ground_checks = 0;
    unsigned creature_near = 0, creature_moves = 0; // BUG-0970: guest creature orders mapped to a near one / a move
    uint32_t turret_shots_seen = 0, turret_fired = 0, bad_comb = 0; // host: turret shots noted; guest: copies fired, bad combatants
    uint32_t shots_tgt_wanted = 0, shots_tgt_found = 0;
    uint32_t shots_notarget = 0; // guest: shots not fired, their target is not in this game (BUG-0534)
    uint32_t shots_tried = 0, shots_refused = 0; // host: FireTool calls that made no shot; guest: FireTool false
    uint32_t shot_stops_seen = 0, shot_released = 0;
    uint32_t tribes_tx = 0, tribes_rx = 0, tribes_removed = 0;
    uint32_t hits_guest = 0, hits_noatt = 0; // guest: TakeDamage calls in Civ seen by the hooks, without an attacker
    uint32_t removed_local = 0; // the guest's own vehicles removed (civ_ai_spawn: not the host's departed ones)
    uint32_t created = 0, removed = 0, cmds_sent = 0, builds_in = 0, captures_in = 0, orders_dropped = 0, growth_blocked = 0, nodes_in = 0;
    unsigned build_skipped = 0;
    uint32_t space_builds_sent = 0, space_builds_in = 0, space_builds_away = 0, space_builds_failed = 0;
    uint32_t space_planner_dropped = 0; // planner buildings taken away again before the planner closed (not sent)
    uint32_t space_city_written = 0;
    uint32_t cities_founded = 0, cities_in = 0, cities_blocked = 0, cities_caught_up = 0;
    uint32_t roads_sent = 0, roads_in = 0, roads_added = 0, roads_failed = 0;
    uint32_t income_blocked = 0;
    uint32_t ai_spawns_blocked = 0;
    uint32_t credit_by[4] = {}; // host: captures credited by command, hit, siege, nearest vehicle
    uint32_t away_captures_sent = 0;
    uint32_t joint_hits = 0, military_hits = 0, religious_boosts = 0, alarms = 0, votes = 0;
    uint32_t siege_npc = 0;
    uint32_t city_buys_sent = 0, city_buys_done = 0, city_buys_refused = 0;
    uint32_t spec_asked = 0, spec_shown = 0, spec_answered = 0, spec_applied = 0, spec_rejected = 0,
             spec_unpaused = 0; // BUG-1175
    uint32_t boost_restored = 0; // tests: boosts put back on a living vehicle that left the tables (BUG-0773)
    uint32_t sw_refused = 0;
    unsigned sw_unvoted = 0; // superweapons fired without a vote (civ.superweapon_vote off; coop.civ sw_unvoted)
    uint32_t turrets_blocked = 0, turrets_added = 0, turrets_removed = 0, turret_tx = 0, turret_rx = 0;
    unsigned probe_hits = 0;
    uint32_t captions_closed = 0;
    uint32_t caption_arms = 0; // tests
    uint32_t build_refunds = 0;
    unsigned bfix_logged = 0, host_offslot_logged = 0;
    uint32_t bfix_off_slot = 0; // BUG-0602: rebuilt here more than 4 m from the host's spot
    uint32_t bfix_moved = 0;
    uint32_t hp_tx = 0, hp_rx = 0, hp_lowered = 0, hp_raised = 0, hp_nomatch = 0;
    uint32_t build_orphans = 0;
    uint32_t bfix_added = 0, bfix_removed = 0, ruins_differ = 0, ruins_in = 0;
    uint32_t save_stripped = 0;
    unsigned veh_design_checked = 0, veh_design_fallback = 0;
    unsigned bound_redesigned = 0;
    unsigned bld_rekeys = 0, bld_pending = 0, bld_restored = 0;
    uint32_t founds_outside = 0; // BUG-0319 diagnostics: city foundings received outside the Civ mode
    uint32_t space_held_built = 0, space_relayed = 0, space_relay_in = 0;
    uint32_t space_place_seen = 0;
    uint32_t world_checks_fired = 0;
};
extern Counters g_n;

struct HV {
    uint8_t owner;
    uint32_t pol; // the vehicle's political id when it was listed (BUG-0059: a reused address)
};
// host: an entry of the table (id: its net id, v: its vehicle, serial: v's cGameData::mID when it was listed (BUG-0951:
// a reused address of the same nation too), h: its data)
struct HvRef {
    uint16_t id = 0;
    void* v = nullptr;
    uint32_t serial = 0;
    HV* h = nullptr;
    explicit operator bool() const { return h != nullptr; }
};
constexpr uint32_t kVehicleNoun = 0x18C6DE8;
struct GV { // (its vehicle and the vehicle's serial are the registry entry's ref)
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
constexpr uint32_t kCreatureNoun = 0x018EB45E; // cCreatureAnimal: the wild creatures (not synced in Civ, BUG-0970)
constexpr uint32_t kBuildingModelType = 0x2399BE55;

struct Target {
    uint8_t kind = 0; // 0 none, 1 vehicle id, 2 host city index, 3 object by noun + position
    uint16_t id = 0;
    uint32_t noun = 0;
    net::Vec3 pos;
};
enum : uint8_t { CMD_ORDER = 0, CMD_MOVE = 1, CMD_BUY = 2, CMD_BUILD = 3, CMD_GIVE = 4, CMD_SPACE_BUILD = 5 };
// (also 5 the superweapon request, 6 CMD_BUYCITY (BUG-0877) and 7 CMD_CAPTURE (BUG-0878), written as numbers
// where they are sent above this line)
struct LastOrder {
    int type = -1;
    uint8_t kind = 0;
    uint16_t id = 0;
    uint32_t noun = 0;
};
struct Route {
    void* partner;
    void* obj;
    int type;
};
struct SpecialtyAsk {
    uint8_t ci;
    int type;
};
struct Boost {
    float orig;
    double until;
    uint32_t pol = 0; // the vehicle's nation when it was boosted (BUG-0773: restore only on that vehicle)
};
struct KeptCity {
    net::Vec3 pos;
    std::string owner; // the player's name; "" = the shared capital
    bool gifted, captured;
};
// tests (BUG-0951, class BUG-0426): mirror <id>'s vehicle is removed by the game (the disband path, kill_local: no
// bookkeeping of ours, the mirror still names it), then once a frame a vehicle of the same purpose and locomotion is
// made for this guest's own civilization (the game's own design), 40 m away, removed again unless it is at the freed
// address, until the game gives it that block
struct VehHunt {
    uint8_t* target = nullptr;
    uint16_t id = 0;
    int left = 0, tries = 0, purpose = 0, loco = 0;
    net::Vec3 at{};
    void* hit = nullptr;
    uint32_t hit_serial = 0;
};
// BUG-0766 / BUG-0884: a colony building for a planet this game is not on yet (host: a guest's; guest: the host's)
struct SpaceBuild {
    int from;
    uint32_t place, noun, world;
    net::Vec3 cp, p;
    double tried = 0;    // the first time it found no free plot here (0 = not yet)
    double next_try = 0; // then the next try
};
using AddTurret_t = int(__fastcall*)(void*);

using ToolMouseUp_t = int(SK_THISCALL*)(void*, void*);

using VTakeDamage_t = int(SK_THISCALL*)(void*, float, uint32_t, int, const void*, void*);

struct LocalFire {
    uint32_t calls = 0, fired = 0;
    uintptr_t owner_vt = 0;
};

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

using SpawnNextCity_t = bool(SK_THISCALL*)(void*, int, void*);

struct BldRekey {
    uint32_t orig[3];
    uint32_t ours;
};

using AddMoney_t = void(SK_THISCALL*)(void*, float, int);

using CityBuy_t = void(SK_THISCALL*)(void*, uint32_t, float);

using SetMoveOrder_t = int(SK_THISCALL*)(void*, float, float, float, int);

using SetOrder_t = int(SK_THISCALL*)(void*, void*, int, int);

struct HeldBuild {
    uint8_t from;
    void* city;
    uint32_t noun;
    net::Vec3 pos;
    float paid = 0; // BUG-0807: what the guest paid for it in its planner
};

struct PendingCapture {
    uint8_t owner;
    double at;
};

// ---- the vehicle tables (civ_tables.cpp) ----
extern mirror::Registry<uint16_t, HV> g_hvr;
extern mirror::Registry<uint16_t, GV> g_gvr;
extern net::SnapshotReceiver<uint16_t> g_rx;

// guest: a mirror (v: its local vehicle or null, serial: v's cGameData::mID when it was bound)
struct GvRef {
    uint16_t id = 0;
    GV* g = nullptr;
    void* v = nullptr;
    uint32_t serial = 0;
    explicit operator bool() const { return g != nullptr; }
};

HvRef hv_find(void* v);
std::string shot_probe();          // civ_shots.cpp: tests (BUG-1322)
std::string shot_listed_status(); // civ_shots.cpp (BUG-1322)
HvRef hv_by_id(uint16_t id);
size_t hv_count();
uint16_t hv_add(void* v, uint8_t owner);
void hv_drop(uint16_t id);
void hv_clear();
GvRef gv_find(uint16_t id);
GvRef gv_at(uint16_t id);
bool gv_id_of(void* v, uint16_t& id);
GvRef gv_of(void* v);
bool gv_bound(void* v);
bool gv_ok(const GvRef& x);
void* gv_live(uint16_t id);
size_t gv_count();
void gv_bind(uint16_t id, void* v, bool made);
void gv_unbind(uint16_t id);
void gv_clear();
bool kill_waiting(void* v);
void kill_wait(void* v, uint32_t serial);
size_t kill_waiting_count();

template <class Fn>
static void hv_each(Fn&& fn)
{
    g_hvr.for_each([&](auto& e) {
        if (e.ref.p)
            fn(HvRef{e.id, e.ref.p, e.ref.serial, &e.data});
    });
}

template <class Fn>
static void gv_each(Fn&& fn)
{
    g_gvr.for_each([&](auto& e) { fn(GvRef{e.id, &e.data, e.ref.p, e.ref.serial}); });
}

// ---- civ.cpp's state and functions the other files use ----
extern uintptr_t a_CivByPol;
extern uintptr_t a_GetAllCivs;
extern uintptr_t a_GetMoney;
extern uintptr_t a_PlayerCiv; // BUG-9274
extern uintptr_t a_EditorPickUp;
extern uintptr_t a_VehicleRemove;
extern uintptr_t a_DestroyInst;
extern uintptr_t a_AddTradeRoute;
extern uintptr_t a_AddTurret;
extern uintptr_t a_CombatantDamage;
extern uintptr_t a_DesignId;
extern uintptr_t a_DesignKey;
extern uintptr_t a_DestroyBuilding;
extern uintptr_t a_Die;
extern uintptr_t a_OrderStamp;
extern uintptr_t a_PushEvent;
extern uintptr_t a_SetMoveOrder;
extern uintptr_t a_SetOrder;
extern uintptr_t a_SetSelected;
extern uintptr_t a_SlotsLeft;
extern uintptr_t a_SpawnVehicle;
extern AddTurret_t o_AddTurret;
extern bool g_fix_away;
extern uint64_t g_exist_frame;
extern void* g_editor;
extern bool g_fix_planner_stale;
extern bool g_planner_probe;
extern int g_met;
extern std::unordered_map<void*, uint8_t> g_city_owner;
extern std::vector<void*> g_host_cities;
extern std::unordered_set<void*> g_city_gifted;
extern std::unordered_set<void*> g_city_captured;
extern bool g_fix_owners_kept;
extern std::map<std::string, std::vector<KeptCity>> g_kept_cities;
extern std::string g_kept_key;
extern bool g_store_fix;
extern std::unordered_map<void*, std::string> g_city_claim;
extern std::unordered_map<void*, std::string> g_veh_claim;
extern uint32_t g_claims_serial;
extern bool g_cities_matched;
extern bool g_vehs_matched;
extern uint16_t g_next_id;
extern bool g_veh_hold;
extern size_t g_chunk_budget;
extern bool g_applying;
extern LastOrder g_last_order;
extern bool g_fix_cities;
extern std::map<uintptr_t, uint32_t> g_local_spawns;
extern bool g_fix_aispawn;
extern uint8_t g_hit_by;
extern std::unordered_map<void*, std::unordered_map<uint8_t, double>> g_siege;
extern std::unordered_set<void*> g_joint_told;
extern bool g_fix_specialty_buyer;
extern std::deque<SpecialtyAsk> g_spec_queue;
extern int g_spec_open;
extern std::unordered_map<void*, Boost> g_boost;
extern std::unordered_map<void*, double> g_alarm_at;
extern int g_sw_last_req;
extern int g_sw_fired;
extern bool g_fix_caption;
extern double g_caption_wait;
extern double g_caption_armed;
extern const char* g_build_fail;
extern bool g_fix_build_money;
extern std::unordered_map<void*, int> g_bfix_seen;
extern bool g_veh_force;
extern uint32_t g_veh_force_key[3];
extern VehHunt g_vh;
extern bool g_veh_lifetime_fix;

std::vector<void*> all_cities();
std::vector<void*> all_vehicles();
void boost_tick(double now);
void* build_in_city(void* city, uint32_t noun, const net::Vec3& pos, bool pay);
uint32_t building_design_id(uint32_t type);
bool city_listed(void* c);
net::Vec3 city_pos(void* c);
bool exists(void* v);
void guest_tick(double now);
void SK_CDECL h_AddCapture(void* ctx, void* city, void* meter, uint32_t owner, float dmg, uint32_t att, bool f);
int SK_THISCALL h_Capture(void* city, uint32_t pol, int type, int a3, int a4, int a5);
bool SK_THISCALL h_FireSW(void* civ, int type, const float* target);
void SK_THISCALL h_OnAttacked(void* city, int kind, uint32_t att);
bool has_route(void* a, void* b);
std::string host_buildings_list();
void* host_buy(void* city, int loco);
void host_command(uint8_t from, net::Reader& r);
int host_index_of_city(void* c);
bool is_guest();
bool is_host();
bool found_next_site(); // BUG-9274
void kill_local(void* v);
bool local_play();
void* local_vehicle(uint16_t id);
bool movie_playing();
void* nm();
uint8_t owner_of_city(void* c);
void* planner_city();
void planner_moved(std::vector<void*>& out);
bool planner_pick_ok(void* ed, void* sp);
void* player_civ();
uint32_t pol_of(void* o);
std::string push_probe();
void request_buy(void* city, int loco);
std::vector<Route> routes_of(void* city);
void send_cmd(net::Writer& w);
void* space_build_here(const SpaceBuild& sb, bool quiet = false);
void space_write_city(void* city);
std::vector<std::pair<void*, net::Vec3>> tribes_here();
std::vector<std::pair<void*, net::Vec3>> turrets_of(void* city);
uint32_t type_for_noun(uint32_t noun);
std::vector<void*> vec_of(void* v);
std::string veh_hunt_status();
void* veh_mirror(uint16_t id);
net::Vec3 veh_pos(void* v);
void write_target(net::Writer& w, const Target& t);

extern uintptr_t a_FireTool;
extern uintptr_t a_ToolMgrGet;
extern uintptr_t a_ToolMouseUp;
bool cosmetic_hit(void* att);
extern bool g_fix_creature_target;
extern bool g_fix_shot_fired;
extern bool g_fix_turret_weapon;
extern uintptr_t g_hit_att_vt;
extern uint32_t g_hits_dropped;
extern std::map<uintptr_t, LocalFire> g_local_fire;
extern std::map<uint32_t, uint32_t> g_shot_tools;
extern std::map<std::pair<uint16_t, uint32_t>, Shot> g_shots;
extern uint32_t g_shots_fired;
extern std::map<std::pair<uint16_t, uint32_t>, Shot> g_shots_in;
extern uint32_t g_shots_nomirror;
extern uint32_t g_shots_notool;
extern uint32_t g_shots_rx;
extern uint32_t g_shots_seen;
extern uint32_t g_shots_tx;
extern std::unordered_map<void*, int> g_tribe_extra_seen;
extern std::unordered_set<void*> g_turret_shooters;
void guest_fire(double now);
void guest_tribes(net::Reader& r);
int SK_THISCALL h_ToolMouseUp(void* mgr, void* tool);
int SK_THISCALL h_VTakeDamage(void* s, float d, uint32_t pol, int t, const void* dir, void* a);
void host_send_shots(double now);
void host_send_tribes(double now);
constexpr float kCreatureReach = 40.f;
constexpr uint8_t kShotChunk = 0xFE;     // MSG_CIV_STATE chunk index of a shot list (chunks 0)
constexpr uint8_t kShotChunkFired = 0xFC;  // MSG_CIV_STATE chunk index of a counted shot list (chunks 0)
constexpr uint8_t kTribeChunk = 0xFA;
constexpr uintptr_t kVModelKey = 0xC4;
extern ToolMouseUp_t o_ToolMouseUp;
extern VTakeDamage_t o_VTakeDamage;
void on_destroy_instance(void* obj, uintptr_t ra);
uint32_t pos_key(const net::Vec3& p);
void guest_shots(net::Reader& r, bool counted);
bool vtable_in_game(const void* obj);
extern uintptr_t a_ChangeOwner;
extern uintptr_t a_CreateVehicle;
extern uintptr_t a_ExpansionGet;
extern uintptr_t a_NodeClaim;
extern uintptr_t a_NodeUnclaim;
extern uintptr_t a_SetCapture;
void drop_foreign_selection();
extern std::unordered_map<void*, BldRekey> g_bld_rekeyed;
extern size_t g_cities_waiting_told;
extern double g_city_short_since;
extern bool& g_cycle_order_fix;
extern bool g_extrap_fix;
extern bool g_instant_found_fix;
extern double g_last_city_spawn;
extern bool g_place_now_fix;
extern bool g_vel_cap_fix;
extern bool g_vel_cap_host_fix;
void guest_build(net::Reader& r);
void guest_building_hp(net::Reader& r);
void guest_roads(net::Reader& r);
void guest_state(net::Reader& r);
void host_buildings_session_end();
constexpr uint32_t kFoundInstant = 1; // MSG_CIV_BUILD kNextCitySite noun: the host founded it instantly
constexpr uint8_t kNextCitySite = 0xFF; // MSG_CIV_BUILD city index: "found the next city site", noun 0
constexpr uint32_t kNodeNoun = 0x403DF5C;
constexpr uint8_t kRoadList = 0xFE; // MSG_CIV_BUILD city index: the host's trade routes follow
constexpr double kVehAhead = 0.15;
constexpr double kVehLead = 0.3; // BUG-0425
extern SpawnNextCity_t o_SpawnNextCity;
bool planner_holds(void* city);
bool planner_moves(void* v);
void prune_gone();
int slot_of(void* city, void* b, const net::Vec3& p, float* nearest, void** occ);
void to_host_spot(void* b, const net::Vec3& p);
void veh_hunt_tick();

void* local_city_near(const net::Vec3& p);
void save_end();
bool save_filter(void* o, uint32_t noun);
void save_start();
extern uintptr_t a_AddBuilding;
extern uintptr_t a_CinematicMgr;
extern uintptr_t a_CreateInstance;
extern uintptr_t a_DestroyInstance;
extern uintptr_t a_SetBuildingModel;
extern uintptr_t a_SnapToSlot;
extern uintptr_t a_SpendMoney;
extern uintptr_t a_VehicleCost;
bool building_model_ok(void* city, uint32_t type, uint32_t noun);
void caption_disarm();
extern int g_build_from;
extern float g_build_refunded;
extern std::unordered_map<void*, PendingCapture> g_capture_by;
extern int g_capture_for;
extern std::vector<claims::Spot> g_city_spots;
extern uint16_t g_cycle;
extern bool g_fix_away_capture;
extern bool g_fix_buy_city;
extern bool g_fix_capture_keeps;
extern std::vector<HeldBuild> g_held_builds;
extern std::unordered_map<void*, std::pair<uint32_t, double>> g_hp_sent;
extern double g_kept_at;
extern bool g_kept_tried;
extern double g_last_send;
extern int g_pending_owner;
extern std::map<std::string, uint8_t> g_present;
extern std::vector<uint8_t> g_split_for;
extern int g_trade_pairs;
extern int g_trade_total;
extern std::vector<claims::Spot> g_veh_spots;
void* host_city(size_t i);
void host_send_roads(double now);
void host_send_turrets(size_t ci, void* city, double now);
void host_specialty_answer(uint8_t from, net::Reader& r);
void host_superweapon_request(uint8_t from, net::Reader& r);
void host_tick(double now);
bool hv_live(const HvRef& x);
constexpr uint8_t kCmdSpecialty = 8; // host -> owner: u8 city, u32 capture type; guest -> host: u8 city, u8 take, u32 type
constexpr uint8_t kHpChunk = 0xFD;
void* nearest_player_city(const net::Vec3& p, uint8_t* owner = nullptr);
extern AddMoney_t o_AddMoney;
extern CityBuy_t o_CityBuy;
extern SetMoveOrder_t o_SetMoveOrder;
extern SetOrder_t o_SetOrder;
uint8_t owner_of_vehicle(void* v);
int present_id(const std::string& key);
Target read_target(net::Reader& r);
uint8_t role(uint8_t p);
net::Quat veh_rot(void* v);

void caption_arm();
} // namespace civ
