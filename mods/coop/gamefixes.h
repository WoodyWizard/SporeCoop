// Workarounds for crashes of the game itself that co-op makes more likely (docs/stability-report.md).
#pragma once

#include <string>

#include "sporekit.h"

namespace gamefixes {

void init(const SkHost* host, SkMod* mod);
void shutdown(); // puts the original bytes back (hot reload)
std::string status();
void frame(); // every frame: a one-time check of the save areas when the startup hook did not see the game's init
// tests (BUG-0270): the window manager `wm` gets a destroyed object as its modal window
std::string fake_dead_modal(void* wm);
// tests (BUG-0310): the next citizen checked outside the Tribe mode gets +0xB54 = 0 (a cleaned-up one)
std::string arm_leave_walk();
// tests (BUG-0310): once armed, puts `citizen` through the guard in the first frame Tribe is left
bool leave_walk_tick(void* citizen);
bool leave_walk_armed(); // the stand-in is armed (the frame loop asks before walking the members)
// tests (BUG-0449): the next physics update gets a live body of kind 0 that no owner refers to and without an owner
std::string arm_physics_orphan();
// tests (BUG-0453): the next NPC whose attack animation plays has its target cleared (SetTarget(null), as the
// game does to every NPC aimed at a creature it destroys) right before its attack node ticks
std::string arm_attack_cut();
// tests (BUG-0582): the next spot lookup of a creature attack's attacker gets no record (as for a cell without one)
std::string arm_spot_cut();
// BUG-0512: clears the stimuli of creature c whose sender is `sender` (or a sub-object of it), as the game's
// clear 0xBC9A60 does (masks, value, the sender released); returns how many
unsigned clear_stimuli_from(void* c, void* sender);

} // namespace gamefixes
