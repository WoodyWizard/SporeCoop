// Snapshot interpolation buffer for remote player states (portable).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>

#include "fixes.h"
#include "protocol.h"

namespace net {

inline Vec3 lerp(const Vec3& a, const Vec3& b, float t)
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

inline Quat nlerp(const Quat& a, Quat b, float t)
{
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) {
        b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w;
    }
    Quat q{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n > 1e-6f) {
        q.x /= n; q.y /= n; q.z /= n; q.w /= n;
    }
    return q;
}

inline float dist(const Vec3& a, const Vec3& b)
{
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Local time minus sender time of the fastest recent snapshots: maps a sender's timestamps onto the
// local clock. A smaller offset (a faster packet) is taken at once. BUG-0133: a larger one used to
// raise it by 0.2 ms per snapshot only (4 ms/s at 20 Hz), so after the latency rose by 300 ms the
// buffers rendered past their newest snapshot (puppets and NPCs in steps) for about 50 s. Now the
// minimum is taken over 1 s buckets of the last 3 s, and the offset follows a higher minimum at
// kRise seconds per second (the render clock runs a little slower, it never jumps back).
class ClockOffset {
public:
    static constexpr double kBucket = 1.0;
    static constexpr int kBuckets = 3;
    static constexpr double kRise = 0.25;

    void clear() { have_ = false; }
    bool have() const { return have_; }
    double value() const { return offset_; }

    void add(double off, double local_now)
    {
        if (!have_) {
            for (auto& b : min_)
                b = off;
            start_ = last_ = local_now;
            offset_ = off;
            have_ = true;
            return;
        }
        if (!g_fix.offset_window) { // the code before BUG-0133
            offset_ = off <= offset_ ? off : (off < offset_ + 0.0002 ? off : offset_ + 0.0002);
            last_ = local_now;
            return;
        }
        if (local_now - start_ >= kBucket * kBuckets || local_now < start_) {
            for (auto& b : min_) // a long silence: the old buckets say nothing about today
                b = off;
            start_ = local_now;
        }
        while (local_now - start_ >= kBucket) {
            for (int i = kBuckets - 1; i > 0; i--)
                min_[i] = min_[i - 1];
            min_[0] = off;
            start_ += kBucket;
        }
        if (off < min_[0])
            min_[0] = off;
        double target = min_[0];
        for (int i = 1; i < kBuckets; i++)
            target = std::min(target, min_[i]);
        double dt = std::min(std::max(local_now - last_, 0.0), 1.0);
        last_ = local_now;
        if (off < offset_)
            offset_ = off; // a faster snapshot: at once
        else if (target > offset_)
            offset_ = std::min(target, offset_ + kRise * dt);
    }

private:
    double min_[kBuckets] = {};
    double start_ = 0, last_ = 0, offset_ = 0;
    bool have_ = false;
};

// BUG-0425 (M1, `coop.fix drive.lead`): the one rule for where a remote body is shown. A guest showed every host
// body interpolated at (now - delay) of the host's clock: the delay (0.10-0.15 s) plus the one-way latency behind
// the host's body (measured with both games' own positions at the same moment, coop_body_trail.py: 0.17-0.21 s,
// 0.8-1.2 m behind a walking or running NPC, the target as far behind as the drawn proxy). With the lead a body
// is shown where the sender has it NOW: its newest state moved on by its velocity for the state's age on the
// sender's clock plus the one-way latency (g_one_way: half the round trip, set by the session every frame),
// at most kMaxLead s ahead of that state (a loss burst: it stands at the end of its lead until the next state).
// The velocity is the state's own while it moves, never faster than its path between the two newest states
// (a herd animal pushed by its mates, one against a rock: its game reports a walk it does not make), the path's
// for a body that moves without a speed of its own (BUG-0394), none once it reported a stop.
// Off: interpolated at now - delay (0.7.8).
inline bool g_lead_fix = true;
inline double g_one_way = 0.03; // s
constexpr double kMaxLead = 0.6; // s ahead of the newest state
// BUG-0619 (`coop.fix drive.lead_smooth`): the lead target does not jump. Every new state moves the lead
// target at once (its age, velocity and the one-way latency change from state to state; over a jittery network,
// 60 ms of jitter, 0.3-1.5 m at a walk's start and end) and the driving caught up the jump within ~0.25 s at up to
// 3x the body's speed: Tribe member mirrors drew 0.9-1.25 m steps per 32 ms frame (tribe_walk_mobile). With the
// fix the shown target is continuous: a new state's jump is kept as an offset to the raw lead (the target goes on
// from where it was shown, with the velocity it had) and the offset decays exponentially (kSmoothTau) at most at
// kSmoothK x the body's speed + kSmoothRate (the target never moves faster than 2x the body + 3 m/s: what the walked
// mirror reaches in a frame, its walk + its speed + catch_up; 1.25x left it behind and it surged, tribe_walk_mobile
// lab 20261106-134346 #2: 0.1 s windows at 1.45x p90). This is the closing
// walk_place's catch_prop did (an error closed in ~0.25 s at most at the speed + 3 m/s on top), measured on the
// target's own path instead of from where the game's walk had put the body (which drew walk + catch-up). An
// offset over the buffer's smooth_max is a real jump (the sender's body was put somewhere): taken at once.
// Only buffers with smooth_max > 0 (the Tribe member mirrors). Off: the raw lead.
inline bool g_lead_smooth_fix = true;
constexpr double kSmoothTau = 0.12; // s: e^-1 of a lead jump left after this
constexpr float kSmoothRate = 3.f;  // m/s on top of kSmoothK x the body's speed
constexpr float kSmoothK = 1.f;

class SnapshotBuffer {
public:
    double delay = 0.10;       // interpolation delay (s)
    bool lead = false;         // BUG-0425: shown where the sender has it now (g_lead_fix)
    double max_extrap = 0.25;  // extrapolation limit (s)
    float smooth_max = 0;      // BUG-0619: a lead jump up to this (m) is smoothed (g_lead_smooth_fix); 0 = never
    // BUG-0499: > 0: no blend across a step between two snapshots faster than this (m/s): the sender's
    // creature was put somewhere else (the hatchling out of its egg 5-11 m, a respawn at the nest), not a
    // walk; nor across the death flag (the dead state at the place of the death, the next one at the nest).
    // Blended, the puppet's target slid the whole way in 0.1 s and the puppet walked after it (0 = off)
    float jump_speed = 0;
    // BUG-0519 (cell.puppet_scale): blend two states of different Cell world scales in one scale
    static inline bool scale_lerp = true;
    // diagnostics (BUG-0388): how far past its newest snapshot the last sample() ran (s, before the
    // max_extrap cap), 0 = it interpolated
    mutable double last_extrap = 0;

    void clear()
    {
        snaps_.clear();
        clock_.clear();
        have_raw_ = false;
        sm_have_ = false;
    }

    void push(const PlayerState& s, double local_now)
    {
        double st;
        if (g_fix.time_wrap) {
            // BUG-0053: time_ms is the sender's 32-bit millisecond clock, it wraps after 49.7 days of
            // uptime; compared as a plain number every snapshot after the wrap was "old" and the
            // puppet froze. Order by the signed difference to the last one and keep a 64-bit sender
            // time. A last time of 0 is the "no time" of an NPC spawn (npc.cpp): any time is newer.
            if (have_raw_ && !snaps_.empty()) {
                int64_t d = last_raw_ == 0 ? (int64_t)s.time_ms : (int64_t)(int32_t)(s.time_ms - last_raw_);
                if (d <= 0)
                    return; // old or duplicate
                ext_ms_ += d;
            } else
                ext_ms_ = s.time_ms;
            have_raw_ = true;
            last_raw_ = s.time_ms;
            st = (double)ext_ms_ / 1000.0;
        } else {
            st = s.time_ms / 1000.0;
            if (!snaps_.empty() && st <= snaps_.back().t)
                return; // old or duplicate
        }
        clock_.add(local_now - st, local_now);
        offset_ = clock_.value();
        snaps_.push_back({st, s});
        while (snaps_.size() > 64)
            snaps_.pop_front();
    }

    bool empty() const { return snaps_.empty(); }
    const PlayerState& latest() const { return snaps_.back().s; }
    double age(double local_now) const
    {
        return snaps_.empty() ? 1e9 : local_now - (snaps_.back().t + offset_);
    }

    // diagnostics: snapshots of the last `span` seconds of sender time before the newest one
    int received_in(double span) const
    {
        int n = 0;
        for (auto it = snaps_.rbegin(); it != snaps_.rend() && snaps_.back().t - it->t < span; ++it)
            n++;
        return n;
    }

    // BUG-0425: the velocity the lead moves the newest state on with (see g_lead_fix)
    Vec3 lead_velocity() const
    {
        const Snap& n = snaps_.back();
        auto len = [](const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
        float sn = len(n.s.vel), ps = -1;
        Vec3 pv{};
        const Snap* pr = snaps_.size() >= 2 ? &snaps_[snaps_.size() - 2] : nullptr;
        if (pr) {
            double dt = n.t - pr->t;
            if (dt > 0.02 && dt < 0.5 && pr->s.mode == n.s.mode && pr->s.world == n.s.world &&
                !((pr->s.flags ^ n.s.flags) & (PF_IN_WORLD | PF_DEAD))) {
                float k = (float)(1.0 / dt);
                pv = {(n.s.pos.x - pr->s.pos.x) * k, (n.s.pos.y - pr->s.pos.y) * k, (n.s.pos.z - pr->s.pos.z) * k};
                ps = len(pv);
                if (ps > 15.f || (jump_speed > 0 && ps > jump_speed))
                    return {}; // the sender's body was put somewhere: no walk to go on with
            }
        }
        if (sn > 0.3f) {
            float cap = ps >= 0 ? ps * 1.25f + 0.3f : sn;
            if (sn > cap) {
                float k = cap / sn;
                return {n.s.vel.x * k, n.s.vel.y * k, n.s.vel.z * k};
            }
            return n.s.vel;
        }
        if (pr && len(pr->s.vel) <= 0.3f && ps > 0.5f)
            return pv; // it moves without a speed of its own (BUG-0394)
        return {};
    }

    // Interpolated state at (now - delay) in sender time; with `lead` (g_lead_fix) the sender's state now.
    bool sample(double local_now, PlayerState& out) const
    {
        sm_used_ = false;
        bool ok = sample_raw(local_now, out);
        // BUG-0619: a sample before the newest state (the path velocity's sample 0.1 s back) is shifted by the same
        // offset, so the path is the shown one (an unshifted one gave the walk a 3-4 m/s path while it stood)
        if (ok && !sm_used_ && lead && g_lead_fix && g_lead_smooth_fix && smooth_max > 0 && sm_have_) {
            out.pos.x += sm_off_.x;
            out.pos.y += sm_off_.y;
            out.pos.z += sm_off_.z;
        }
        return ok;
    }

private:
    bool sample_raw(double local_now, PlayerState& out) const
    {
        if (snaps_.empty())
            return false;
        double t = local_now - offset_ - delay;
        last_extrap = 0;
        if (lead && g_lead_fix) {
            const double tl = local_now - offset_ + g_one_way;
            const Snap& n = snaps_.back();
            if (tl >= n.t) {
                double dt = tl - n.t;
                last_extrap = dt;
                dt = std::min(dt, kMaxLead);
                Vec3 v = lead_velocity();
                out = n.s;
                out.pos.x += v.x * (float)dt;
                out.pos.y += v.y * (float)dt;
                out.pos.z += v.z * (float)dt;
                out.vel = v;
                if (g_lead_smooth_fix && smooth_max > 0)
                    smooth(local_now, out, last_extrap > kMaxLead ? Vec3{} : v);
                return true;
            }
            t = tl; // (a state from the future: the clock offset is late) interpolated there
        }
        if (t <= snaps_.front().t) {
            out = snaps_.front().s;
            return true;
        }
        for (size_t i = 1; i < snaps_.size(); i++) {
            const auto& a = snaps_[i - 1];
            const auto& b = snaps_[i];
            if (t <= b.t) {
                out = b.s;
                // no blend across a gap: a snapshot of another mode / world, or one without the
                // player in the world (pos 0,0,0 then) is no point on the path. Blending from it
                // put a puppet that appeared after the editor, a load or a respawn somewhere
                // between the planet's centre and the surface, and the herd made for it kept that
                // home (crash 0xB88BEA in the herd's land search, docs/re/creature.md 10)
                if (a.s.mode != b.s.mode || a.s.world != b.s.world || ((a.s.flags ^ b.s.flags) & PF_IN_WORLD))
                    return true;
                // BUG-0519: two Cell states of a level change are in different world scales (every
                // position of the sender's world is rescaled about the origin): compare and blend them
                // in one scale, b's
                Vec3 ap = a.s.pos;
                if (scale_lerp && a.s.scale > 0 && b.s.scale > 0 && a.s.scale != b.s.scale) {
                    float k = a.s.scale / b.s.scale;
                    ap = {ap.x * k, ap.y * k, ap.z * k};
                }
                if (jump_speed > 0 && (((a.s.flags ^ b.s.flags) & PF_DEAD) ||
                                       dist(ap, b.s.pos) > jump_speed * (float)std::max(b.t - a.t, 0.01)))
                    return true;
                float f = (float)((t - a.t) / (b.t - a.t));
                out.pos = lerp(ap, b.s.pos, f);
                out.rot = nlerp(a.s.rot, b.s.rot, f);
                return true;
            }
        }
        // extrapolate from the newest snapshot
        const auto& n = snaps_.back();
        double dt = t - n.t;
        last_extrap = dt;
        if (dt > max_extrap)
            dt = max_extrap;
        out = n.s;
        out.pos.x += n.s.vel.x * (float)dt;
        out.pos.y += n.s.vel.y * (float)dt;
        out.pos.z += n.s.vel.z * (float)dt;
        return true;
    }

public:
    // diagnostics (BUG-0619): the smoothing's offset now (m) and the raw lead target's jump at the last new state
    float smooth_off() const { return sm_have_ ? dist(Vec3{}, sm_off_) : 0.f; }
    mutable float last_lead_jump = 0;

private:
    // BUG-0619 (g_lead_smooth_fix): `out` (the raw lead at local_now) plus the decaying offset. Advanced once per
    // local time (a later call at the same or an earlier time, the path velocity's sample, adds the same offset)
    void smooth(double local_now, PlayerState& out, const Vec3& v) const
    {
        sm_used_ = true;
        if (local_now > sm_t_) {
            double dt = local_now - sm_t_;
            if (!sm_have_ || dt > 0.5) {
                sm_off_ = {};
            } else {
                Vec3 cont{sm_shown_.x + sm_vel_.x * (float)dt, sm_shown_.y + sm_vel_.y * (float)dt,
                          sm_shown_.z + sm_vel_.z * (float)dt};
                Vec3 off{cont.x - out.pos.x, cont.y - out.pos.y, cont.z - out.pos.z};
                float d = dist(Vec3{}, off);
                float jump = dist(sm_off_, off);
                if (jump > 0.05f)
                    last_lead_jump = jump;
                // a stop (the newest state stands, the one before walked): the target goes to the stop at once, as
                // the raw lead (the mirror stops there by drive::step); smoothed, an overshoot of the lead slid back
                // at kSmoothRate (3 m/s) and the mirror walked back to its stop
                const bool stop = dist(Vec3{}, v) < 0.3f && dist(Vec3{}, sm_vel_) >= 0.3f;
                if (d > smooth_max || stop) {
                    off = {};
                } else if (d > 1e-4f) {
                    float sp = std::max(dist(Vec3{}, v), dist(Vec3{}, sm_vel_));
                    float nd = std::max(d * (float)std::exp(-dt / kSmoothTau), d - (kSmoothK * sp + kSmoothRate) * (float)dt);
                    nd = std::max(nd, 0.f);
                    float k = nd / d;
                    off = {off.x * k, off.y * k, off.z * k};
                }
                sm_off_ = off;
            }
            sm_have_ = true;
            sm_t_ = local_now;
            sm_vel_ = v;
            sm_shown_ = {out.pos.x + sm_off_.x, out.pos.y + sm_off_.y, out.pos.z + sm_off_.z};
        }
        out.pos.x += sm_off_.x;
        out.pos.y += sm_off_.y;
        out.pos.z += sm_off_.z;
    }
    mutable bool sm_have_ = false, sm_used_ = false;
    mutable double sm_t_ = 0;
    mutable Vec3 sm_off_{}, sm_vel_{}, sm_shown_{};

    struct Snap {
        double t;
        PlayerState s;
    };
    std::deque<Snap> snaps_;
    ClockOffset clock_;
    double offset_ = 0;
    int64_t ext_ms_ = 0;   // the sender time of the newest snapshot, unwrapped (BUG-0053)
    uint32_t last_raw_ = 0;
    bool have_raw_ = false;
};

// BUG-0122: the host's now_ms() after the last entry of a ship / vehicle snapshot chunk (older
// guests ignore it, older hosts do not send it). body = bytes of the entries still to read in r.
// Returns the sender time in seconds, -1 when the chunk has none.
inline double snapshot_time(const Reader& r, size_t body)
{
    if (!r.ok() || r.left() != body + 4)
        return -1;
    uint32_t ms;
    memcpy(&ms, r.cur() + body, 4);
    return ms / 1000.0;
}

// Velocity of a remote object from its snapshots (for objects whose snapshots carry no velocity):
// update() on every snapshot, at(now) = where it should be now (moved on for the snapshot's age).
// sender_s: the sender's time of the snapshot (s), < 0 = unknown (an older host). BUG-0122: with the
// arrival time only, network jitter went straight into the speed (one packet 75 ms late: 270 u/s
// for a ship at 100 u/s, the mirror 22 units ahead; two bunched packets: 40 units). With the
// sender's time the speed is distance over the sender's interval, and the age in at() is in sender
// time mapped onto the local clock (a late snapshot is moved on further, a bunched one is not).
struct VelocityEstimate {
    Vec3 pos{}, vel{};
    double t = 0;          // local time of the last snapshot (its arrival, or its sender time mapped)
    double sender = -1;    // sender time of the last snapshot, -1 = none
    ClockOffset clock;
    // BUG-0186: > 0: a step between two snapshots faster than this (u/s) is a jump, not a speed (the
    // sender's game put the object somewhere; a Civ vehicle 16 m off for 0.4 s after one such step).
    // 0 = only the 200-unit jump. `capped` counts the steps taken as a jump this way.
    float max_speed = 0;
    unsigned capped = 0;
    // BUG-0186: how far ahead of its last snapshot at() moves the object (s). A gap in the states is
    // mostly a hitch of the sender, whose objects stand still meanwhile: a fast Civ air vehicle (25 u/s)
    // was put 10 m past where the host had it 0.4 s after its last state. 0.4 = the old limit.
    double max_ahead = 0.4;
    bool too_fast(const Vec3& v)
    {
        if (max_speed <= 0 || v.x * v.x + v.y * v.y + v.z * v.z <= max_speed * max_speed)
            return false;
        capped++;
        return true;
    }
    void update(const Vec3& p, double now, double sender_s = -1)
    {
        if (sender_s >= 0 && g_fix.sender_time) {
            if (sender >= 0) {
                double dts = sender_s - sender;
                if (dts <= 0 && dts > -60)
                    return; // an older or duplicate snapshot (unreliable messages can overtake)
                if (dts <= 0) { // the sender's clock wrapped (49 days) or restarted: start again
                    clock.clear();
                    vel = {};
                } else if (dts > 0.02 && dts < 1.0) {
                    Vec3 v{(p.x - pos.x) / (float)dts, (p.y - pos.y) / (float)dts, (p.z - pos.z) / (float)dts};
                    bool jump = dist(p, pos) > 200.f || too_fast(v);
                    vel = jump ? Vec3{} : Vec3{vel.x + (v.x - vel.x) * 0.6f, vel.y + (v.y - vel.y) * 0.6f, vel.z + (v.z - vel.z) * 0.6f};
                } else if (dts >= 1.0)
                    vel = {};
            } else
                vel = {};
            clock.add(now - sender_s, now);
            pos = p;
            sender = sender_s;
            t = sender_s + clock.value();
            return;
        }
        sender = -1;
        double dt = now - t;
        if (t > 0 && dt > 0.02 && dt < 1.0) {
            Vec3 v{(p.x - pos.x) / (float)dt, (p.y - pos.y) / (float)dt, (p.z - pos.z) / (float)dt};
            bool jump = dist(p, pos) > 200.f || too_fast(v);
            vel = jump ? Vec3{} : Vec3{vel.x + (v.x - vel.x) * 0.6f, vel.y + (v.y - vel.y) * 0.6f, vel.z + (v.z - vel.z) * 0.6f};
        } else if (dt >= 1.0)
            vel = {};
        pos = p;
        t = now;
    }
    // latency: the one-way delay before the snapshot arrived
    Vec3 at(double now, double latency = 0.03) const
    {
        float a = (float)std::min(std::max(now - t + latency, 0.0), max_ahead);
        return {pos.x + vel.x * a, pos.y + vel.y * a, pos.z + vel.z * a};
    }
};

} // namespace net
