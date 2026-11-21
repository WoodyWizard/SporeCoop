#pragma once
// A partner's own launch at the Civ -> Space change (BUG-9295, after Lineages PR #196's per-guest launch, BUG-9071):
// civ_launch.cpp, docs/re/civ.md#civ-guest-launch.

#include <cstdint>
#include <string>

namespace civ::launch {

// guest: the host's vote with an editor ended (session_glue.cpp); the spaceship editor's vote arms the own launch of a
// guest that has its own nation
void on_vote_end(uint32_t editor);
// guest, every frame (world::guest_tick): the planet-won state of its game, the hold's end
void tick();
// guest: a received world waits while its own launch runs (its History, its ship editor, its own first flight)
bool holds_load();
void reset(); // session end
std::string status();

} // namespace civ::launch
