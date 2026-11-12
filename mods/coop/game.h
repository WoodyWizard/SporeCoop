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
constexpr uintptr_t kLocoModeOff = 0x2B0; // BUG-0409: the locomotion mode (0 normal, 3 thrown by a hit: cCreatureBase TakeDamage 0xC1A520, 4 abducted)
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
// the noun manager's avatar field in the creature stage / adventures, without the BUG-0264 checks:
// only to compare pointers (the save filter runs during the quit's own save), never to dereference
void* avatar_field();
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
void* spawn_creature(const net::Vec3& pos, void* species, void* herd, bool reuse = false); // reuse: Create's pool flag (tests)
// cGameNounManager::DestroyInstance; a creature (cCreatureAnimal) is kept until the running
// cinematic is over (it may be one of its actors), destroy_deferred_tick() removes it then
void destroy_creature(void* c);
void destroy_untracked(void* c); // tests (BUG-0507): the game's destroy without the mod's bookkeeping
void destroy_instance(void* obj); // cGameNounManager::DestroyInstance of a game object that is no creature (a fruit, BUG-0584)
void destroy_deferred_tick(); // once per frame
void set_destroy_defer(bool on); // tests (off: at once)
void set_defer_keeps_puppet(bool on); // tests (off: a deferred puppet leaves the puppet list at once, BUG-0066)
bool cinematic_playing(); // cCinematicManager +0x2C != 0 (W2: no destroy of a village meanwhile)
bool destroy_pending(void* c);     // queued by destroy_creature, still there
std::string destroy_deferred_status();
// BUG-0614 (game.finite_pose): a received pose with NaN / Inf; counted, false with the switch off
// BUG-0434 (creature.culled_ground): this frame's target of a puppet, lifted to after the physics update when the game grounds it
void mound_hold(void* c, const net::Vec3& tgt);
void after_physics();
bool pose_bad(const net::Vec3& pos, const net::Quat& rot, const net::Vec3& vel);
void teleport(void* c, const net::Vec3& pos, const net::Quat& rot);
// BUG-0360: teleport without the teleport mark (+0xB5D, set by vt 0x44 0xC0C040): the locomotion takes
// the position, the animation's walk gait goes on (with the mark it restarts every frame)
void place(void* c, const net::Vec3& pos, const net::Quat& rot);
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
// the surface on pos's radial line as the game's snap finds it (cPlanetModel_SurfacePoint: the terrain or a collision
// object over it, a nest mound, a rock; only where the collision is loaded, near this game's camera); 0: none
float surface_radius(const net::Vec3& pos);
void set_scale(void* c, float s);
// BUG-0360 (coop.track): the creature's Anim::AnimatedCreature (cCreatureBase +0xB54): where its
// model is drawn (+0x04) and the id of its current animation (vt 0x58 GetCurrentAnimation, 0 when
// none). False when the creature has none.
bool drawn_state(void* c, net::Vec3* drawn, uint32_t* anim);
// BUG-0360: the walk state the creature's idle animation is chosen by (0xC0C790): the animation's
// gait state (AnimatedCreature +0x17C -> +0x15B8, two ints: 0 = no gait), the creature's +0x137 and
// its speed state +0xF98. False when it has no animation.
bool gait_state(void* c, int* gait, int* gait2, int* moving, int* speed_state);
// BUG-0376: +0x137, 1 while the creature is grounded on the terrain, 0 while it falls or stands on an object
bool grounded(void* c);
void set_grounded(void* c, bool on);
// BUG-0532: puppet c is held in the air at pos (modes.cpp, every frame while the partner is airborne): put back there
// and marked not grounded right after an animation start or an update of the game that grounded it
void air_hold(void* c, bool on, const net::Vec3& pos = {}, const net::Quat& rot = {}, const net::Vec3& vel = {});
void air_release_all();
std::string air_status();

