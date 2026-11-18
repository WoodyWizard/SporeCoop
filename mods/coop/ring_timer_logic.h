// The revive ring (owner request 2026-11-12: "Instead of a 'revive' and countdown, the usual circle with a draining ring
// that resembles time running out, on every stage that needs revives"): the pure geometry and colours, no Windows and
// no game (tests/ring_timer_tests.cpp). The drawing is ring_timer.cpp; who draws it: docs/re/ui.md#ui-revive-ring.
//
// Look: a dark disc; a dim track; the time left as a bright arc. Full at the knockout, it drains clockwise from
// 12 o'clock like a clock hand eating the ring (the arc left runs from the hand on to 12 o'clock), amber when there is
// plenty of time, red near the end; a green pie inside fills while a partner revives; a white "+" in the middle.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ring_timer {

struct Pt {
    float x = 0, y = 0;
};

constexpr int kSegments = 48; // for the full circle (7.5 degrees each: smooth at 30 px)
constexpr float kPi = 3.14159265358979f;

// the screen point at fraction `f` of the circle (0 = 12 o'clock, clockwise; screen y grows downwards)
inline Pt on_circle(float cx, float cy, float r, float f)
{
    const float a = f * 2.f * kPi;
    return {cx + r * std::sin(a), cy - r * std::cos(a)};
}

// a ring's arc as a triangle strip (outer, inner, outer, inner ...) from fraction `start` clockwise over `frac` of the
// circle (both clamped to 0..1), between radius r_in and r_out. Empty when nothing is left.
inline std::vector<Pt> arc_strip(float cx, float cy, float r_in, float r_out, float start, float frac, int segments = kSegments)
{
    std::vector<Pt> v;
    start = std::clamp(start, 0.f, 1.f);
    frac = std::clamp(frac, 0.f, 1.f - start);
    if (!(frac > 0) || !(r_out > r_in) || r_in < 0 || segments < 3) return v;
    const int n = std::max(1, (int)std::ceil(frac * segments));
    v.reserve(2 * (n + 1));
    for (int i = 0; i <= n; ++i) {
        const float f = start + frac * (float)i / (float)n;
        v.push_back(on_circle(cx, cy, r_out, f));
        v.push_back(on_circle(cx, cy, r_in, f));
    }
    return v;
}

// a pie from 12 o'clock clockwise over `frac` as a triangle list (centre, edge, edge per segment). Empty for 0.
inline std::vector<Pt> pie(float cx, float cy, float r, float frac, int segments = kSegments)
{
    std::vector<Pt> v;
    frac = std::clamp(frac, 0.f, 1.f);
    if (!(frac > 0) || !(r > 0) || segments < 3) return v;
    const int n = std::max(1, (int)std::ceil(frac * segments));
    v.reserve(3 * n);
    for (int i = 0; i < n; ++i) {
        v.push_back({cx, cy});
        v.push_back(on_circle(cx, cy, r, frac * (float)i / (float)n));
        v.push_back(on_circle(cx, cy, r, frac * (float)(i + 1) / (float)n));
    }
    return v;
}

// the arc of the time left: it begins where the hand is (the part already gone) and runs on to 12 o'clock
inline float left_start(float left) { return 1.f - std::clamp(left, 0.f, 1.f); }

inline uint32_t lerp_argb(uint32_t a, uint32_t b, float t)
{
    t = std::clamp(t, 0.f, 1.f);
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const float ca = (float)((a >> shift) & 0xFF), cb = (float)((b >> shift) & 0xFF);
        out |= (uint32_t)std::lround(ca + (cb - ca) * t) << shift;
    }
    return out;
}

constexpr uint32_t kAmber = 0xFFFFB030, kRed = 0xFFFF3B30, kTrack = 0xFF4A2A26, kDisc = 0xB0101418,
                   kRevive = 0xFF4CD964, kCross = 0xFFFFFFFF;

// the arc's colour: amber with half the time or more left, then towards red; the last quarter blinks (`blink_on`
// false: the darker red)
inline uint32_t time_color(float left, bool blink_on = true)
{
    left = std::clamp(left, 0.f, 1.f);
    if (left >= 0.5f) return kAmber;
    const uint32_t c = lerp_argb(kRed, kAmber, left / 0.5f);
    return left < 0.25f && !blink_on ? lerp_argb(c, 0xFF000000, 0.35f) : c;
}

// the ring's outer radius for a screen height: 30 px at 1080, never below 16 or above 36
inline float radius_for(int screen_h) { return std::clamp((float)screen_h / 36.f, 16.f, 36.f); }

// the fraction of time left (0..1) from seconds left and the total; 0 for a bad total
inline float fraction(double seconds_left, double total)
{
    if (!(total > 0) || !std::isfinite(seconds_left)) return 0.f;
    return (float)std::clamp(seconds_left / total, 0.0, 1.0);
}

} // namespace ring_timer
