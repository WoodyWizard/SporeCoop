// Thin, offset-based access to Spore creature-stage objects (see docs/05-re-notes.md).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/protocol.h"
#include "sporekit.h"

namespace game {

// cCreatureAnimal layout (GOG 2016 build, matches ModAPI March2017 headers)
constexpr uintptr_t kSpatialOff = 0xC0;     // cLocomotiveObject/cSpatialObject subobject
constexpr uintptr_t kPosOff = 0xC0 + 0x04;  // cSpatialObject::mPosition
constexpr uintptr_t kRotOff = 0xC0 + 0x10;  // cSpatialObject::mOrientation
constexpr uintptr_t kVelOff = 0xC0 + 0x1C8; // cLocomotiveObject::mVelocity
constexpr uintptr_t kCombatOff = 0x5A8;     // cCombatant subobject
constexpr uintptr_t kMaxHpOff = 0x5A8 + 0x2C;
constexpr uintptr_t kHpOff = 0x5A8 + 0x38;
constexpr uintptr_t kSpeciesOff = 0xB20;    // cSpeciesProfile*
constexpr uintptr_t kSpeciesKeyOff = 0xB28; // ResourceKey
constexpr uintptr_t kFlagsOff = 0xB58;      // bit 0x200 = avatar
constexpr uintptr_t kDeadOff = 0xB5E;       // bool mbDead
constexpr uintptr_t kNounAvatarOff = 0x54;  // cGameNounManager::mpAvatar
constexpr uintptr_t kNounAvatarHerdOff = 0x58; // cGameNounManager::mpAvatarHerd
constexpr uintptr_t kHerdOff = 0x1674;     // cCreatureAnimal::mHerd

bool init(const SkHost* host, SkMod* mod); // resolve symbols; false = coop disabled
bool ready();                              // symbols ok
uint32_t mode_id();
bool in_creature_stage();
// BUG-0119: a counter of creature worlds (entering the creature stage, another avatar in it; the
// editors keep it). Identity of a world where the avatar's address is not enough (pooled objects).
// Call once per frame at least (coop.cpp on_frame); 0 always with set_world_epoch_fix(false) (tests)
uint32_t world_epoch();
void set_world_epoch_fix(bool on);
// BUG-0217: which world of each stage is open, as counters (stage_epoch.h has the rules: a load,
// a received world, another stage raise it; an editor visit keeps it). stage_epoch_tick() once per
// frame at the very top of on_frame, before any message or module tick; stage_epoch(kTribe) etc.
// read it. stage_epoch(kCreature) counts the same mode routes as world_epoch() without its avatar
// rule. No fix switch: callers gate their own use (tribe.editor_world, civ.world_epoch).
void stage_epoch_tick();
uint32_t stage_epoch(uint32_t stage_mode);
void* avatar();
// BUG-0264: the stage is being left for the galaxy menu (the quit message 0x4715068 posted or
// handled): avatar() returns null until the mode changes (30 s at most). world.leave_avatar.
struct LeaveStats {
    unsigned windows = 0;    // stages left
    unsigned frames = 0;     // frames seen while leaving
    unsigned raw_gone = 0;   // frames where the avatar field held an object the game no longer lists
    unsigned used_gone = 0;  // frames where the mod used such an avatar (the crash)
    unsigned refused = 0;    // avatar() calls answered with null while leaving
    unsigned bad_vtable = 0; // avatars refused for vtables outside the game
    unsigned stage_gone = 0; // an unlisted avatar seen outside the window (1 Hz check)
};
void stage_leaving(const char* why);
void stage_leave_cancel();
bool leaving_stage();
const LeaveStats& leave_stats();
void leave_probe_tick(void* used);
net::Vec3 position(void* c);
net::Quat orientation(void* c);
net::Vec3 velocity(void* c);
float health(void* c);
float max_health(void* c);
bool is_dead(void* c);
uint32_t species_instance(void* c);
void* species_profile(void* c);

// Every creature must belong to a herd: game code calls GetHerd() on neighbours
// without a null check (0xD41A20 -> 0xC046B0, see docs/05-re-notes.md).
void* avatar_herd();
void* spawn_creature(const net::Vec3& pos, void* species, void* herd);
// cGameNounManager::DestroyInstance; a creature (cCreatureAnimal) is kept until the running
// cinematic is over (it may be one of its actors), destroy_deferred_tick() removes it then
void destroy_creature(void* c);
void destroy_deferred_tick(); // once per frame
void set_destroy_defer(bool on); // tests (off: at once)
void set_defer_keeps_puppet(bool on); // tests (off: a deferred puppet leaves the puppet list at once, BUG-0066)
bool destroy_pending(void* c);     // queued by destroy_creature, still there
std::string destroy_deferred_status();
void teleport(void* c, const net::Vec3& pos, const net::Quat& rot);
// cSpatialObject::SetPosition (vtable 0x38): moves without touching locomotion/animation
void set_position(void* c, const net::Vec3& pos);
void set_orientation(void* c, const net::Quat& rot);
void move_to(void* c, const net::Vec3& dst, float stop_dist);
void stop(void* c);
void set_speed(void* c, float speed);
float scale(void* c);
float bounding_radius(void* c);
// the terrain's distance from the planet's centre in the direction of pos (cPlanetModel::GetHeightAt), 0 without a planet
float ground_radius(const net::Vec3& pos);
void set_scale(void* c, float s);

// Animation events: called for every PlayAnimation/PlayAnimationTo on any creature.
using AnimListener = void (*)(void* creature, uint32_t anim_id, int block, bool disable_blend, bool to_target);
void set_anim_listener(AnimListener fn);
void play_animation(void* c, uint32_t anim_id, int block, bool disable_blend);

// ---- creature registry: every cCreatureAnimal that ran Update since the last call ----
const std::vector<void*>& creatures_updated();   // swap per frame (call once per frame)
using UpdateFilter = bool (*)(void* creature);   // return false to skip the creature's Update
void set_update_filter(UpdateFilter fn);
bool is_destroyed(void* c);

// ---- species / herds ----
struct Key {
    uint32_t instance, type, group;
};
Key species_key(void* c);
void* species_by_key(const Key& k);
void* create_herd(const net::Vec3& pos, void* species);
void* own_herd_for(void* species); // an existing herd like create_herd's of this species, or null
void* herd_of(void* c);
void herd_quiet(void* herd); // target size 0, disabled: the game spawns no members into it
void herd_home(void* herd, const net::Vec3& pos); // a nestless herd's home follows its puppet
constexpr uint32_t kHerdNoun = 0x01BE418E;      // cHerd (runtime: nest herds of the creature stage)
inline void* herd_species(void* herd) { return *(void**)((uint8_t*)herd + 0xA4); } // cSpeciesProfile*
inline void* herd_nest(void* herd) { return *(void**)((uint8_t*)herd + 0x160); }    // cNest* (mpNest)
uint32_t political_id(void* c);

// ---- combat ----
// filter for cCreatureAnimal::TakeDamage: return true to let the game apply it
// return false to drop the hit; the filter may change the damage
using DamageFilter = bool (*)(void* creature, float& damage, int type, void* attacker_creature);
void set_damage_filter(DamageFilter fn);
void apply_damage(void* c, float damage, int type, void* attacker_creature); // bypasses the filter
void hit(void* c, float damage, int type, void* attacker_creature);          // goes through the filter
void set_health(void* c, float hp);
// A hatchling (age +0xB34 == 0) whose behaviour tree waits for a family member of its nest to greet it
// (tree 0x9413EDEB): after a respawn or a hatch the greeting NPC makes it grow up (docs/re/creature.md 13).
constexpr uint32_t kHatchlingTree = 0x9413EDEBu;
bool hatchling_waiting(void* c);
// what the greeting does at its end: the creature grows up (0xC04F30(1)), the pack too (0xD52DF0(1))
bool grow_up(void* c);
// impressed by a social game (+0xB58 bit 0x80, set by the social success 0xD9BC40; docs/re/creature.md 8)
inline bool impressed(void* c) { return (*(uint32_t*)((uint8_t*)c + kFlagsOff) & 0x80) != 0; }
// a member of the player's pack (+0xB58 bit 0x100, set by AddPosseMember 0xB23430; docs/re/creature.md 11)
inline bool pack_member(void* c) { return (*(uint32_t*)((uint8_t*)c + kFlagsOff) & 0x100) != 0; }
inline void set_impressed(void* c, bool on)
{
    uint32_t& f = *(uint32_t*)((uint8_t*)c + kFlagsOff);
    f = on ? (f | 0x80) : (f & ~0x80u);
}

// ---- time of day: orientation of the sky object (see docs/05-re-notes.md) ----
bool time_of_day(net::Quat& q);
void set_time_of_day(const net::Quat& q);

// ---- DNA (cCreatureGameData evolution points, a global) ----
float dna();
void dna_add_raw(float delta);              // original AddEvolutionPoints, not reported
using DnaListener = bool (*)(float delta);  // return false to swallow a gain
void set_dna_listener(DnaListener fn);

// ---- any game object (cGameData) ----
// cGameNounManager::mNouns (+0x78) lists every cGameData (list node at object+0xC)
void* noun_manager();
void for_each_object(bool (*fn)(void* obj, uint32_t noun, void* user), void* user);
uint32_t noun_id(void* obj);                    // ISimulatorSerializable::GetNounID (vt 0x20)
void* cast(void* obj, uint32_t type);           // Object::Cast (vt 0x0C)
bool object_position(void* obj, net::Vec3& out); // via cSpatialObject (TYPE 0x1186577), position +4
bool object_alive(void* obj);                    // cGameData::mbIsDestroyed (+0x21) not set
// cGameData::mID (+0x24): `++[counter]` in cGameNounManager::CreateInstance (GOG 2024 0xB20BF0,
// counter 0x167BE28): unique among the objects CreateInstance made in this process. The pointer
// must be a cGameData base (offset 0) that ufo_listed / object_alive accepted in this frame;
// 0 = no object. A pointer whose serial equals one recorded earlier is the same object. Not
// across machines (the counter is per process) and not across a load: the counter's address is
// in two static descriptor tables (0x131AD5A, 0x131B076, a serializer's field list?), so a load
// may set it back; callers that keep an identity across frames also compare
// spaceext::world_epoch() (docs/re/space.md#space-object-serial).
uint32_t object_serial(void* obj);
// a cGameDataUFO pointer is still a live UFO noun (the game may have freed it: never read it before)
bool ufo_listed(void* u);
// UFO puppets/mirrors found freed by the game (tests: coop.status "freed_by_game")
extern uint32_t g_ufos_freed;
// nearest object of `noun` within max_dist of pos (nullptr if none)
void* find_object(uint32_t noun, const net::Vec3& pos, float max_dist);

// The NPC AI (NPCTickAI 0xC09570 and the behaviour tree it ticks) notices, hunts, attacks and flees
// only the creature at cGameNounManager +0x54 (mpAvatar): a partner's puppet was never a target
// (docs/re/creature.md 14). The picker returns the creature an NPC should treat as the player this
// tick (a puppet), or null for the avatar; NPCTickAI then runs with mpAvatar swapped to it.
// During the swap avatar() still returns the real avatar.
using AiTargetPicker = void* (*)(void* npc);
void set_ai_target_picker(AiTargetPicker fn);
uint32_t ai_swaps(); // NPC ticks run with a puppet as the player (diagnostics)
void swap_guard_tick();          // once per frame: undo an NPC tick's avatar swap a fault left open (BUG-0110)
void set_swap_guard(bool on);    // tests (off: no undo)
void debug_swap_fault();         // tests: the next swapped NPC tick returns without its restore
uint32_t swaps_restored();
// BUG-0110, the other detours (cell avatar index, selection flag, blank +0xE84, civ capture
// attribution): a change made around a call into the game is opened with its undo before the call
// and closed after it; swap_guard_tick undoes what a fault left open. close(mark, own): own = the
// entries the caller opened from mark on (it restores them itself).
size_t swap_open(void (*undo)(void* p, uintptr_t v), void* p, uintptr_t v, const char* what);
size_t swap_depth();
void swap_close(size_t mark, size_t own = 1);
void swap_undo_u32(void* p, uintptr_t v);    // *(uint32_t*)p = v
void swap_undo_u8(void* p, uintptr_t v);     // *(uint8_t*)p = v
void swap_undo_or32(void* p, uintptr_t v);   // *(uint32_t*)p |= v
void swap_undo_ptr_if(void* p, uintptr_t v); // *(void**)p = nullptr when it is still (void*)v
void debug_swap_ledger_fault();              // tests: the next close is skipped, as after an unwind
uint32_t swap_ledger_restored();

// The death of a creature: its behaviour tree's death node (0xD85B80) calls the creature's Die
// (vt 0x8C, 0xC08210: mbDead +0xB5E, stops locomotion and effects) and plays the death animation.
// A puppet / proxy has no AI: a lethal hit left it standing with 0 health. Does both (once).
void play_death(void* c);

// AI suppression for puppets: NPCTickAI is skipped for registered creatures
void puppet_register(void* c);
void puppet_unregister(void* c);
bool is_puppet(void* c);
const std::vector<void*>& puppets(); // puppets and NPC proxies (creatures), UFO puppets in space

// `c` is still a live cCreatureAnimal of the world (compares pointers only: safe on a freed one)
bool creature_listed(void* c);
// diagnostics (coop.npc herds): every herd, and the creatures of herds without a nest
std::string herds_dump();

} // namespace game
