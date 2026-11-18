// Decision 5X B (alpha): the galaxy menu shows the galaxy server's planets while connected (galaxy_menu.cpp).
#pragma once

#include <string>

#include "sporekit.h"

namespace galaxy_menu {

void init(const SkHost* host, SkMod* mod);
void tick(double now);
std::string command(int argc, const char* const* argv); // coop.gmenu [status | on | off | rebuild]

} // namespace galaxy_menu
