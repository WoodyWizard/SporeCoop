// BUG-9285 (step 4 part 4, docs/12-server-and-players.md#server-nest-refresh, decision 6Z): on a galaxy planet an
// extinct species comes back a set time after it died out (the galaxy server's setting, 3 hours by default), so a
// persistent planet hunted by many players never empties. The planet's host notes when each species went extinct (by
// the server's clock, the same for every host) in <save>.spo.duo-extinct next to the planet (it travels with the world
// on the server, so a hand-over keeps it) and, when the time is up, undoes the extinction the game's way: the herds'
// mbExtinction (+0x190) and their nests' mark (+0x120) cleared, the species' relation record set back to "not set";
// the game's own respawn fills the herds again. Guests follow (worldobj.cpp herd kind). Players' own species are not
// touched (their nests have their own rule). Switch `creature.nest_refresh`.
#pragma once

#include <cstdint>
#include <string>

#include "net/session.h"

struct SkHost;
struct SkMod;

namespace nest_refresh {

void init(const SkHost* host, SkMod* mod);
void tick(const net::Session& s, double now); // host
bool revive_on();                              // the switch (worldobj: guests follow a revived herd)
std::string status();

} // namespace nest_refresh
