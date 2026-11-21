#pragma once
#include "sporekit.h"

namespace partner_marker {
// Screen-space filled arrow; only call during SK_EV_OVERLAY.
void draw(const SkHost* host, float x, float y, float dx, float dy, float size, unsigned color);
// Decision 3E C (the owner 2026-11-13, images/partner_arrow_mockup_C.png): a slim chevron in the HUD's blue with a white
// edge, pointing along (dx, dy); `size` its width. Only during SK_EV_OVERLAY.
void chevron(const SkHost* host, float x, float y, float dx, float dy, float size, unsigned fill);
// a rounded plate (the Party panel's name plate look: dark, see-through, a thin light edge) for a partner's name tag
// (7D A: `width` the edge's width in pixels; the player's colour replaces the light edge, owner 2026-11-19)
void plate(const SkHost* host, float x1, float y1, float x2, float y2, float radius, unsigned fill, unsigned edge,
           float width = 1.f);
// decision 7K A (BUG-9302): a ring on the ground round each point of `centres` (n x {x, y, z}, on a planet centred at
// the origin: the ground's normal is the point's direction), `radii[i]` metres out, `colors[i]` ARGB, with a thin dark
// edge; each projected with the game's camera (world_to_screen), a ring with a point behind the camera is left out.
// Only during SK_EV_OVERLAY. Returns the rings drawn.
int ground_rings(const SkHost* host, const float* centres, const float* radii, const unsigned* colors, int n);
}
