// Portable lead / extrapolation rules for Space colony citizens (kind "space_citizen", BUG-1404).
// Included by worldobj.cpp and by native unit tests (tests/space_tests.cpp).
//
// With space.citizen_lead on:
// - Host streams space_citizen snapshots every kFastEvery (0.25 s) to guests of 0.7.15+ (older guests stay at 0.5 s).
// - Guest estimates velocity from snapshots at least kFastVelDt (0.15 s) apart.
// - Per-frame extrapolation adds one-way network latency and damps smoothly past kLeadKnee (0.30 s, tau kLeadTau 0.15 s),
//   capped at kLeadMax (0.50 s), instead of extrapolating linearly up to 1.0 s (which overshot stops and turns by 3-5 m).
// With space.citizen_lead off: 0.7.15 behavior (0.5 s snapshots, 0.30 s vel dt, no latency lead, linear clamp at 1.0 s).
#pragma once

#include <algorithm>
#include <cmath>

#include "net/protocol.h"

namespace wobj_space {

constexpr double kFastEvery = 0.25;  // s between host snapshots when space.citizen_lead is on
constexpr double kSlowEvery = 0.50;  // s (0.7.15)
constexpr double kFastVelDt = 0.15;  // s minimum snapshot span for velocity when on
constexpr double kSlowVelDt = 0.30;  // s (0.7.15)

constexpr double kLeadKnee = 0.30;   // s full-speed extrapolation past the last snapshot
constexpr double kLeadTau = 0.15;    // s exponential decay past the knee (damps overshoots on stops / packet drops)
constexpr double kLeadMax = 0.50;    // s hard cap when space.citizen_lead is on (2x fast tick)
constexpr double kOldMax = 1.0;      // s (0.7.15 linear cap)

inline double citizen_tick_every(bool lead_fix) { return lead_fix ? kFastEvery : kSlowEvery; }
inline double citizen_vel_min_dt(bool lead_fix) { return lead_fix ? kFastVelDt : kSlowVelDt; }

// Extrapolation horizon (s) for a bound Space colony citizen at snapshot `age` (s) and `one_way` latency (s).
inline double citizen_extrap_time(double age, double one_way, bool lead_fix)
{
    double a = std::max(age, 0.0);
    if (!lead_fix)
        return std::min(a, kOldMax);
    double lat = std::clamp(one_way, 0.0, 0.25);
    double a0 = a + lat;
    double knee = kLeadKnee + lat;
    if (a0 > knee)
        a0 = knee + kLeadTau * (1.0 - std::exp(-(a0 - knee) / kLeadTau));
    return std::min(a0, kLeadMax);
}

inline net::Vec3 citizen_target(const net::Vec3& hp, const net::Vec3& vel, double age, double one_way, bool lead_fix)
{
    float t = (float)citizen_extrap_time(age, one_way, lead_fix);
    return {hp.x + vel.x * t, hp.y + vel.y * t, hp.z + vel.z * t};
}

} // namespace wobj_space
