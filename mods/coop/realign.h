// Whole-planet re-alignment after transitions (W2 of 0.7.7: BUG-0564, BUG-0566, BUG-0567, classes BUG-0421 /
// BUG-0427; docs/re/tribe.md#tribe-realign).
//
// The host sends its whole planet's list of the world objects that matter (villages with their player flag,
// huts and tools with their health, food mats, nests, cities) every 2 s and at once after a transition of
// its own (its stage epoch: a load, the game over's reload of the last save) or of a guest's (it is in our
// place again: a join, a rejoin, an editor return). A guest compares the list with its own game's objects
// (realign_rule.h): an object of its own the host has not goes through the game's own removal, a state that
// differs gets the host's; what it lacks is counted (the stage's own path makes it: tribe.npc_tribes).
// The guest re-aligns in place: a load of the host's world would cost the guest a loading screen at every
// host game over and its game's own changes since, and would not cover a rejoin or an editor return.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "net/session.h"
#include "sporekit.h"

namespace realign {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<bool(uint8_t id)> in_place; // host: guest `id` plays our world
    std::function<bool()> host_away;          // guest: its own game runs its world
};

void init(const SkHost* host, SkMod* mod, const Ctx& ctx);
void tick(double now);                    // every frame
void on_planet(uint8_t from, net::Reader& r); // MSG_WOBJ sub-kind 2 (worldobj.cpp passes it on)
void on_session_end();
std::string status();

} // namespace realign
