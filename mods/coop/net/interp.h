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

class SnapshotBuffer {
public:
    double delay = 0.10;       // interpolation delay (s)
    double max_extrap = 0.25;  // extrapolation limit (s)

    void clear()
    {
        snaps_.clear();
        clock_.clear();
        have_raw_ = false;
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

    // Interpolated state at (now - delay) in sender time.
    bool sample(double local_now, PlayerState& out) const
    {
        if (snaps_.empty())
            return false;
        double t = local_now - offset_ - delay;
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
                float f = (float)((t - a.t) / (b.t - a.t));
                out.pos = lerp(a.s.pos, b.s.pos, f);
                out.rot = nlerp(a.s.rot, b.s.rot, f);
                return true;
            }
        }
        // extrapolate from the newest snapshot
        const auto& n = snaps_.back();
        double dt = t - n.t;
        if (dt > max_extrap)
            dt = max_extrap;
        out = n.s;
        out.pos.x += n.s.vel.x * (float)dt;
        out.pos.y += n.s.vel.y * (float)dt;
        out.pos.z += n.s.vel.z * (float)dt;
        return true;
    }

private:
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