// Animation events: called for every PlayAnimation/PlayAnimationTo on any creature.
using AnimListener = void (*)(void* creature, uint32_t anim_id, int block, bool disable_blend, bool to_target);
void set_anim_listener(AnimListener fn);
void play_animation(void* c, uint32_t anim_id, int block, bool disable_blend);
// BUG-0464: animations started on a creature outside its idle refresh 0xC148B0 (the idle / walk choice every
// game makes by itself): how 0 SporeCoop::CreaturePlayAnim (thiscall(creature, anim, int a, int b) ret 0xC, the
// behaviour tree's), 1 cCreatureBase::PlayAnimation (a = disable blend, b = block), 2 PlayAnimationTo (b = block).
// Called after the play, not for plays of the mod (play_animation, play_behaviour_anim).
using BehaviourAnimListener = void (*)(void* creature, uint32_t anim_id, int how, int a, int b);
void set_behaviour_anim_listener(BehaviourAnimListener fn);
// BUG-0936: the idle refresh's own plays (Creature_IdleRefresh's choice: idle, walk, the BT's idle override), which the
// behaviour listener leaves out; tribe.cpp notes them per member (the host) to tell its idle from a behaviour animation
using IdleAnimListener = void (*)(void* creature, uint32_t anim_id);
void set_idle_anim_listener(IdleAnimListener fn);
// BUG-0936: the game's idle refresh on c (force: also when the choice is the current animation), as its update does
void idle_refresh(void* c, bool force);
bool play_behaviour_anim(void* c, uint32_t anim_id, int how, int a, int b); // how 2 is played as 1
void behaviour_anim_counts(uint32_t* seen, uint32_t* idle);

// Visual effects of a creature (BUG-0451): every effect the game makes on or at a creature goes
// through its two effect pools (cCreatureBase +0xCC8 pool 1, +0xD68 pool 2); these are the only
// creators and the one stop (docs/re/creature.md#creature-effects-relay).
struct EffectEvent {
    uint32_t id = 0;    // the effect id (FNV hash of its name)
    uint32_t key = 0;   // the key it is kept under in the pool (the effect id unless the game renames it)
    uint8_t pool = 0;   // 1 or 2 for a start, 0 for a stop
    bool stop = false;
};
using EffectListener = void (*)(void* creature, const EffectEvent& ev);
void set_effect_listener(EffectListener fn);
// A visual effect created for an arbitrary object (not only a creature): effect.cpp reads its
// position (effect +0x9C, where the game's transform setter put it) in its tick. The listener runs
// only on the game's main thread; the hooks call the original and return on every other thread.
using WorldEffectListener = void (*)(void* effect, uint32_t id);
void set_world_effect_listener(WorldEffectListener fn);
// BUG-0603: an attack animation's per-target hit impact (an animation event effect pointed at its target's hit
// spot) made by this game: the effect id and the world position (main thread only)
using HitEffectListener = void (*)(uint32_t id, const net::Vec3& pos);
void set_hit_effect_listener(HitEffectListener fn);
uint32_t hit_effects_seen();
void hit_effect_calls(uint32_t& calls, uint32_t& last_other_ret, uint32_t& other_fx); // diagnostics
// the stage logic's gate for the world effect hooks (installed once in init); no work outside a
// played stage. Never installs or removes a hook: a hook removed while the UI skinner's render
// thread was inside it faulted (BUG-0451 review).
void set_world_relay(bool on);
// effects.cpp's switch: false = the hooks only call the original (the frame cost measured in
// out/REPORT.md); also notes the main thread id (called from effects::tick, main thread)
void set_effect_relay_enabled(bool on);
// the receiver: create an effect by id in the game's effect world, set the same transform, start it
void* create_world_effect(uint32_t id, const void* xform);
// a test / perf workload: the same, but our hooks see it as a game effect (the relay sends it)
void* create_local_effect(uint32_t id, const void* xform);
// what effects::tick reads about the effects the hooks recorded this frame
bool effect_position(void* effect, net::Vec3& out);
bool effect_relayed(void* effect); // a creature hook already sent it
// created in another effect world than the game's own (cEffectsManager +0x244): the skin painter's
bool effect_foreign(void* effect);
// the last effect id the game created (a valid id in this stage); main_world_only: in the game's own
// effect world (not the skin painter's, whose effects fault anywhere else)
uint32_t last_effect_id(bool main_world_only);
uint32_t last_foreign_effect_id();
uint32_t foreign_effects();
// a bone index inside the creature model's bone array (the range cCreatureBase::CreateEffect indexes)
bool creature_bone_ok(void* creature, int bone);
void clear_effect_table();         // after effects::tick (once a frame, main thread)
void effect_table_stats(uint32_t& creates, uint32_t& probes, uint32_t& evicted, uint32_t& mgr, uint32_t& world,
                        uint32_t& anim);
