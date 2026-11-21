#pragma once
#include "sporekit.h"

#include <cmath>
#include <string>

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
// decision 7K A (BUG-9302), BUG-9321: a ring on the ground round each unit a player controls, in that player's colour.
// at: its centre, up: the ground's normal there (a planet centred at the origin: the point's direction; Cell and a
// star system: +z), radius in metres, color ARGB (its alpha the ring's opacity), min_px / max_px: the ring's radius on
// the screen is kept between them (0: no bound). soft (BUG-9321, the owner's reference picture of 2026-11-21):
// a gradient glow, almost clear in the middle, the colour strongest near the rim, a soft edge with a faint dark halo;
// else the first look (a hard band over a dark edge). Projected with the game's camera (world_to_screen); a ring with
// a point behind the camera is left out. Only during SK_EV_OVERLAY. Returns the rings drawn.
struct Ring {
    float at[3];
    float up[3];
    float radius;
    unsigned color;
    float min_px = 0, max_px = 0;
    bool planet = false; // up is the planet's normal (the caller leaves out a ring beyond the horizon)
    // depth: metres along up from at (NAN: just over the ground, a little more for a bigger ring); negative puts the
    // ring under a unit that has no ground under it (a cell on its plane, a ship), so it never cuts through its body
    float lift = NAN;
};
// depth (BUG-9321): world-space triangles through the game's view and projection, tested against the scene's depth
// buffer (a unit or a hill in front hides that part of its ring); drawn flat when the device has no depth buffer or
// transforms at the overlay pass
int rings(const SkHost* host, const Ring* rings, int n, bool soft, bool depth);
// the kept scene depth-stencil reference: release before a device Reset and at the unload (BUG-9321)
void release_depth();
// tests (BUG-9321): the device's depth-stencil, render target, depth states and transforms at the overlay pass
std::string device_report(const SkHost* host);
}
