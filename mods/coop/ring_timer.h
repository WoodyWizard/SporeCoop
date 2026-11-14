// The revive ring drawn on screen (ring_timer_logic.h has the geometry; docs/re/ui.md#ui-revive-ring).
#pragma once

#include "ring_timer_logic.h"
#include "sporekit.h"

namespace ring_timer {
// Only during SK_EV_OVERLAY. (x, y): the centre in screen pixels; `left`: the fraction of the time still left (1 at the
// knockout, 0 when it runs out); `revive`: a partner's revive so far (0..1, 0: none); `now`: for the last quarter's
// blink. Nothing is drawn for a position that is not finite.
void draw(const SkHost* host, float x, float y, float radius, float left, float revive, double now);

// BUG-9156: a red edge round the screen (only during SK_EV_OVERLAY), strength 0..1
void vignette(const SkHost* host, int sw, int sh, float strength);
// BUG-9156: a rounded panel (the spectating box, the give-up button); border 0: none
void panel(const SkHost* host, float x0, float y0, float x1, float y1, float radius, uint32_t fill, uint32_t border);
// BUG-9156: a filled arrow pointing left or right, centred at (x, y), `size` its half height
void arrow(const SkHost* host, float x, float y, float size, bool left, uint32_t argb);

// BUG-9159 (owner decision 1P A): a late joiner's wait for their home after the creature editor: the frame blurred and
// dimmed, a ring at (x, y) filling with `progress` (0..1) and a light arc turning round it (only during SK_EV_OVERLAY)
void loading(const SkHost* host, int sw, int sh, float x, float y, float radius, float progress, double now);
// BUG-9202: the mean brightness (0..255) of the game's frame under the last loading screen drawn, -1 when none in 0.5 s
float scene_luma(double now);

// `coop.fix ui.revive_ring` (on): the ring instead of the text countdowns (off: "KO 12", "[REVIVE 12s]", "Downed: 12 s")
bool enabled();
} // namespace ring_timer
