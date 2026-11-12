// BUG-0937: the Creature HUD portrait follows the avatar's species after the mod's body swaps (hud_portrait.cpp).
#pragma once

#include <functional>

#include "sporekit.h"

namespace portrait {

void init(const SkHost* host, SkMod* mod, std::function<bool()> creature_now);
void tick(double now); // every frame: the check is scheduled outside the frame twice a second
extern bool g_portrait_fix;

} // namespace portrait
