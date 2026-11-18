// BUG-9145: the screen axis a drag of the creature editor's depth cube moves along (editor_depth.cpp). Portable (no
// Windows, no game): tests/editor_depth_axis_tests.cpp.
//
// Before, the press measured the part's outward line with a point one world unit out (about 300 pixels at the usual
// zoom). That point was off the screen or behind the camera whenever the part faced the camera or the screen's edge,
// world_to_screen refused it, and the press went to the editor: the cube lit up but could not be dragged. Now the line
// is measured over a short step (the cube's own scale), and a line that points (almost) at the camera falls back to
// the screen's up / down: up pushes the part out.
#pragma once

#include <cmath>

namespace editor_depth_axis {

constexpr float kStep = 0.05f;   // world units along the part's line for the measure (as the cube's own size measure)
constexpr float kMinStepPx = 3.f; // a shorter projection of that step: the line looks at the camera

struct Axis {
    float sx = 0, sy = -1;      // the unit screen direction a drag along moves the part out
    float px_per_unit = 0;      // screen pixels per world unit along it
    bool fallback = false;      // the line points at the camera: up / down instead
};

// a: the part on screen; b: the point kStep out along its line on screen (b_ok false or NaN: behind the camera);
// cube_ppu: the cube's pixels per world unit last drawn (for the fallback; 300 when unknown)
inline Axis drag_axis(float ax, float ay, float bx, float by, bool b_ok, float cube_ppu, float step = kStep)
{
    Axis r;
    if (b_ok && std::isfinite(ax) && std::isfinite(ay) && std::isfinite(bx) && std::isfinite(by) && step > 0) {
        const float dx = bx - ax, dy = by - ay, l = std::hypot(dx, dy);
        if (l >= kMinStepPx) {
            r.sx = dx / l;
            r.sy = dy / l;
            r.px_per_unit = l / step;
            return r;
        }
    }
    r.fallback = true;
    r.sx = 0;
    r.sy = -1; // screen y grows downwards: up is out
    r.px_per_unit = std::isfinite(cube_ppu) && cube_ppu > 1.f ? cube_ppu : 300.f;
    return r;
}

} // namespace editor_depth_axis
