// BUG-0332: the planet terrain's stamps (terrain_diag.cpp, docs/re/tribe.md#tribe-terrain-stamps)
#pragma once
#include <string>

#include "sporekit.h"

namespace terrain_diag {
void init(const SkHost* host, SkMod* mod);
std::string stats();
} // namespace terrain_diag
