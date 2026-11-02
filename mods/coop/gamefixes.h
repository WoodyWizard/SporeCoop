// Workarounds for crashes of the game itself that co-op makes more likely (docs/stability-report.md).
#pragma once

#include <string>

#include "sporekit.h"

namespace gamefixes {

void init(const SkHost* host, SkMod* mod);
void shutdown(); // puts the original bytes back (hot reload)
std::string status();
// tests (BUG-0270): the window manager `wm` gets a destroyed object as its modal window
std::string fake_dead_modal(void* wm);
// tests (BUG-0310): the next citizen checked outside the Tribe mode gets +0xB54 = 0 (a cleaned-up one)
std::string arm_leave_walk();
// tests (BUG-0310): once armed, puts `citizen` through the guard in the first frame Tribe is left
bool leave_walk_tick(void* citizen);
bool leave_walk_armed(); // the stand-in is armed (the frame loop asks before walking the members)
// tests (BUG-0449): the next physics update gets a live body of kind 0 that no owner refers to and without an owner
std::string arm_physics_orphan();

} // namespace gamefixes
