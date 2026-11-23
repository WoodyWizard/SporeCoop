// Portable decisions of the guest's NPC proxy walk (npc.cpp guest_tick), unit-tested in tests/npc_tests.cpp.
// No Windows, SporeKit or game headers.
#pragma once

#include <algorithm>
#include <cmath>

namespace npc_logic {

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};
inline float speed(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

constexpr float kReportedWalkMin = 0.3f; // m/s: a reported speed at or under this is none (BUG-0361)
constexpr float kPathGoOn = 0.5f;        // m/s: a walked proxy goes on at its path's speed over this (BUG-0361)
constexpr float kPathStart = 0.8f;       // m/s: an unreported walk starts at this frame's path speed (BUG-0394)
constexpr float kPathJump = 15.f;        // m/s: a path faster than this is a jump of the target (drive::path_velocity)

// BUG-1360 (`coop.fix npc.walk_gait`): px.walk_sp, the speed the host NPC reported while it last walked, clamps a
// walked proxy's path velocity in the frames whose state reports no speed (BUG-0361: at a stop the target can jump back
// up to 1 m, a 37 m/s "path" and a 0.8 m jump of the proxy). It was never cleared: a walk without a reported speed
// (BUG-0394: moves an animation carries, every walk of npc_walk_novel) was clamped to the speed of some earlier walk
// with one. Observed (npc_walk_novel 20261120-133703 lane 3, proxy #21): the game's own speed 0.28-0.31 m/s in every
// walked frame of the run while its path ran at 4 m/s, the placing carrying it 0.7-0.8 m behind; the game's
// locomotion at 0.3 m/s plays the standing idle 02481DE5 (78-80 % walk gait). With the fix walk_sp belongs to one
// walk: a proxy that is not walked and has no reported speed (it stands, or an unreported walk starts) forgets it, so
// an unreported walk follows its path's speed (a path jump over kPathJump is still no velocity, as
// drive::path_velocity has it); within a walk that reported its speed the stop clamp is the same. Off: 0.7.16.1.
struct WalkIn {
    bool kinematic = false;  // walked on the host's path last frame
    Vec3 vel{};              // the state's (reported) velocity
    Vec3 path_v{};           // drive::PathTrack::v: this frame's path velocity
    Vec3 path_v_ema{};       // drive::PathTrack::v_ema
    float path_sp_ema = 0.f; // drive::PathTrack::sp_ema
    float walk_sp = 0.f;     // px.walk_sp before this frame
};
struct WalkOut {
    Vec3 vel{};              // the velocity the walk uses (npc_rules::place, drive::walk_place)
    float walk_speed = -1.f; // WalkPlace::walk_speed (-1: the velocity's)
    float walk_sp = 0.f;     // px.walk_sp after this frame
};
inline WalkOut walk_velocity(const WalkIn& in, bool fix)
{
    WalkOut out;
    out.vel = in.vel;
    out.walk_sp = in.walk_sp;
    const float rep = speed(in.vel);
    if (in.kinematic && rep <= kReportedWalkMin) {
        // not faster than it walked (BUG-0361)
        const float ps = speed(in.path_v);
        if (ps > kPathGoOn) {
            if (!fix || in.walk_sp > kReportedWalkMin) {
                const float k = in.walk_sp > kReportedWalkMin && ps > in.walk_sp ? in.walk_sp / ps : 1.f;
                out.vel = {in.path_v.x * k, in.path_v.y * k, in.path_v.z * k};
            } else
                out.vel = ps < kPathJump ? in.path_v : Vec3{}; // an unreported walk: its path (no jump)
        }
    } else if (rep <= kReportedWalkMin) {
        // BUG-0394: a host NPC that moves without a speed of its own: its path's velocity (drive::path_velocity)
        if (fix)
            out.walk_sp = 0.f; // BUG-1360: no walk with a reported speed goes on
        if (speed(in.path_v_ema) > kPathGoOn)
            out.vel = in.path_v_ema;
        else {
            const float ps = speed(in.path_v);
            out.vel = ps > kPathStart && ps < kPathJump ? in.path_v : Vec3{};
        }
    } else {
        out.walk_sp = rep;
        // a host NPC that reports its speed but moves slower (pushed by its herd, against a rock, turning): the game
        // walks the proxy at the path's speed (only the walk: scaling the placing too made it trail at every start)
        const float ps = std::max(in.path_sp_ema * 1.1f, kPathGoOn);
        if (ps < out.walk_sp)
            out.walk_speed = ps;
    }
    return out;
}

} // namespace npc_logic
