// What drive::step does with a puppet in one frame, as a pure decision (unit test:
// tests/npc_tests.cpp). Portable: no game calls.
#pragma once

#include <cmath>

namespace drive {

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

// BUG-0281: the nudge pulls a proxy 15 % of the way to its target every frame. At 60 fps that is
// ~99.9 % a second, at the 15 fps of a loaded guest (4 players on one PC) only ~91 %, and a running
// NPC whose proxy does not walk by itself trailed 2-6 m. Scaled by the frame time the pull per
// second is the same at any frame rate: 1 - 0.85^(dt * 60). `scaled` off: 0.15 per frame.
constexpr float kNudge = 0.15f;
inline float nudge_gain(double dt, bool scaled)
{
    if (!scaled)
        return kNudge;
    if (dt <= 0)
        return 0.f;
    if (dt > 0.25)
        dt = 0.25; // a hitch: no jump to the target
    return 1.f - (float)std::pow(1.0 - kNudge, dt * 60.0);
}

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
