#pragma once

#include <string>

#include "sporekit.h"

namespace fov {

// notify shows a local message (the change of the field of view)
bool init(const SkHost* host, SkMod* mod, bool (*input_allowed)(), void (*notify)(const std::string&));
void shutdown();

// BUG-9156: fn (null: none) gives the creature the follow camera follows this frame instead of the avatar (null: the avatar)
void set_camera_target(void* (*fn)());
} // namespace fov
