// BUG-9287 (step 4 part 5, docs/12-server-and-players.md#server-era-visitors-built): the game's side of the galaxy
// server's creation store (net/galaxy.h CRTPUT .. CRTPART). A player puts its own creature there (the mod's species
// bundle, creations::species_bundle); a game that has to show a creature it does not have (a visitor of another era)
// asks for it and installs it (creations::species_from_bundle). Switch: `galaxy.era_visitors` (era_visitors.cpp) uses it.
#pragma once

#include <cstdint>
#include <string>

#include "sporekit.h"

namespace galaxy_crt {

void init(const SkHost* host, SkMod* mod);
void tick(double now);
// this player's creature: into the store (once per key and connection; again until the server says it has it)
void put_own(uint32_t instance, uint32_t type, uint32_t group);
// a creature this game does not have: asked from the store (again now and then). false: asked recently, nothing new
bool want(uint32_t instance, uint32_t type, uint32_t group);
std::string status();

} // namespace galaxy_crt
