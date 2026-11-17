// Roadmap phase 2 (BUG-9135, owner decisions 2, 3, 6, 7, 8 of docs/ROADMAP.md): every player's own nest.
//
// Host: each party seat that plays its own species gets a nest herd of that species (the game's own CreateHerd with
// its nest) 40-80 m from the host's home nest, with the host's nest's brain range, best friends with the party.
// The game fills, respawns and saves it like any nest; the seat's creature is archived with the save (an offline
// player's nest loads, the AI runs it, it can die out; it is made again when the player is back). When the host's
// species migrates, a seat's nest far from the new home is made again next to it (the old one stays, as the game
// leaves the player's own old nests). Each partner is told its own nest as its home (nest.cpp MSG_NEST kind 2): its
// game joins its copy of that nest herd and hatches and respawns there. A nest made while its player is online sends
// them the world again (their copy must hold the nest; an empty nest made on their side faulted the game's claim).
// A seat playing the host's species shares the host's nest.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/protocol.h"
#include "net/session.h"
#include "sporekit.h"

namespace home_nests {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> notice; // shown to every player
    std::function<uint32_t()> world;                // the planet's id (worlds.ini notes: each seat's nest, BUG-9173)
};

void init(const SkHost* host, SkMod* mod, const Ctx& ctx);
void host_tick(double now);
// host: where `player` hatches and respawns: its seat's own nest, else the host's (false: no home nest known)
bool home_for(uint8_t player, net::Vec3& out);
// host (decision 5CD phase 4): `player`'s own nest herd (its family), null when it has none here (the host's species)
void* family_herd(uint8_t player);
bool enabled();
void on_world_change();
std::string status();
std::string debug(const std::vector<std::string>& args);

} // namespace home_nests