// our own work in the create hooks (the original game call is not counted)
void effect_hook_cost(double& sum, double& max, uint64_t& calls);
void set_effect_log(bool on); // support logging of every effect the game creates (coop.effects log on)
// the game's own pool calls, for a relayed effect (start: the same create+start as StartOrStop...)
void* start_effect(void* c, const EffectEvent& ev);
// the game's one-shot creature effect (pool 3 in EffectEvent: key = the bone index)
void* create_creature_effect(void* c, int bone, uint32_t effect_id);
bool stop_effect(void* c, uint32_t key);
// the game's state: the effect object of `key` in the creature's pools, and whether it is active (vt 0x10)
void* effect_from_pools(void* c, uint32_t key);
bool effect_active(void* effect);
// stop a world effect the relay created (IVisualEffect vt 0xC with 0); false if the object is not callable
bool stop_world_effect(void* effect);
// drop the reference the relay holds on a created world effect (IVisualEffect vt 0x4, Release)
bool release_world_effect(void* effect);
// the tribe's "+N / -N food" over its food mat (Simulator::cTribe::UpdateFoodVisuals); false when the game would
// fault: the tribe's food mat (+0x260) is gone once the City Hall stands at the end of Tribe (BUG-0585)
bool tribe_food_visuals(void* tribe, float change);

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
// the cSpeciesManager's map slot that holds the profile of this key (its node value), or null: no node yet
// (docs/re/creations.md#creations-species-map, BUG-0200)
void** species_slot(const Key& k);
void* create_herd(const net::Vec3& pos, void* species);
void* own_herd_for(void* species); // an existing herd like create_herd's of this species, or null
void* herd_of(void* c);
// tests (BUG-0790): a hit through the hook with any combatant as the attacker (a UFO's +0x508, a vehicle's)
void hit_by_combatant(void* c, float dmg, int type, void* attacker_combatant, uint32_t pol);
bool attacker_owner_fix();
net::Vec3 finite_vel(const net::Vec3& v); // BUG-0894 (game.finite_vel): a non-finite snapshot velocity -> 0
uint32_t vel_zeroed(); // BUG-0790 (creature.attacker_owner): the hit's attacker is its object, maybe not a creature
bool nest_position(void* c, net::Vec3& out); // the nest of the creature's herd (false: no herd or no nest)
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
// the same with a push (knockback impulse, the 4th TakeDamage argument; tests: a charge's throw, BUG-0383)
void hit_push(void* c, float damage, int type, void* attacker_creature, const net::Vec3& push);
void set_health(void* c, float hp);
std::string damage_log(bool clear); // the last creature hits through TakeDamage (coop.hp dmglog)
// BUG-0463 sneak (docs/re/creature.md#creature-sneak): +0xBB1 is 1 while the creature sneaks, +0xB80 its
// noise radius (0 while sneaking, what the NPCs' hearing check reads), +0xB7C the model's alpha the
// creature's update eases to 0.2 while sneaking (written to the model +0x80)
bool sneaking(void* c);
float noise_radius(void* c);
float sneak_alpha(void* c);
// the game's own setter CreatureSetSneaking 0xC1B030 (c, on, force 1: no ability check); false: no symbol
bool set_sneaking(void* c, bool on);
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
void dna_add_raw(float delta);              // a shared change, not reported: a gain through the original
                                            // AddEvolutionPoints (DNA + the evolution bar), a spend the DNA only (BUG-0630)
