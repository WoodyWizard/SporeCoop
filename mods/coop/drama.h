// The game's random world events (Drama Manager: the visiting UFO, meteors, the sea monster, ...)
// in a session: a guest's own game does not start the ones that belong to the host's world
// (BUG-0486, BUG-0465; docs/re/creature.md#creature-drama-events).
#pragma once

#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace drama {

void init(const SkHost* host, SkMod* mod, const net::Session* sess);
// coop.drama [start <kind> | mask <hex>]: the events seen, started and refused
std::string debug(const std::vector<std::string>& args);

} // namespace drama
