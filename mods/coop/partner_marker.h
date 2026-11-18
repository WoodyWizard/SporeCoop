#pragma once
#include "sporekit.h"

namespace partner_marker {
// Screen-space filled arrow; only call during SK_EV_OVERLAY.
void draw(const SkHost* host, float x, float y, float dx, float dy, float size, unsigned color);
// Decision 3E C (the owner 2026-11-13, images/partner_arrow_mockup_C.png): a slim chevron in the HUD's blue with a white
// edge, pointing along (dx, dy); `size` its width. Only during SK_EV_OVERLAY.
void chevron(const SkHost* host, float x, float y, float dx, float dy, float size, unsigned fill);
// a rounded plate (the Party panel's name plate look: dark, see-through, a thin light edge) for a partner's name tag
void plate(const SkHost* host, float x1, float y1, float x2, float y2, float radius, unsigned fill, unsigned edge);
}