void dna_set_raw(float total);              // SetEvolutionPoints: the DNA only, as the game's editor spends it
float evolution_bar();                      // cPlayer +0x10F0, the stage's total earned (NAN without a player)
using DnaListener = bool (*)(float& delta); // return false to swallow a gain; may change it (BUG-0510 pace)
void set_dna_listener(DnaListener fn);

// ---- any game object (cGameData) ----
// cGameNounManager::mNouns (+0x78) lists every cGameData (list node at object+0xC)
void* noun_manager();
void for_each_object(bool (*fn)(void* obj, uint32_t noun, void* user), void* user);
// BUG-0386 / BUG-0440 (diagnostics): "noun@dist" of every placed object within r m of pos but `self`, nearest first
std::string objects_near(const net::Vec3& pos, float r, void* self, int max_n = 8);
uint32_t noun_id(void* obj);                    // ISimulatorSerializable::GetNounID (vt 0x20)
void* cast(void* obj, uint32_t type);           // Object::Cast (vt 0x0C)
bool object_position(void* obj, net::Vec3& out); // via cSpatialObject (TYPE 0x1186577), position +4
float object_scale(void* obj);                   // cSpatialObject::GetScale (vt 0x34), -1 without one
void set_object_scale(void* obj, float s);         // cSpatialObject::SetScale (vt 0x40), nothing without one
bool set_object_position(void* obj, const net::Vec3& pos); // cSpatialObject::SetPosition (vt 0x38), false without one
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
bool lend_ledger_on();           // BUG-0882 (game.lend_ledger): lent state in the ledger, call guards reset per frame
uint32_t call_guards_reset();
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

// ---- the liveness of a stored pointer (BUG-0426 class, package L1 of 0.7.13) ----
// A pointer kept from an earlier frame (or from before a game call) may name an object the game freed since, or a new
// object the game built at the same address. Its own memory says nothing (mbIsDestroyed +0x21 of a freed block can
// read 0 again, BUG-0842 / BUG-0843 / BUG-0844). The truth is the game's object list (cGameNounManager +0x78) and the
// object's serial (cGameData::mID +0x24, ++ in CreateInstance only, docs/re/space.md#space-object-serial).
// listed(): one walk of the list per frame (on the first question of the frame), kept exact inside the frame by
// gamefixes' DestroyInstance hook (an object destroyed now leaves the set) and by the serial counter (a miss after a
// CreateInstance of this frame walks the list again). Compares pointers only: safe on a freed one.
bool listed(const void* obj, uint32_t noun = 0); // in the list now (noun 0: any noun) and not destroyed
struct LiveRef {
    void* p = nullptr;
    uint32_t serial = 0; // its cGameData::mID when it was taken
    uint32_t noun = 0;
    explicit operator bool() const { return p != nullptr; }
};
LiveRef live_ref(void* obj, uint32_t noun = 0); // {} unless obj is listed (with that noun) now
bool alive(const LiveRef& r);                    // r.p is listed with r.noun now and is the same object (serial)
inline void* live(const LiveRef& r) { return alive(r) ? r.p : nullptr; }
void note_destroyed(void* obj); // gamefixes' DestroyInstance hook, before the game destroys obj
// the mod's frame tick (coop.cpp on_frame): listed() keeps its walk only inside it; elsewhere (our hooks inside the
// game's update, where the game may free objects without DestroyInstance) every question walks the list
struct LiveScope {
    LiveScope();
    ~LiveScope();
};
void note_created();            // the mod made an object: the next miss walks the list again
std::string live_status();      // coop.live (tests)
bool listed_exact();            // listed() is kept exact inside a frame (its fix on, the destroy hook in)
// diagnostics (coop.npc herds): every herd, and the creatures of herds without a nest
std::string herds_dump();
std::string herd_nulls_status(); // BUG-0256: null herd members taken out before Herd_Update

} // namespace game
