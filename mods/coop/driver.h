// Drives a locally simulated creature ("puppet") towards a remotely owned state:
// walk with the game's locomotion for small errors (so it animates), teleport for
// big ones, keep the orientation when idle.
#pragma once

#include <cmath>

#include "drive_plan.h"
#include "game.h"
#include "net/interp.h"

namespace drive {

// BUG-0361 / BUG-0394: the remote's interpolated path frame by frame (where the driver is asked to put the
// creature), for a remote that moves without reporting a speed and for a walk's last frames
struct PathTrack {
    net::Vec3 prev{};
    double prev_t = 0;
    net::Vec3 v{};     // this frame's path velocity
    net::Vec3 v_ema{}; // smoothed over ~0.2 s
    float sp_ema = 0;  // its speed, smoothed over ~0.2 s
};

struct State {
    double last_cmd = 0;
    Track track;
    net::Vec3 last_target;
    // diagnostics (coop.status): did the last frame's nudge stay where we put it?
    net::Vec3 nudged_to;
    bool nudged = false;
    uint32_t nudges = 0, nudges_kept = 0, teleports = 0, stalls = 0, trails = 0, goals = 0;
    uint32_t grounded = 0;   // BUG-0399: put out of the ground (step with ground)
    float grounded_from = 0; // BUG-0399: how far over (negative: under) the ground it was then (m)
    float last_err = 0, last_speed = 0;
    double hold_until = 0; // BUG-0305: after a trail teleport the nudge puts it on the path every frame
    double last_step = 0;  // BUG-0175: the time of the previous step (the nudge per second, not per frame)
    bool kinematic = false; // BUG-0360: a walking partner's puppet, walked and placed on its path (modes.cpp)
    net::Vec3 goal_dir;     // BUG-0360: the direction of the walk goal given to it
    PathTrack path;          // BUG-0361 / BUG-0394: path_update
    net::Vec3 placed_to;    // BUG-0361 (walk_place): where it was placed last frame
    uint32_t place_lost = 0; // BUG-0361: frames the game did not keep that place (a teleport with the mark then)
    double stuck_since = 0;  // BUG-0361: since when its error grows although it is placed
    bool held = false;       // BUG-0361: teleported (with the mark) onto the path until it is on it again
    net::Vec3 jump_from;    // BUG-0499: the target of the frame before (a jump of the target: the puppet jumps too)
    bool jump_have = false;
    uint32_t jumps = 0;     // BUG-0499: puppets put at a target that jumped
    net::Vec3 drawn;        // BUG-0360 (coop.track): its position before this frame's driving, i.e. where the
                            // game's update put it and drew it (the mod's frame runs at Present)
};

inline float speed(const net::Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

// once per frame with the remote's interpolated position: the path's velocity (0 after a gap)
inline void path_update(State& ds, const net::Vec3& pos, double now)
{
    PathTrack& t = ds.path;
    double dt = now - t.prev_t;
    if (t.prev_t > 0 && dt > 0 && dt < 0.25) {
        float k = 1.f / (float)dt, a = (float)std::min(1.0, dt / 0.2);
        t.v = {(pos.x - t.prev.x) * k, (pos.y - t.prev.y) * k, (pos.z - t.prev.z) * k};
        t.v_ema = {t.v_ema.x + (t.v.x - t.v_ema.x) * a, t.v_ema.y + (t.v.y - t.v_ema.y) * a,
                   t.v_ema.z + (t.v.z - t.v_ema.z) * a};
        t.sp_ema += (speed(t.v) - t.sp_ema) * a;
    } else {
        t.v = t.v_ema = {};
        t.sp_ema = 0;
    }
    t.prev = pos;
    t.prev_t = now;
}

// the velocity of a remote that moves without reporting one: the smoothed path once it is over 0.5 m/s, before
// that this frame's path from 0.8 m/s on (a walk's start: the smoothed one is frames late at 30 fps); not a
// jump of the path (over 15 m/s). A remote that stands has a still path (turning on the spot moves nothing)
inline net::Vec3 path_velocity(const State& ds)
{
    const PathTrack& t = ds.path;
    if (speed(t.v_ema) > 0.5f)
        return t.v_ema;
    float v = speed(t.v);
    return v > 0.8f && v < 15.f ? t.v : net::Vec3{};
}

// BUG-0366 (creature.puppet_ground): see walk_place below
constexpr float kOverGround = 0.5f, kUnderGround = 3.f; // m
constexpr float kSink = 0.05f;                          // m (0.15: the drawn puppet 0.13 m under at times)
inline net::Vec3 on_own_ground(const net::Vec3& pos)
{
    float r = std::sqrt(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z), g = game::ground_radius(pos);
    if (g <= 0 || r < 1.f || r - g > kOverGround || r - g < -kUnderGround)
        return pos;
    float k = (g - kSink) / r;
    return {pos.x * k, pos.y * k, pos.z * k};
}

// BUG-0399 (npc.proxy_ground): right after a join some Tribe animal proxies, driven at their own height
// (own_height: the radius they have, BUG-0329), stood or walked 0.75-1.7 m under the guest's ground for 1-30 s
// (docs/re/tribe.md#tribe-proxy-ground). A proxy made at the host's absolute height where this game's ground
// lies higher (BUG-0329: up to 5 m) would start under it too (inferred). A target more than kUnderTol and at most
// kOutMax under this game's ground (cPlanetModel::GetHeightAt, the terrain only: never under it legitimately)
// goes onto it, kSink into it as on_own_ground.
inline bool g_out_of_ground_fix = true;
constexpr float kUnderTol = 0.25f, kOutMax = 15.f; // m
inline net::Vec3 out_of_ground(const net::Vec3& pos, bool fix = g_out_of_ground_fix)
{
    float r = std::sqrt(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z);
    if (!fix || r < 1.f)
        return pos;
    float g = game::ground_radius(pos);
    if (g <= 0 || r - g >= -kUnderTol || r - g < -kOutMax)
        return pos;
    float k = (g - kSink) / r;
    return {pos.x * k, pos.y * k, pos.z * k};
}
// BUG-0444 (tribe.mirror_ground): onto this game's ground from up to kOutMax under or over it, for a creature the
// game does not ground itself (its game time paused: no locomotion runs). Higher up it is left where it is.
inline net::Vec3 onto_ground(const net::Vec3& pos)
{
    float r = std::sqrt(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z);
    if (r < 1.f)
        return pos;
    float g = game::ground_radius(pos);
    if (g <= 0 || std::fabs(r - g) > kOutMax)
        return pos;
    float k = (g - kSink) / r;
    return {pos.x * k, pos.y * k, pos.z * k};
}
// how far `pos` is over this game's ground (m; 0 when unknown)
inline float over_ground(const net::Vec3& pos)
{
    float r = std::sqrt(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z), g = r > 1.f ? game::ground_radius(pos) : 0.f;
    return g > 0 ? r - g : 0.f;
}

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
                 bool own_height = false, bool ground = false)
{
    float a = (float)std::min(std::max(age, 0.0), 0.5);
    net::Vec3 pos{pos_in.x + vel.x * a, pos_in.y + vel.y * a, pos_in.z + vel.z * a};
    net::Vec3 cur = game::position(p);
    if (own_height) // BUG-0329: the guest's own ground height, not the host's
        pos = at_own_height(pos, cur);
    if (ground) {
        // BUG-0399: never a target under this game's ground; a creature found under it (made there, or
        // kept there by own_height) is put onto it at once (walked or pulled it would stay under).
        // `ground` is the caller's switch (npc.proxy_ground, tribe.mirror_ground)
        pos = out_of_ground(pos, true);
        if (net::dist(out_of_ground(cur, true), cur) > 0.01f) {
            ds.grounded_from = over_ground(cur);
            game::teleport(p, pos, rot);
            ds.teleports++;
            ds.grounded++;
            ds.last_step = now;
            ds.last_cmd = now;
            ds.track = Track{};
            return;
        }
    }
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

// BUG-0360 (Creature puppets), BUG-0365 (Tribe mirrors): a remote creature that walks is walked by the game
// and placed on its path. drive::step gave a MoveTo again every 0.1 s (which restarts the game's walk: the
// creature stood or crept, or a citizen at its default 9 m/s reached the point ahead and stopped) and pulled
// it with SetPosition (which the locomotion undoes), so it moved in steps and trailed until a snap.
// walk_place, every frame while the remote moves:
// * the game walks it: the remote's speed (SetDesiredSpeed) and a MoveTo goal_ahead m ahead on its path,
//   given again only when that direction turns more than ~6 degrees (at most every min_gap s, at least
//   every max_gap s): its walk gait and walk animation run;
// * game::place puts it on the remote's path with the remote's facing: a teleport (the locomotion keeps
//   its own position, SetPosition alone is undone) without the teleport mark +0xB5D, which restarts the
//   animation's gait. An error left from standing is closed at catch_up m/s faster than the remote.
// walk_end when the remote stands again: a MoveTo to where it stands (not StopMovement, which put it back
// up to 3 m to where its own walk was), then drive::step takes over.
// BUG-0366 (creature.puppet_ground): a target from kUnderGround under to kOverGround over this game's
// ground (cPlanetModel::GetHeightAt) goes kSink into it: placed exactly on the ground a walking creature is
// a few cm over it after the game's next step downhill and plays its fall animation; under the ground it
// stays there. Higher up (a jump, a rock) it is left where it is.
struct WalkPlace {
    float goal_ahead = 12.f;  // m: the MoveTo point ahead on the remote's path
    float turn_dot = 0.995f;  // a new MoveTo when its direction turns more than ~6 degrees,
    double min_gap = 0.3;     // not more often than this (s: given every 0.1 s it restarts the walk)
    double max_gap = 1.5;     // and at least this often (s)
    float catch_up = 3.f;     // m/s faster than the remote while it is off the path
    bool own_height = false;  // BUG-0329: at the creature's own distance from the planet's centre
    // BUG-0365: the speed after the MoveTo (~ a MoveTo sets a citizen's speed back to its default 9 m/s:
    // the first frame of a mirror's walk went at 9 m/s); the Creature puppets keep the old order
    bool speed_after_goal = false;
    // BUG-0366: the target onto this game's ground (on_own_ground). The Creature puppet puts its target
    // there before it chooses walk or stand (creature_drive), so it leaves this off
    bool ground = false;
    // the longest frame the catch-up step is scaled by (s). BUG-0365: a guest at 3-4 fps (226-336 ms
    // frames, lab load 31 on 12 cores) closed 1.2 m per frame of a 2-3 m step of its member, fell 8 m
    // behind and was teleported; the Creature puppet keeps 0.1
    double max_dt = 0.1;
    // BUG-0361 (NPC proxies): the speed the game walks it at when that is slower than the remote's reported
    // speed (a herd animal pushed by its mates walks slower than it reports); the placing follows the remote
    float walk_speed = -1.f;
    // BUG-0361: farther off than this (m) it is teleported onto the path when the remote itself jumped (its
    // path moved over snap/2 in this frame: a host NPC put 4.8 m to the side stayed 2-7 m off for 2 s when
    // closed at catch_up); needs path_update with this frame's target first; 0: never
    float snap = 0.f;
    // BUG-0361: when the game does not keep the place (a host NPC thrown back by a hit, 9 m/s slowing down:
    // without the teleport mark the proxy's own locomotion moved it back each frame by as much as it was
    // placed, it stood and ended 4.5 m off), i.e. its error grows over kWalkLostErr for kWalkLostAfter s or
    // last frame's place is lost outright, it is moved by teleports with the mark (at the same catch-up
    // speed) until it is on the path again
    bool keep_lost = false;
    // tests (coop.npctrack variant): 1 place only, no walk goal (the velocity written); 2 the walk goal
    // but teleports with the mark; 3 keep_lost off
    int variant = 0;
};
constexpr float kWalkLostErr = 0.75f;   // m
constexpr double kWalkLostAfter = 0.15; // s

inline void walk_place(void* p, const net::Vec3& pos_in, const net::Quat& rot, const net::Vec3& vel, State& ds, double now,
                       const WalkPlace& o = {})
{
    float sp = speed(vel);
    if (sp <= 0.f)
        return;
    net::Vec3 pos = o.own_height ? at_own_height(pos_in, game::position(p)) : pos_in;
    if (o.ground)
        pos = on_own_ground(pos);
    bool was = ds.kinematic;
    ds.kinematic = true;
    if (!was) {
        ds.held = false;
        ds.stuck_since = 0;
    }
    const float walk_sp = o.walk_speed > 0 ? std::min(o.walk_speed, sp) : sp;
    if (o.variant != 1) {
        if (!o.speed_after_goal)
            game::set_speed(p, walk_sp);
        net::Vec3 hd{vel.x / sp, vel.y / sp, vel.z / sp};
        float dot = hd.x * ds.goal_dir.x + hd.y * ds.goal_dir.y + hd.z * ds.goal_dir.z;
        if (!was || (now - ds.last_cmd > o.min_gap && dot < o.turn_dot) || now - ds.last_cmd > o.max_gap) {
            game::move_to(p, {pos.x + hd.x * o.goal_ahead, pos.y + hd.y * o.goal_ahead, pos.z + hd.z * o.goal_ahead}, 0.3f);
            ds.goal_dir = hd;
            ds.last_cmd = now;
            ds.goals++;
        }
        if (o.speed_after_goal)
            game::set_speed(p, walk_sp);
    } else
        *(net::Vec3*)((uint8_t*)p + game::kVelOff) = vel;
    net::Vec3 cur = game::position(p);
    float err = net::dist(cur, pos);
    double dt = was && now > ds.last_step ? std::min(now - ds.last_step, o.max_dt) : 1.0 / 60;
    if (o.keep_lost && o.variant != 3) {
        bool lost = was && net::dist(cur, ds.placed_to) > std::max(0.5f, 2.f * (sp + o.catch_up) * (float)dt);
        if (was && err > kWalkLostErr && err > ds.last_err + 0.005f) {
            if (ds.stuck_since <= 0)
                ds.stuck_since = now;
            lost = lost || now - ds.stuck_since > kWalkLostAfter;
        } else if (err <= kWalkLostErr)
            ds.stuck_since = 0;
        if (lost)
            ds.held = true;
        else if (err < 0.2f)
            ds.held = false;
        ds.place_lost += ds.held;
    }
    ds.last_err = err;
    // (only when the path itself jumped this frame: a proxy thrown back with its NPC by a hit trails it by
    // more than snap at 30 fps over the player network and was teleported 2.4-4 m, npc_walk_player)
    if (o.snap > 0 && err > o.snap && net::dist(ds.path.prev, pos_in) < 0.01f &&
        speed(ds.path.v) * (float)(now - ds.last_step) > 0.5f * o.snap) {
        game::teleport(p, pos, rot);
        ds.teleports++;
        ds.placed_to = pos;
        ds.last_step = now;
        ds.track = Track{};
        return;
    }
    float max_step = (sp + o.catch_up) * (float)dt;
    net::Vec3 to = pos;
    if (err > max_step) {
        float k = max_step / err;
        to = {cur.x + (pos.x - cur.x) * k, cur.y + (pos.y - cur.y) * k, cur.z + (pos.z - cur.z) * k};
    }
    if (o.variant == 2 || ds.held) // held: closed at the same speed, but with the mark (the game keeps it)
        game::teleport(p, to, rot);
    else
        game::place(p, to, rot);
    ds.placed_to = to;
    ds.last_step = now;
    ds.track = Track{};
}

inline void walk_end(void* p, const net::Vec3& pos, State& ds, double now)
{
    if (!ds.kinematic)
        return;
    ds.kinematic = false;
    game::move_to(p, pos, 0.3f);
    ds.last_cmd = now;
}

} // namespace drive
