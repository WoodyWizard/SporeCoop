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

// walk_place's placing step this frame (driver.h), as a pure rule. What a frame draws is the placing's step plus the
// game's own walk of the next frame (at walk_sp). `sp` the target's speed, `gv` the game's walk speed now (its
// velocity), `err` the distance to the target, `base_catch` WalkPlace::catch_up, `catch_prop` / `lead_fix` BUG-0425's
// error-proportional catch-up (drive.lead), `lead_speed` BUG-0619's frame bound (> 0: the body's own speed), `held` a
// placing by marked teleports (keep_lost: the game's walk is undone).
// BUG-1235 / item 12 of docs/plan-0.7.17.md (`coop.fix drive.lead_cap`): the raw lead's catch-up of a Tribe member mirror
// that is late (catch_prop) drew 0.86-0.93 m frames at 30 fps (the test's jump limit 3 x 9 m/s x 32 ms = 0.86 m):
// tribe_walk_player 20261123-023643 member 8 (2.74 m behind at 9 m/s: placing (9 + 11) m/s + the game's walk 9 m/s),
// member 3 and -071312 member 1 (12 m/s run, 1-1.4 m behind: (12 + 5.6) + 12 m/s). Two holes: (1) the bound's
// catch-up was catch_prop's (up to sp + 3 m/s), so 2 x lead_speed + catch_up - walk_sp held nothing (sp + catch_up);
// (2) the raw lead had a bound only while its late lead caught up (lead_speed 0 otherwise, tribe_logic::
// mirror_lead_speed); and the placing's budget (sp + catch_up) left out that the game walks the mirror at its speed
// too. With the fix, when the bound is on (lead_speed > 0): a frame draws at most 2 x lead_speed + base_catch (BUG-0619's
// promise); with catch_prop the frame (the game's walk now included) closes the error at catch_up over the body's
// speed, sp + catch_up in all (a 6-9 m error, BUG-0674: 12 m/s over a 9 m/s body, under 2 m in ~0.6 s); while it
// speeds up or slows down (sp under kSlowCapSpeed) with a small error (under kSlowCapErr) at most kSlowCapK x sp +
// kSlowCapAdd, whether the game's walk of the next frame comes or not (between 0.5 x sp + 0.9 and that: inside the
// test's "uneven" line, half the target's step + 1 m/s; the stop-and-go half of BUG-1235: surges of the target's speed
// + 3 m/s at 1-5 m/s, uneven 20 % in 20261123-023643, guest's member 9 at 1.6 m/s drawn 4.8-5.0 m/s). Idea from fix/agy-open-bugs c8c548db (the outside agent's branch). Off: 0.7.16.1.
inline bool g_lead_cap_fix = true;
constexpr float kCatchTime = 0.25f;  // s: catch_prop closes the error in about this
constexpr float kSlowCapSpeed = 5.f; // m/s: a walk starting or stopping
constexpr float kSlowCapErr = 1.5f;  // m: under body_trail's 2 m
constexpr float kSlowCapK = 1.5f, kSlowCapAdd = 0.9f;
inline float catch_speed(float sp, float err, float base_catch, bool catch_prop, bool lead_fix)
{
    float c = base_catch;
    if (catch_prop && lead_fix)
        c = std::max(c, std::min(err / kCatchTime, sp + base_catch));
    return c;
}
inline float walk_place_max_step(float sp, float walk_sp, float gv, float err, double dt, float base_catch, bool catch_prop,
                                 bool lead_fix, float lead_speed, bool held, bool fix = g_lead_cap_fix)
{
    const float catch_up = catch_speed(sp, err, base_catch, catch_prop, lead_fix);
    float step = sp + catch_up; // m/s
    if (lead_speed > 0) {
        if (!fix)
            return std::min(step, std::max(0.f, 2.f * lead_speed + catch_up - walk_sp)) * (float)dt;
        const float hard = 2.f * lead_speed + base_catch;
        const float walk_now = held || !(gv > 0.f) ? 0.f : std::min(gv, walk_sp);
        if (catch_prop && lead_fix)
            step = std::max(0.f, std::min(sp + catch_up, hard) - walk_now);
        if (sp < kSlowCapSpeed && err < kSlowCapErr)
            step = std::min(step, std::max(0.f, kSlowCapK * sp + kSlowCapAdd - walk_sp)); // (the next frame's walk)
        step = std::min(step, std::max(0.f, hard - walk_sp));
    }
    return step * (float)dt;
}

} // namespace drive
