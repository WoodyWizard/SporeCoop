#pragma once

#include "sporekit.h"

// Depth handle for creature parts in the creature editors (roadmap 42, BUG-9103):
// docs/re/creations.md#creations-editor-depth-handle
namespace editor_depth {

bool init(const SkHost* host, SkMod* mod);
void tick();
void draw(); // SK_EV_OVERLAY

} // namespace editor_depth
