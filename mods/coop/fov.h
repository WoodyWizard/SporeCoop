// Wider third-person FOV in Creature and Galactic Adventures (BUG-0866).
// Local only (no network traffic). Hooks App::cViewer::BeginUpdate, EndUpdate, and GetCameraToPoint.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "sporekit.h"

namespace fov {

void init(const SkHost* host, SkMod* mod, std::function<void(const std::string&)> feed);
void tick(uint32_t mode);
void shutdown();

int step();            // current FOV step 0..6 (+3 deg per step)
int set_step(int s);   // set and clamp step 0..6; returns applied step
int cycle_step();      // UI helper: 0 -> 1 -> ... -> 6 -> 0
bool active_now();     // true when Creature/Adventure follow camera is widened
int command(void*, int argc, const char* const* argv, SkOut* out);

} // namespace fov
