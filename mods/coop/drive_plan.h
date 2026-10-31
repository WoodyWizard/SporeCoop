// What drive::step does with a puppet in one frame, as a pure decision (unit test:
// tests/npc_tests.cpp). Portable: no game calls.
#pragma once

#include <algorithm>
#include <cmath>

namespace drive {

// BUG-0175: a per-frame pull of a mirror towards where it should be ("move k of the way") made the
// error a function of the frame rate: a target moving at v leaves a mirror that cannot move by
// itself (the GOG 2024 guest's paused game time after a load, an NPC proxy held by others)
// (1 - k) / k * v * frame_time behind, 4x as far at 15 fps as at 60. With the fix the pull is k60 per 1/60 s, so a frame of dt
// pulls 1 - (1 - k60)^(dt * 60): the same pull per second at every frame rate, and at 60 fps the
// old one. Off (`coop.fix drive.nudge_dt off`): k60 every frame.
inline bool g_nudge_dt_fix = true;
inline float pull_k(float k60, double dt, bool fix = g_nudge_dt_fix)
{
    if (!fix || !(dt > 0))
        return k60;
    double n = std::min(dt, 0.1) * 60.0; // a hitch (a load, a dialog) counts as 0.1 s
    return (float)(1.0 - std::pow(1.0 - (double)k60, n));
}

// BUG-0329: the ground of a host and of a guest can differ in height in places (a Tribe village 5 m
// higher or lower on the guests, which load the host's save, than on the host that made it live).
// A mirror on the ground was driven at the host's absolute height: 5 m under or over its own ground
// it never reached its target, walked in place (5.5 m/s while the host's member stood) and was
// teleported again and again. With `own_height` the target keeps its direction from the planet's
// centre but takes the mirror's own distance from it, when the two differ by less than kOwnHeightMax.
inline bool g_own_height_fix = true;
constexpr float kOwnHeightMax = 10.f;
template <class V> inline V at_own_height(const V& target, const V& cur, bool fix = g_own_height_fix)
{
    float rt = std::sqrt(target.x * target.x + target.y * target.y + target.z * target.z);
    float rc = std::sqrt(cur.x * cur.x + cur.y * cur.y + cur.z * cur.z);
    if (!fix || rt < 1.f || rc < 1.f || std::fabs(rt - rc) > kOwnHeightMax)
        return target;
    float k = rc / rt;
    V out = target;
    out.x *= k; out.y *= k; out.z *= k;
    return out;
}

struct Track {
    double stuck_since = 0; // the remote stands still and the puppet is more than 1 m off
    double far_since = 0;   // BUG-0169: the puppet is more than kFar off and gets no closer
    float far_best = 0;
    double far_quiet_until = 0; // after such a teleport: not again for a while (a spot it cannot stand on)
    double trail_since = 0;     // BUG-0305: the puppet is more than kTrail off and gets no closer
    float trail_best = 0;
    double trail_quiet_until = 0;
};

struct Plan {
    bool snap = false;  // teleport: too far
    bool walk = false;  // walk with the game's locomotion
    bool stuck = false; // teleport: the remote stands still, the puppet could not reach it
    bool stall = false; // teleport: far off for seconds whatever the remote's speed (BUG-0169)
    bool trail = false; // teleport: trailing more than kTrail for half a second (BUG-0305)
};

constexpr float kFar = 2.5f;     // m
constexpr double kFarHold = 3.0; // s without getting 0.5 m closer
constexpr double kFarQuiet = 10.0;
// BUG-0305: an NPC proxy pushed off its path by the proxies next to it (a herd that flees together
// in a Tribe hunt) walks at most at the animal's own speed: it trailed 2-4 m for the whole dash
constexpr float kTrail = 1.5f;     // m
constexpr double kTrailHold = 0.5; // s without getting kTrailGain closer
constexpr float kTrailGain = 0.3f; // m
constexpr double kTrailQuiet = 1.0;

// err: puppet to target (m), sp: the remote's speed (m/s), snap: teleport distance.
// stall_fix off: the code before BUG-0169 (a remote that reports a speed, e.g. an NPC pushing
// against a rock, keeps the puppet walking to a spot it cannot reach, for as long as it pushes)
inline Plan plan(Track& t, float err, float sp, double now, float snap, bool stall_fix = true, bool trail_fix = false)
{
    Plan p;
    // trail: checked first, independent of the stall rule below
    if (!trail_fix || err > snap || err <= kTrail || now < t.trail_quiet_until)
        t.trail_since = 0;
    else if (t.trail_since == 0 || err < t.trail_best - kTrailGain) {
        t.trail_since = now;
        t.trail_best = err;
    } else if (now - t.trail_since > kTrailHold) {
        p.trail = true;
        t.trail_since = 0;
        t.trail_quiet_until = now + kTrailQuiet;
    }
    if (err > snap)
        p.snap = true;
    else if (sp > 0.3f || err > 0.8f)
        p.walk = true;
    if (sp < 0.3f && err > 1.0f) {
        if (t.stuck_since == 0)
            t.stuck_since = now;
        else if (now - t.stuck_since > 1.5) {
            p.stuck = true;
            t.stuck_since = 0;
        }
    } else
        t.stuck_since = 0;
    if (!stall_fix || p.snap || p.stuck || err <= kFar || now < t.far_quiet_until) {
        t.far_since = 0;
        return p;
    }
    if (t.far_since == 0 || err < t.far_best - 0.5f) {
        t.far_since = now;
        t.far_best = err;
    } else if (now - t.far_since > kFarHold) {
        p.stall = true;
        t.far_since = 0;
        t.far_quiet_until = now + kFarQuiet;
    }
    return p;
}

} // namespace drive
