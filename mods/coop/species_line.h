// The species line (WP-0 of docs/plan-0.7.16.md, BUG-1270): every player's own species per stage and its own Civ
// designs, kept by the mod for every game. The API the per-stage packages read (Tribe members, Civ citizens,
// vehicles, buildings, the guest's own editors, the stage transitions). docs/01-architecture.md#arch-species-line;
// the rules, the wire and the store row are species_line_core.h (portable, tests/species_tests.cpp).
//
// What a key is: a creation key THIS game can build now (its records are registered here), and the same key on every
// game of the session (the owner's game too): the creature is creations' shared copy (creations::player_key, 0x7C......),
// an outfit or a design the line's shared copy (0x7B......, from the content). {} (instance 0) = none: use the world's
// own (the host's species, the tribe's slot, the civilization's species, the culture set's key). The host (player 0)
// has no line: its species is the world's own, so every call for player 0 answers {}.
//
// Keys of ours never go into a game save: a package that writes one into a game object strips it while saving
// (savestrip.h; the owner decision of 2026-11-18: the host's save holds the host's species and designs only).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "creations.h"
#include "net/session.h"
#include "species_line_core.h"
#include "sporekit.h"

namespace species {

using Key = creations::Key;

// creature.mode own (the session setting, F5): the lines count. In host mode every guest plays the host's species and
// every key() / design() answers {} (the rows are kept: back to own, the lines come back)
bool own_mode();

// the key of player pid's species in stage s on this game (see above); {} = the world's own
Key key(uint8_t pid, Stage s);
// player pid's own Civ design for the game's design id (cCivilization +0x6C: City Hall 0x99E92F05, house 0x4E3F7777,
// ..., a vehicle's VehicleDesignId(purpose, locomotion) 0xC9E850); {} = the culture set's key
Key design(uint8_t pid, uint32_t design_id);
// the design ids player pid has a design for on this game
std::vector<uint32_t> design_ids(uint8_t pid);

// this game's own line, on the owner's side (a guest's own editors start from it): the key of its own creation of the
// stage on its own game ({} = the world's own); design: the own key of that design
Key own_key(Stage s);
Key own_design(uint32_t design_id);

// The owner's setters (a guest, own mode, in a session): `k` is a creation this game has (its My Creations: the editor
// result); {} clears it. The tribe outfit is taken as made for the current creature, the Civ outfit for the current
// tribe line. The records go to the host and from there to every game; the key() of this player follows on each game
// when its records are registered there. false: refused (the host, host mode, no session, a key without records here)
bool set_tribe_outfit(const Key& k);
bool set_civ_outfit(const Key& k);
bool set_design(uint32_t design_id, const Key& k);

// BUG-1298 (WP-S2b, an own editor's result kept over a disconnect): own_ready: a guest in own mode whose host has
// answered with its stored row in this session (a set before that answer would make the stored row be ignored);
// host_took: the host's row of this guest (as the host broadcast it) names the own creation `own` (the owner-side
// instance): the host has it, the result is safe; own_waiting: a guest (line on) still without that answer
bool own_ready();
bool own_waiting();
bool host_took(const Key& own);

// A change of player pid's line on this game (any key() / design() answer of it: a new row, records registered, the
// creature changed, own / host mode, the player left): fn(pid), on the main thread from tick(), outside the frame's
// drawing (game calls allowed, as in the module ticks). Register from a static initializer:
//   static bool g_reg = species::on_change("tribe.own_species", [](uint8_t pid) { ... });
using ChangeFn = void (*)(uint8_t pid);
bool on_change(const char* name, ChangeFn fn);
uint32_t serial(uint8_t pid); // +1 for every change of pid's line here (a poll instead of the callback)
std::vector<uint8_t> players(); // the players with a line here (a key() or design() that is not {})

// a key of a line on this game (any player's outfit or design, a guest's creature), or one that was a line's key
// earlier in this process (a player who left, a key changed by an outfit): a save strip asks this
bool is_line_key(const Key& k);

// BUG-1315: the world's species of a stage (Tribe: the player tribe's slot, Civ: the player civilization's species),
// the key a save writes instead of a line's creature key (savestrip.h add_key_filter). A package registers its stage's
// from a static initializer: `static bool g = species::set_world_species(Stage::Tribe, fn);` ({} = not known now)
using WorldFn = Key (*)();
bool set_world_species(Stage s, WorldFn fn);

// ---- the game glue (coop.cpp, session_glue.cpp) ----
void init(const SkHost* host, SkMod* mod, net::Session* sess);
void tick(double now);                                   // every frame, main thread
void on_message(uint8_t type, uint8_t from, net::Reader& r); // MSG_SPECIES_LINE
void on_player_join(uint8_t id);                         // host: the rows and their records to the newcomer
std::string status();                                    // coop.species
std::string trace_line();                                // the support trace

} // namespace species
