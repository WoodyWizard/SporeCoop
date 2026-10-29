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
uint32_t current_place(const Adapter* a); // adapter place() or current_world()
std::string current_world_name(); // the planet file being played, UTF-8 ("Name.spo"; "SaveGame" in menus)
std::string raw_world_name(); // the game's CurrentGameName as is (may be "snapshot.spo", BUG-0258)
void world_name_tick(); // once per frame: current_world_name() follows the game (BUG-0258)

// ---- cell stage helpers (pool access, creation with a given model) ----
uint8_t* cell_game();             // cCellGame*
uint8_t* cell_at(uint32_t idx);   // live cell for a pool index, else nullptr
extern bool cell_at_fix;           // coop.pool fix cellat 0: the old bound (slot == highWater passed, BUG-0083)
bool cell_is_puppet(void* cell);  // a remote player's cell
// BUG-0111: a Space puppet is ours only while the UFO at its address still has what space_spawn gave
// it (model key, invincible, no owner mission); any listed Defender (UfoType 6) passed before, so a
// game Defender at a reused address became the partner's ship. coop.fleet typefix 0: the old check
extern bool ufo_ident_fix;
bool space_puppet_ok(void* u); // listed, alive and still our puppet
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
void cell_destroy(uint32_t idx);
// move a cell exactly (soft-body cells take their position from their particles, docs/re/cell.md §6)
void cell_move(uint8_t* c, const net::Vec3& p, bool still = false);
void mat_to_quat(const float* m, net::Quat& q);
void quat_to_mat(const net::Quat& q, float* m);

} // namespace modes
