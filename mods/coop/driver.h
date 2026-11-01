// Drives a locally simulated creature ("puppet") towards a remotely owned state:
// walk with the game's locomotion for small errors (so it animates), teleport for
// big ones, keep the orientation when idle.
#pragma once

#include <cmath>

#include "drive_plan.h"
#include "game.h"
#include "net/interp.h"

namespace drive {

struct State {
    double last_cmd = 0;
    Track track;
    net::Vec3 last_target;
    // diagnostics (coop.status): did the last frame's nudge stay where we put it?
    net::Vec3 nudged_to;
    bool nudged = false;
    uint32_t nudges = 0, nudges_kept = 0, teleports = 0, stalls = 0, trails = 0, goals = 0;
    float last_err = 0, last_speed = 0;
    double hold_until = 0; // BUG-0305: after a trail teleport the nudge puts it on the path every frame
    double last_step = 0;  // BUG-0175: the time of the previous step (the nudge per second, not per frame)
    bool kinematic = false; // BUG-0360: a walking partner's puppet, walked and placed on its path (modes.cpp)
    net::Vec3 goal_dir;     // BUG-0360: the direction of the walk goal given to it
    net::Vec3 drawn;        // BUG-0360 (coop.track): its position before this frame's driving, i.e. where the
                            // game's update put it and drew it (the mod's frame runs at Present)
};

inline float speed(const net::Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

// age: how old `pos` is (a snapshot received `age` seconds ago): the target moves on with the
// remote velocity, so the puppet does not trail a walking creature by its latency
// nudge: also pull the puppet a part of the way every frame (it cannot walk faster than the
// creature's top speed, so a walking puppet would otherwise trail by its latency)
// snap: teleport when the puppet is this far from where it should be
// stall: also teleport when it stays more than 2.5 m off for 3 s whatever the remote's speed (NPC
// proxies, BUG-0169: an NPC of the host pushing against a rock reports a speed while it stands)
// trail: also teleport when it stays more than 1.5 m off for 0.5 s (NPC proxies, BUG-0305: proxies of
// a herd that flees together push each other off the path and cannot run faster than the animal)
inline void step(void* p, const net::Vec3& pos_in, const net::Quat& rot, const net::Vec3& vel, State& ds, double now,
                 double age = 0, bool nudge = false, float snap = 8.f, bool stall = false, bool trail = false,
                 bool own_height = false)
{
    float a = (float)std::min(std::max(age, 0.0), 0.5);
    net::Vec3 pos{pos_in.x + vel.x * a, pos_in.y + vel.y * a, pos_in.z + vel.z * a};
    net::Vec3 cur = game::position(p);
    if (own_height) // BUG-0329: the guest's own ground height, not the host's
        pos = at_own_height(pos, cur);
    const double dt = ds.last_step > 0 && now > ds.last_step ? now - ds.last_step : 1.0 / 60;
    ds.last_step = now;
    if (ds.nudged && net::dist(cur, ds.nudged_to) < 0.5f)
        ds.nudges_kept++;
    ds.nudged = false;
    float err = net::dist(cur, pos);
    float sp = speed(vel);
    ds.last_err = err;
    ds.last_speed = speed(game::velocity(p));
    Plan pl = plan(ds.track, err, sp, now, snap, stall, trail);
    if (pl.snap) {
        game::teleport(p, pos, rot);
        ds.teleports++;
        ds.last_cmd = now;
    } else if (pl.walk) {
        // walk towards a point slightly ahead, faster than the remote while behind (catch up)
        net::Vec3 tgt{pos.x + vel.x * 0.25f, pos.y + vel.y * 0.25f, pos.z + vel.z * 0.25f};
        float want_sp = std::min((sp > 0.5f ? sp : 2.f) + err * 1.2f, std::max(sp, 2.f) * 2.5f);
        if (now - ds.last_cmd > 0.1 || net::dist(tgt, ds.last_target) > 1.f) {
            game::set_speed(p, want_sp);
            game::move_to(p, tgt, 0.3f);
            ds.last_cmd = now;
            ds.last_target = tgt;
        }
        if (nudge && err > 0.5f) {
            const float k = now < ds.hold_until ? 1.f : pull_k(0.15f, dt); // BUG-0175: per second
            ds.nudged_to = {cur.x + (pos.x - cur.x) * k, cur.y + (pos.y - cur.y) * k, cur.z + (pos.z - cur.z) * k};
            game::set_position(p, ds.nudged_to);
            ds.nudged = true;
            ds.nudges++;
        }
        // face where the remote faces (walking would turn the puppet towards its own path)
        game::set_orientation(p, rot);
    } else {
        game::set_orientation(p, rot);
    }
    // the remote stands still but the puppet could not reach it (rocks, other creatures), or it
    // stays far off and gets no closer (stall)
    if (pl.stuck || pl.stall || pl.trail) {
        game::teleport(p, pos, rot);
        ds.teleports++;
        ds.stalls += pl.stall;
        ds.trails += pl.trail;
        if (pl.trail)
            ds.hold_until = now + kTrailQuiet;
    }
}

} // namespace drive
