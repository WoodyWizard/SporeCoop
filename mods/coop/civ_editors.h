// WP-C3 of docs/plan-0.7.16.md (BUG-1275): a guest's own Civ editors in own mode (docs/re/civ.md#civ-own-editors).
//
// The civilization is one and its culture set the host's (BUG-1157). Before 0.7.16 a guest's design editors and its
// Civ outfitter in the host's world were refused (BUG-1158) and its own Sporepedia pick went back to the host's design.
// With `civ.guest_editors` (creature.mode own, the species line on) a guest's building and vehicle editors, its Civ
// outfitter and its planner picks open as usual; their results go to the guest's species line (species::set_design,
// species::set_civ_outfit) and never into the game's civilization: the parts of the game's return that write the
// shared civilization (the planner's design apply 0xD095A0: Civ_SetDesign + cPlayer +0x1150; the Civ outfitter's
// result in cGameModeCiv: the player species, every civilization's species key, every tribe's slot) are skipped for
// it. Every game then shows the guest's objects in its designs (civ_own_designs.cpp) and its citizens of its Civ
// species (civ_species.cpp). The host and creature.mode host keep the game's own behaviour.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace civedit {

// a guest in own mode in the host's Civ world with the switch on: its editors and picks go to its line
bool active();
// designs::refuse_own_editor: this Civ editor request of a guest opens (its result goes to its line)
bool allows(uint32_t editor);
// designs::civ_sync: the guest's own culture set entry `id` changed to `key` by a path none of the hooks saw (it goes
// back to the host's there): take it as the guest's own design. false: not taken (not active, no creation)
bool take_pick(uint32_t id, const uint32_t key[3]);
// WP-X1 (BUG-1276, civ_arrival.cpp): the result of the guest's own City Hall / vehicle editor at its arrival in Civ
// (design id `id`, the creation `key`) goes to its line like a planner editor's. false: not taken (not own mode, no
// creation, not a building or vehicle)
bool take_editor(uint32_t id, const uint32_t key[3]);
// BUG-1298 (civ_arrival.cpp): our own editor request was just submitted outside the planner's path (its return stays
// ours if the session ends before it)
void own_opened();
// BUG-1308: our own Civ outfitter (its request starts from our creature) is on its way or open: the editor's part-list
// error of our creation's Creature Creator parts is waived (vote.cpp)
bool own_outfit_open();

void init(const SkHost* host, SkMod* mod, net::Session* sess);
void tick(double now); // every frame, main thread (after species::tick)
std::string status();

} // namespace civedit
