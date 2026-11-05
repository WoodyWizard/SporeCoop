// Per game mode adapters: what the local player controls in that mode and how a remote
// player is shown ("puppet"). docs/07-modes.md describes the design of every mode.
#pragma once

#include <cstdint>
#include <string>

#include "driver.h"
#include "net/protocol.h"

namespace modes {

constexpr uint32_t kCell = 0x1654C00, kCreature = 0x1654C01, kTribe = 0x1654C02, kCiv = 0x1654C04,
                   kSpace = 0x1654C05, kEdit = 0x1654C06, kLoad = 0x1654C08, kScenario = 0x1654C10,
                   kEditor = 0x00DBDBA1, kMenu = 0x2CCD1D2;

struct Adapter {
    const char* name;
    uint32_t mode;
    // where the local player is inside this mode: players see each other only when equal.
    // nullptr = the current save (players share the host's world)
    uint32_t (*place)();
    // true: guests keep their own saves (space: the galaxy is the same for everyone)
    bool own_worlds;
    // the entity the local player controls right now (nullptr if none)
    void* (*local)();
    // fill position/orientation/velocity/health/flags from the local entity
    void (*fill)(void* local, net::PlayerState& st);
    // create the visible copy of a remote player; `local` helps to pick a model/herd
    void* (*spawn)(const net::PlayerState& s, void* local);
    // move the puppet towards the remote state
    void (*drive)(void* puppet, const net::PlayerState& s, drive::State& ds, double now);
    void (*despawn)(void* puppet);
    // height of the name tag above the entity origin (nullptr: 3 m)
    float (*tag_height)(void* entity);
    // false when the game removed the puppet on its own (nullptr: always alive)
    bool (*alive)(void* puppet);
    // name tags go "up" along the planet normal; flat worlds (cell) use +z instead
    bool flat;
};

// position of any entity created/controlled by adapter `a`
net::Vec3 entity_pos(const Adapter* a, void* e);

const Adapter* for_mode(uint32_t mode);
// the species a remote player's creature puppet is built from (its own creation), nullptr = local
extern void* (*g_species_for_player)(uint8_t player);
// the model a remote player's cell (kind 1) / spaceship (kind 2) puppet uses; false = the local one
extern bool (*g_model_for_player)(uint8_t player, int kind, uint32_t key[3]);
const char* mode_name(uint32_t mode);
uint32_t current_world(); // crc32 of the current save name, 0 if none
// tests (BUG-0223): Tribe's and Civ's local entity is null for the next `frames` frames
// (test_tick() once per frame counts them down)
void test_null_local(int frames);
void test_tick();
// tests (BUG-0280): the local Tribe / Civ pointer stays at `p` (as if the mouse pointed there);
// pin=false: the mouse again
void test_pin_marker(bool pin, const net::Vec3& p);
// Tribe / Civ: the pointer marker `e` of adapter `a` (own or a partner's) moved in the last `within` s
// (false for every other adapter); the sync-check leaves a moving pointer out (BUG-0380)
bool rts_marker_moving(const Adapter* a, void* e, double within);
// Tribe / Civ: seconds since the pointer marker `e` of adapter `a` last moved (-1: not a pointer); the trace's
// `ptr_still=` (BUG-0416: the monitor judges a partner's copy only of a pointer that stood still)
double rts_marker_still(const Adapter* a, void* e);
uint32_t current_place(const Adapter* a); // adapter place() or current_world()
std::string current_world_name(); // the planet file being played, UTF-8 ("Name.spo"; "SaveGame" in menus)
std::string raw_world_name(); // the game's CurrentGameName as is (may be "snapshot.spo", BUG-0258)
void world_name_tick(); // once per frame: current_world_name() follows the game (BUG-0258)

// ---- cell stage helpers (pool access, creation with a given model) ----
uint8_t* cell_game();             // cCellGame*
uint8_t* cell_at(uint32_t idx);   // live cell for a pool index, else nullptr
// BUG-0348: false while the cell broadphase's pending list is near full (a cell made now would
// overflow it); counts the refusals (cell_sap_held)
bool cell_sap_room();
uint32_t cell_sap_held();
extern bool cell_at_fix;           // coop.pool fix cellat 0: the old bound (slot == highWater passed, BUG-0083)
bool cell_is_puppet(void* cell);  // a remote player's cell
// BUG-0271 (core.stale_local): no cell is read from a pool block the game already freed
extern bool stale_fix;
void own_ground_tick(double now); // BUG-0592 (creature.own_ground): once per frame, before the drives
extern uint32_t g_own_grounded;       // BUG-0592: times the own creature was put onto its ground
extern uint32_t g_puppets_gone_alive; // BUG-0507: Creature puppets found freed by the game (creature.puppet_alive)
bool cell_usable(void* cell);     // the local cell or a puppet, still live in a mapped pool
// our frame scope (on_frame, on_overlay): no game code runs inside, so the pool check is cached
void cell_scope(bool enter);
struct CellScope {
    CellScope() { cell_scope(true); }
    ~CellScope() { cell_scope(false); }
};
struct CellGoneStats {
    uint32_t checks = 0;    // pool block checks made
    uint32_t outside = 0;   // of them outside a frame scope (commands, the keepalive)
    uint32_t pool_gone = 0; // the block was no longer mapped
    uint32_t refused = 0;   // cell_fill refused a cell
    uint32_t windows = 0;   // frame probe: times the block went away while the mode read Cell
    uint32_t frames_gone = 0; // frames of ours in Cell with the block unmapped
};
void cell_probe_tick(); // once per frame: the diagnostic above (whatever the switch)
const CellGoneStats& cell_gone_stats();
// BUG-0111: a Space puppet is ours only while the UFO at its address still has what space_spawn gave
// it (model key, invincible, no owner mission); any listed Defender (UfoType 6) passed before, so a
// game Defender at a reused address became the partner's ship. coop.fleet typefix 0: the old check
extern bool ufo_ident_fix;
bool space_puppet_ok(void* u); // listed, alive and still our puppet
// BUG-0378: pin a puppet / mirror UFO so the game's controller update draws it at p (its own offset
// taken out, mNextPosition / mDestination / mZoomAltitude, marked for squadron's controller hook);
// fix false: p itself (the old pin)
void space_pin(void* u, const float p[3], bool fix);
extern bool puppet_pin_fix; // coop.fix space.puppet_pin (BUG-0378)
void space_pin_frame();       // every Present, before the drives: nothing pinned yet
bool space_pinned(void* u);
// diagnostics (BUG-0388, coop.fleet track): the pose space_drive gave puppet u last, and how far past the
// partner's newest snapshot that state was extrapolated (s, 0 = interpolated); false = never driven
extern double g_drive_extrap; // set by coop.cpp drive_puppet before the adapter's drive
bool space_drive_note(void* u, float p[3], float& extrap);   // pinned (fix on) since the last Present: its controller update is skipped
// BUG-0218: the identity is the object serial; our puppet taken by a game mission (owner mission
// +0x800) is retired: held still, not sent to the guests, its shots dropped, destroyed (deferred)
// once the mission lets go, left to the game at session end (a log line). fleet.puppet_id
extern bool puppet_id_fix;
bool space_retired(void* u);
void space_retired_tick();              // every frame (squadron::tick)
void space_retired_clear(const char* why); // session end, unload
// tests: impostor_id (the recorded serial changes), impostor_mission on|off, else the counters
std::string space_ident_test(void* u, const std::string& what, bool on);
void cell_forget();               // the Cell stage was left: forget the puppets' pool indices
uint32_t cell_create(const net::Vec3& pos, void* res_ref, int level, const uint32_t key[3], const net::Quat* rot);
uint32_t cell_unknown_models(); // BUG-0455: cells not made because their model's creation is not here
void cell_destroy(uint32_t idx);
// BUG-0308: the Cell stage is being left (quit posted for a world load or the menu) and coop.fix
// cell.leave_guard is on: no mirror creation and no pool write of ours until the mode changes
bool cell_leaving();
// move a cell exactly (soft-body cells take their position from their particles, docs/re/cell.md §6)
void cell_move(uint8_t* c, const net::Vec3& p, bool still = false);
void mat_to_quat(const float* m, net::Quat& q);
void quat_to_mat(const net::Quat& q, float* m);
// BUG-0499 (creature.puppet_jump): the speed a partner's states may not be blended over (0: off)
float puppet_jump_speed();
// BUG-0376 (creature.puppet_air), tests: coop.track air <v>
extern int g_puppet_air_variant;

} // namespace modes
