// Creature editor depth handle (BUG-0871): pushes details and weapons in or out along the skin normal.
#pragma once

#include <string>
#include <vector>

#include "sporekit.h"

namespace editor_depth {

bool init(const SkHost* host, SkMod* mod);
void tick();
void draw();
void shutdown();
std::string debug(const std::vector<std::string>& args);

} // namespace editor_depth
