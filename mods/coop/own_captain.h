// BUG-1475 (#217 a, WP-K1 of docs/plan-0.7.17.md): a guest's own captain on its own game in Space (own_captain.cpp;
// the rules captain::own_step in captain_logic.h; docs/re/space.md#space-own-captain). Switch space.own_captain.
#pragma once

#include <string>

#include "net/session.h"
#include "sporekit.h"

namespace own_captain {

void init(const SkHost* host, SkMod* mod, net::Session* sess);
void tick(double now); // every frame, main thread (it acts twice a second)
std::string status();

} // namespace own_captain
