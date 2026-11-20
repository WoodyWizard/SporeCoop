// BUG-9287 / BUG-9290 (step 4 part 5, docs/12-server-and-players.md#server-era-visitors-built): the game's side of the
// galaxy server's creation store (net/galaxy.h CRTPUT .. CRTPART). A game puts creations it shows to other worlds there
// (its player's creature; a Civ era host its vehicles' and buildings' designs), kept as this player's; a game that has
// to show another player's creation asks the store for it by its owner and key and installs it under a key of its own
// (creations::creation_from_bundle): two players' creations can share a key, never a key of ours. Used by
// era_visitors.cpp (switch `galaxy.era_visitors`).
#pragma once

#include <cstdint>
#include <string>

#include "sporekit.h"

namespace galaxy_crt {

void init(const SkHost* host, SkMod* mod);
void tick(double now);
// a creation of this game into the store, as this player's (once per key and connection; again until the server has it)
void put(uint32_t instance, uint32_t type, uint32_t group);
// another player's creation this game shows: the key to build it from here. false: not here yet (asked from the store;
// try again later). A creation its owner never put (the game's own content): its own key.
bool local_key(uint64_t owner, const uint32_t key[3], uint32_t out[3]);
std::string status();

} // namespace galaxy_crt
