// Snapshot interpolation buffer for remote player states (portable).
#pragma once

#include <algorithm>
#include <array>
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
// And no lead at all over a link whose one-way latency (median of the last kJitterN states) is over kJitterHigh
// (until under kJitterLow): there the lead extrapolates 0.2-0.3 s, every change of speed between two states moved the
// target and the mirror went stop-and-go whatever the closing (docs/re/tribe.md#tribe-lead-smooth); shown
// interpolated as 0.7.8 (smooth, late). Only buffers with smooth_max > 0 (the Tribe member mirrors). Off: M1's lead.
inline bool g_lead_smooth_fix = true;
constexpr double kSmoothTau = 0.12; // s: e^-1 of a lead jump left after this
constexpr float kSmoothRate = 3.f;  // m/s on top of kSmoothK x the body's speed
constexpr float kSmoothK = 1.f;
constexpr size_t kJitterN = 64;      // states (3-6 s)
constexpr double kJitterHigh = 0.10; // s: no lead over this one-way latency (measured: netem mobile 123-149 ms, player and local under 0.1)
constexpr double kJitterLow = 0.085;
constexpr double kSmoothVelTau = 0.1; // s
// BUG-0650 (`coop.fix drive.lead_late`): a late state is brought in over time, not at once. The lead shows the
// sender's body at the sender's time now (+ one-way); a state that arrives late (a walk that starts in a latency spike
// of the player network, +250 ms: 0.3-0.4 s after it was sent) moved that target 3.6-3.8 m in one frame from where the
// standing state had it, and catch_prop closed it in 0.87-0.95 m frames (tribe_walk_player). With the fix the shown
// target has its own sender time (lt_t_): it goes on at the real time's rate and, when a new state arrives, may step
// back to as early as the state before the newest: of that span the time whose target is nearest where the shown target
// was going (a state as expected, a walk at the same velocity: the lead's own time, no change; the first state of a
// start: the path from the last standing state, no start step). Then it catches up with the lead at 1 + kLateRate x the real time, at most
// kLateExtra m/s over the body's speed (the target and the velocity the mirror walks at run at that rate meanwhile). Member mirrors only (smooth_max > 0).
// Off: M1's lead (the target at the sender's time now at once).
inline bool g_lead_late_fix = true;
constexpr double kLateRate = 0.5;
// and never more than this over the body's own speed (m/s): caught up at 1.5 x 11 m/s the target ran 16.5 m/s and the
// mirror's walk + catch_prop on top drew 0.94-0.96 m frames (tribe_walk_player 20261106-212040 #2, member 7 at 34.8 s)
constexpr double kLateExtra = 3.0;
constexpr int kLateSteps = 16; // candidate times between the earliest and the shown time's continuation
// BUG-0410, BUG-0651 (K3, `coop.fix drive.lead_keep`): the member lead stays on up to a 130 ms one-way latency and a
// late state is caught up at up to twice the body's speed. In 0.7.9 the lead went off over a one-way latency over 100 ms (BUG-0619's
// rule against the mobile network's stop-and-go, from before drive.lead_late): the player network measures 80-101 ms and
// a loaded guest 110-150 ms on any link (the round trip counts both games' frame times), so the members were shown
// interpolated, 0.2 s + latency late, for whole runs (body_trail_tribe_player p90 3.2-3.4 m, coop.bodyrec diag rows,
// lab 20261107-073841 #1, #3), and where the lead was on, drive.lead_late's catch-up at 1.33x (+3 m/s at 9 m/s) kept the
// shown target 0.25-0.3 s behind for 0.75 s after every start known late (a latency spike, a loss burst): p90 2.36 m
// against M1's raw lead 1.20 m (-073841 #2). drive.lead_late brings any late state in without a step, which is what
// the no-lead rule was for, but over the mobile network (120 ms, 60 ms jitter) the lead kept on drew stop-and-go and
// 0.9 m jumps again (tribe_walk_mobile 1 of 3 and 1 of 3 against 3 of 3 with the rule, lab 20261107-094647,
// -095652-2 / -095652), and the latency the rule reads does not part that link from a loaded guest (mobile 109-126 ms
// at the switch, the player network under load 100-150 ms). With the fix the rule's line is kKeepHigh (130 ms, back
// under kKeepLow 115 ms) instead of 100 / 85 ms: the player network and a guest under a moderate load keep the lead,
// the mobile network and a heavily loaded guest do not (BUG-0651 stays for those); the late target catches up at
// 1 + kKeepRate x the real time (a mirror walking at that rate draws at most 2 x its speed + catch_up: tribe_walk's jump
// is 3 x the top speed), and the mirror is walked at the target's rate with its drawn step held to 2 x its own speed +
// catch_up (driver.h WalkPlace::lead_speed: the 16.5 m/s + catch-up frames of BUG-0650). And a new state steps the shown time back only as far as its
// news was late: only when the lead had to go kLateNews s further than usual (the median of the last kHorizonN) past
// the state before it when it came. drive.lead_late let every new state step back towards where the shown target was going and
// never forward, so any speed change, turn or send jitter that made the body faster than the lead had it stepped it
// back, one that made it slower did not step it on: with the lead kept on the shown target was still 0.07 s (p50) to
// 0.23 s (p90) behind M1's raw lead, p90 2.12 m against the raw lead's 1.65 m (lab 20261107-080850 #1). A start in a
// spike or after states lost or dropped as old (0.25-0.4 s over the usual) is brought in as before. Off: 0.7.9.
inline bool g_lead_keep_fix = true;
constexpr double kKeepRate = 1.0;
constexpr double kKeepTau = 0.25; // s
constexpr double kKeepHigh = 0.13, kKeepLow = 0.115; // s: the no-lead rule's one-way latency line with the fix
constexpr size_t kHorizonN = 32;
constexpr size_t kHorizonLongN = 64; // BUG-1093: the puppets' knee (20 states/s: 3.2 s)
constexpr double kLateNews = 0.05; // s
constexpr float kLateLost = 1.5f;  // m
// BUG-0674 (K4, `coop.fix drive.lead_fall`): a body is moved on past its newest state at its velocity only as far as
// its states usually come (the median lead horizon at the last kHorizonN arrivals, + kFallMargin); further (a latency
// spike, a loss burst, no state for 0.2-0.6 s) the extrapolation slows down exponentially (kFallTau: at most kFallTau x
// the speed more). Moved on at the full speed for up to kMaxLead s, a body that turned round or stopped in the gap was
// shown going on the old way: 2 x speed x gap off (Tribe members 8-9 m at 9-12 m/s, lab 20261107-102038; walked NPC
// proxies teleported 2.5-5.6 m, npc_walk_tribe 20261107-112016; a Space ship copy 11.4 m past a ship that braked,
// space_fast_sync). The cost: a straight walk in a spike is shown up to (gap - horizon - kFallTau) x speed behind and
// caught up when the states come (drive.lead_late). One rule for every remote body that is moved on: the lead of the
// snapshot buffers (fall_time) and VelocityEstimate::at (Civ vehicles, Space ships). Off: 0.7.10.
inline bool g_lead_fall_fix = true;
constexpr double kFallMargin = 0.04; // s past the usual horizon at the full speed
constexpr double kFallTau = 0.12;    // s
// the time a body is moved on for after `dt` s past its newest state, with the usual horizon `knee` (s), falling with
// `tau`; `on`: the rule's switch (drive.lead_fall, or drive.puppet_fall for the partners' puppets)
inline double fall_time(double dt, double knee, double tau = kFallTau, bool on = g_lead_fall_fix)
{
    if (!on || dt <= knee)
        return dt;
    return knee + tau * (1.0 - std::exp(-(dt - knee) / tau));
}
// its rate (d fall_time / d dt): the share of the velocity the moved-on body still has
inline float fall_rate(double dt, double knee, double tau = kFallTau, bool on = g_lead_fall_fix)
{
    if (!on || dt <= knee)
        return 1.f;
    return (float)std::exp(-(dt - knee) / tau);
}
// BUG-0680 (K4, `coop.fix drive.puppet_fall`): the same for a buffer shown interpolated (the partners' puppets, delay
// 0.1 s): past its newest state (a state later than the delay: a spike) it was moved on at the state's velocity for up to
// max_extrap 0.25 s, and a partner that stopped in a spike was shown walking on: the target stepped back 0.72 m at the
// stop's news and the puppet stood 0.33-0.86 m past where the partner stopped (puppet_stops, lab 20261107-132459 #1,
// 1 stop in 10-14). With the fix it is moved on at the full speed for kPuppetFallMargin s only, then falling with
// kPuppetFallTau, its velocity too (creature.puppet_stop then holds it where the target slows down). Off: 0.7.10.
inline bool g_puppet_fall_fix = true;
// BUG-1095 (`coop.fix drive.air_keep`): not for a state in the air (PF_JUMPING). A partner in the air at 9-10 m/s whose
// states came 0.2-0.3 s late was slowed to 0.1-1.8 m/s past each newest state: its puppet (placed on the target in the
// air) stalled in mid-air and was put 0.9-1.1 m on when the next state came (puppet_stops "no jump of the puppet while it
// walks", lab 20261113-203423 #4, 20261113-211453 #1). In the air it is moved on at the state's velocity up to max_extrap
// as before BUG-0680 (its agl goes with it: drive.agl_extrap, BUG-1094). Off: 0.7.13.
inline bool g_puppet_air_keep_fix = true;
constexpr double kPuppetFallMargin = 0.02, kPuppetFallTau = 0.05; // s
// BUG-1092 (`coop.fix drive.agl_lerp`): a state between two snapshots took the newer one's height over the sender's
// terrain (agl) with a position blended between them: at a hop's start (the partner put 1.5 m up between two states)
// the target rose over 2-3 frames but drive::at_sender_agl put the partner's puppet at the newer state's full 1.5 m in
// the first of them (the puppet's step 1.5 m in one 15 ms frame, puppet_air "no jump of the puppet while it walks", lab
// 20261113-185217). The agl is blended with the position (a state without agl: its height over the newer state's
// ground, 0 m on the ground; a state already up but not yet marked airborne at its height). Off: 0.7.13.
inline bool g_agl_lerp_fix = true;
// BUG-1094 (`coop.fix drive.agl_extrap`): a state moved on past the newest snapshot (the lead of the host's NPCs, the
// interpolation's extrapolation, the smoothing's offset) kept the newest state's height over the sender's terrain (agl)
// while its position went on at the state's velocity, and drive::at_sender_agl places a body in the air by that agl: a
// thrown NPC flying up at 9.8 m/s was put back to the same height in every frame of a state period and stepped 0.6-1.1 m
// when the next state came (npc_walk "a thrown proxy flies with its NPC", stood in 128 of 166 frames, lab
// 20261113-221929 #2). The agl goes with the position's radial move. Off: 0.7.13.
inline bool g_agl_extrap_fix = true;
// the agl of `out` after its position was moved from one at radius r0 (BUG-1094)
inline void agl_follow(PlayerState& out, float r0)
{
    if (g_agl_extrap_fix && agl_known(out.agl) && r0 > 1.f)
        out.agl += dist(Vec3{}, out.pos) - r0;
}
// BUG-1093 (`coop.fix net.send_phase`): the own state goes out every kStatePeriod s. Sent at the first frame at least
// kStatePeriod after the last send, a game whose frames are a little shorter than that (48 ms at 21 fps, 40 ms at 25 fps)
// sent every other frame: states 80-100 ms apart, half the rate (the lab's host at 21 fps: 91-95 ms). The partner's puppet
// (interpolated 0.1 s back) then ran past its newest state in 17-26 % of the frames, its usual horizon (the fall's knee)
// grew to 0.04-0.06 s, and a stop known late was shown walking on: the target stepped back 0.6-0.7 m and the puppet stood
// 0.37-0.66 m past the stop (puppet_stops, lab 20261113-193854, 20261113-204729 #2). With the fix the schedule keeps its
// phase (the next send is due kStatePeriod after the last one was due, not after the frame that sent it; after a hitch of
// over two periods it starts again from now), so the rate stays 1 / kStatePeriod down to that frame rate, and a creature
// that walked (over 1 m/s in the last state sent) and stands now (under 0.3 m/s) is sent at once: the stop's news does
// not wait up to a period. Off: 0.7.13 (the first frame kStatePeriod after the last send).
inline bool g_send_phase_fix = true;
constexpr double kStatePeriod = 0.05; // s
// whether the own state goes out at `now`; `last`: when the last one was due (updated when one goes out); `sent_sp` the
// speed in the last state sent, `sp` the creature's speed now (m/s, 0 0 outside Creature)
inline bool state_due(double now, double& last, float sent_sp = 0, float sp = 0, double period = kStatePeriod)
{
    if (g_send_phase_fix && sent_sp > 1.f && sp < 0.3f) {
        last = now; // a stop: at once, the schedule from here
        return true;
    }
    if (now - last < period)
        return false;
    last = g_send_phase_fix && now - last < 2 * period ? last + period : now;
    return true;
}
// the median of the last N horizons (how far past the newest state a body was moved on when the next state came)
template <size_t N> struct HorizonMedian {
    std::array<float, N> h{};
    size_t n = 0;
    float med = 0;
    void clear() { n = 0; med = 0; }
    void add(double v)
    {
        h[n++ % N] = (float)v;
        const size_t k = std::min(n, N);
        std::array<float, N> d = h;
        std::nth_element(d.begin(), d.begin() + k / 2, d.begin() + k);
        med = d[k / 2];
    }
    // the knee for fall_time: `extra` s over the median (never under 0); none (1e9) before 8 horizons
    double knee(double extra) const { return n >= 8 ? std::max(0.0, (double)med) + extra : 1e9; }
};
// BUG-0675 (K4, `coop.fix drive.lead_throw`): a path between the two newest states faster than 15 m/s is a real move,
// not the body put somewhere, when the state's own velocity says the same (kThrowAgree): a pushed NPC thrown at 13-31 m/s
// reports that speed (the host's coop.npctrack rows: path 19.6-20.4 m/s, reported 19.4-20.4). Taken as put somewhere,
// the lead stood at each newest state and the copy stood 3-6 frames, then stepped 2 m (body_trail_hits over the player
// network: over 2 m 0.88-0.96 s, max 6-7.4 m). Off: 0.7.10 (no lead over a 15 m/s path).
inline bool g_lead_throw_fix = true;
constexpr float kThrowAgree = 0.35f; // the state's speed within this share of the path's, the directions within ~20 deg
// And a step under kPutStep is never a put: under a lab load of ~30 the host's state times are off its positions by up to
// a frame, two states 10-20 ms apart in its time carry a 33 ms walk, and a 9-10 m/s member read as 12-18 m/s: its lead
// stood at the newest state for 0.4 s while the member walked on (6 m off, body_trail_tribe_player lab 20261108-005832,
// t3 at 3.2-3.6 s). A real put (an egg's hatchling, a respawn, the game's teleport) goes further.
constexpr float kPutStep = 1.0f; // m
// BUG-0651 (K4, `coop.fix drive.lead_low`): the no-lead rule reads the link's own latency, not the mean round trip. The
// mean (Connection::rtt_ms, an average of the last ~10 samples) follows every latency spike of the player network for
// 0.5-1 s and both games' frame times (a sample waits for the host's frame that reads it and the guest's that reads the
// ack): the player network read 139 ms at a lab load of ~27 and the members were shown 0.2 s + latency late for the rest
// of the run (body_trail_tribe_player p90 2.11 m, lab 20261108-003907-2). With the fix the rule reads g_one_way_low:
// half of the lower quartile of the last 128 round-trip samples, which spikes (10 % of the time) and frame waits (their
// lower part) leave out; the mobile network's 120 ms each way stays in it. Its own line kLowHigh / kLowLow. Off: 0.7.10.
inline bool g_lead_low_fix = true;
inline double g_one_way_low = -1; // s, -1 = no samples yet (set by the session every frame)
constexpr double kLowHigh = 0.08, kLowLow = 0.07; // s

class SnapshotBuffer {
public:
    double delay = 0.10;       // interpolation delay (s)
    bool lead = false;         // BUG-0425: shown where the sender has it now (g_lead_fix)
    // tests (BUG-0974): with the lead, shown this many s behind where the lead rule puts it (a planted late mirror;
    // `delay` is not used by the lead, so `coop.tribe delay` did nothing since BUG-0425)
    double test_lag = 0;
    double max_extrap = 0.25;  // extrapolation limit (s)
    float smooth_max = 0;      // BUG-0619: a lead jump up to this (m) is smoothed (g_lead_smooth_fix); 0 = never
    // BUG-0499: > 0: no blend across a step between two snapshots faster than this (m/s): the sender's
    // creature was put somewhere else (the hatchling out of its egg 5-11 m, a respawn at the nest), not a
    // walk; nor across the death flag (the dead state at the place of the death, the next one at the nest).
    // Blended, the puppet's target slid the whole way in 0.1 s and the puppet walked after it (0 = off)
    float jump_speed = 0;
    // BUG-0680: the extrapolation past the newest state falls off (g_puppet_fall_fix); the partners' puppets
    bool interp_fall = false;
    // BUG-0674: a late state is brought in over time (drive.lead_late's late_time) without smooth_max: the NPC proxies.
    // A walk that started in a spike was known 0.3-0.5 s late and the proxy jumped 2.5-6 m in one frame
    // (npc_walk_tribe over the player network, lab 20261108-011449 #2: proxy 19 at 36.9 s, 2.95 m)
    bool late_lead = false;
    // BUG-0674: the Tribe member mirrors' rules (the no-lead rule, the smoothing after a switch); a proxy (late_lead)
    // with smooth_max > 0 has every lead jump up to smooth_max smoothed instead (g_lead_fall_fix)
    bool member_rules() const { return smooth_max > 0 && !late_lead; }
    bool proxy_smooth() const { return late_lead && g_lead_fall_fix && smooth_max > 0; }
    // BUG-0519 (cell.puppet_scale): blend two states of different Cell world scales in one scale
    static inline bool scale_lerp = true;
    // diagnostics (BUG-0388): how far past its newest snapshot the last sample() ran (s, before the
    // max_extrap cap), 0 = it interpolated
    mutable double last_extrap = 0;

    void clear()
    {
        jitter_ = 0;
        dev_n_ = 0;
        jittery_ = false;
        snaps_.clear();
        clock_.clear();
        have_raw_ = false;
        sm_have_ = false;
        lt_have_ = false;
        age_n_ = 0;
        hz_.clear();
        hz_long_.clear();
        lt_excess_ = 0;
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
        // BUG-0619: the link's one-way latency, the median over the last kJitterN states (a latency spike of the
        // player network, +250 ms for 0.7 s every 7 s, stays out of it). The states' arrival lateness did not part
        // the networks in the lab: its median (a loaded local link 39-51 ms, mobile 51-62 ms), median + one-way
        // (player 150 ms, mobile 156-193 ms) and IQR (local at lab load 10-15: 34-106 ms, mobile 45-103 ms; the
        // guest's own frame times dominate it) (lab 20261106-164638, -165647, -170617)
        {
            // BUG-0651 (g_lead_low_fix): the link's own latency (the round trip's lower quartile)
            const bool low = g_lead_low_fix && g_one_way_low >= 0;
            devs_[dev_n_++ % kJitterN] = (float)(low ? g_one_way_low : g_one_way);
            if (dev_n_ >= 32) {
                size_t n = std::min<size_t>(dev_n_, kJitterN);
                std::array<float, kJitterN> d = devs_;
                std::nth_element(d.begin(), d.begin() + n / 2, d.begin() + n);
                jitter_ = d[n / 2];
                // BUG-0651 (g_lead_keep_fix): the rule's line moved over what the player network and a moderately loaded
                // guest read (80-115 ms); the mobile network (110-150 ms) stays under it
                double hi = g_lead_keep_fix ? kKeepHigh : kJitterHigh, lo = g_lead_keep_fix ? kKeepLow : kJitterLow;
                if (low)
                    hi = kLowHigh, lo = kLowLow;
                jittery_ = jitter_ > hi || (jittery_ && jitter_ > lo);
            }
        }
        // BUG-0410 (g_lead_keep_fix): how much further than usual the lead had to go past the state before this one
        if (!snaps_.empty()) {
            const double h = local_now - offset_ + g_one_way - snaps_.back().t;
            hz_.add(h);
            hz_long_.add(h);
            lt_excess_ = std::max(0.0, h - (double)hz_.med);
        }
        age_n_++; // BUG-0650: a new state for late_time
        lt_new_ = true;
        snaps_.push_back({st, s});
        while (snaps_.size() > 64)
            snaps_.pop_front();
    }

    // diagnostics (BUG-1093, coop.track): the knee of the interpolated extrapolation's fall (s past the newest state at
    // the full speed) and the median arrival horizon less the one-way latency
    double fall_knee() const
    {
        return interp_knee();
    }
    double horizon_med() const { return hz_.n ? (double)(g_send_phase_fix ? hz_long_.med : hz_.med) - g_one_way : 0; }
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
                // BUG-0675 (g_lead_throw_fix): not when the step is short (kPutStep: the two states' times are off by
                // a loaded sender's frame) or its own velocity says it moves that fast that way (a throw)
                bool put = ps > 15.f || (jump_speed > 0 && ps > jump_speed);
                if (put && g_lead_throw_fix && !(jump_speed > 0 && ps > jump_speed)) {
                    const bool agree = sn > 15.f && std::fabs(sn - ps) <= kThrowAgree * ps &&
                                       pv.x * n.s.vel.x + pv.y * n.s.vel.y + pv.z * n.s.vel.z >= 0.94f * ps * sn;
                    if (agree || ps * (float)dt < kPutStep)
                        put = false;
                }
                if (put)
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
        sm_used_ = sm_lead_out_ = false;
        bool ok = sample_raw(local_now, out);
        // BUG-0619: a sample before the newest state (the path velocity's sample 0.1 s back) is shifted by the same
        // offset, so the path is the shown one (an unshifted one gave the walk a 3-4 m/s path while it stood)
        // A slow body's interpolated state (see sample_raw) goes through the same smoothing: the switch between the
        // two is a jump of the raw target that the offset takes
        if (ok && !sm_lead_out_ && lead && g_lead_fix && g_lead_smooth_fix && (member_rules() || proxy_smooth())) {
            const float r0 = dist(Vec3{}, out.pos);
            smooth(local_now, out, out.vel, false);
            agl_follow(out, r0);
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
            double tl = local_now - offset_ + g_one_way;
            const Snap& n = snaps_.back();
            // BUG-0674 (g_lead_fall_fix): the walked NPC proxies' late states are brought in over time too (late_lead)
            const bool late = g_lead_late_fix && ((g_lead_smooth_fix && member_rules() && !jittery_) || (late_lead && g_lead_fall_fix));
            if (late)
                tl = late_time(local_now, tl);
            tl -= test_lag; // (tests, BUG-0974)
            // BUG-0619: no lead over a jittery link (jitter_ over kJitterHigh until it is under kJitterLow again): the lead
            // moves a state on by its age, and states 0-120 ms late (mobile: 60 ms jitter) made the lead target step
            // 1-4 m at every start, speed change and stop; closing that at any speed the mirror can show was the
            // stop-and-go the test sees (uneven 11-22 %; 0.7.8 2-5 %). There it is shown as 0.7.8 (interpolated,
            // smooth, ~0.2 s late); the switch goes through the smoothing
            bool use = true;
            if (g_lead_smooth_fix && member_rules())
                use = !jittery_;
            if (use && tl >= n.t) {
                double dt = tl - n.t;
                last_extrap = dt;
                // BUG-0674 (g_lead_fall_fix): past the usual horizon at a falling speed
                const double knee = hz_.knee(kFallMargin);
                const float fr = dt > kMaxLead ? 1.f : fall_rate(dt, knee);
                dt = std::min(fall_time(dt, knee), kMaxLead);
                Vec3 v = lead_velocity();
                out = n.s;
                out.pos.x += v.x * (float)dt;
                out.pos.y += v.y * (float)dt;
                out.pos.z += v.z * (float)dt;
                v = {v.x * fr, v.y * fr, v.z * fr};
                out.vel = v;
                if (late && lt_rate_ > 1.f) // BUG-0650: catching up: the target runs at that rate x the body's speed
                    out.vel = {v.x * lt_rate_, v.y * lt_rate_, v.z * lt_rate_};
                sm_lead_out_ = true;
                if (g_lead_smooth_fix && (member_rules() || proxy_smooth())) {
                    // the lead's own new states as M1's (raw, closed by walk_place's catch_prop: over a link under
                    // kJitterHigh its start step is 1-1.5 m, under a jump); smoothed only after a switch from the
                    // interpolated state until that offset is closed. Smoothed always, the members trailed more over
                    // the player network (body_trail_tribe_player p90 1.47-1.59 m and 1.1-1.8 s over 2 m, M1's
                    // 1.14-1.27 m, lab 20261106-172428 / -172431, -184547 / -184550)
                    // BUG-0674: an NPC proxy's always (its driving teleports onto a target that jumps over snap / 2: a
                    // turn known late jumped the target 2.6-3 m and the proxy with it, npc_walk_tribe 20261108-012638-2)
                    if (!sm_last_lead_ || sm_closing_ || proxy_smooth()) {
                        sm_closing_ = true;
                        smooth(local_now, out, last_extrap > kMaxLead ? Vec3{} : v, true);
                        if (dist(Vec3{}, sm_off_) < 0.05f)
                            sm_closing_ = false;
                    } else if (local_now > sm_t_) {
                        sm_have_ = true;
                        sm_off_ = {};
                        sm_t_ = local_now;
                        sm_vel_ = last_extrap > kMaxLead ? Vec3{} : v;
                        sm_shown_ = out.pos;
                    }
                }
                agl_follow(out, dist(Vec3{}, n.s.pos));
                return true;
            }
            if (use)
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
                if (g_agl_lerp_fix && agl_known(b.s.agl)) {
                    // a state without agl (not marked airborne yet: the hop's first frame can already be up) is at its
                    // own height over b's ground (b's radius less b's agl), not at 0 m
                    const float aa = agl_known(a.s.agl) ? a.s.agl : dist(Vec3{}, ap) - (dist(Vec3{}, b.s.pos) - b.s.agl);
                    out.agl = aa + (b.s.agl - aa) * f;
                }
                return true;
            }
        }
        // extrapolate from the newest snapshot
        const auto& n = snaps_.back();
        double dt = t - n.t;
        last_extrap = dt;
        // BUG-0680 (g_puppet_fall_fix, interp_fall): past the usual horizon (in render time) at a falling speed
        float fr = 1.f;
        // BUG-1095 (g_puppet_air_keep_fix): not for a state in the air (PF_JUMPING): it can not stop there
        if (interp_fall && !(lead && g_lead_fix) && !(g_puppet_air_keep_fix && (n.s.flags & PF_JUMPING))) {
            const double knee = interp_knee();
            fr = dt > max_extrap ? 0.f : fall_rate(dt, knee, kPuppetFallTau, g_puppet_fall_fix);
            dt = fall_time(dt, knee, kPuppetFallTau, g_puppet_fall_fix);
        }
        if (dt > max_extrap)
            dt = max_extrap;
        out = n.s;
        out.pos.x += n.s.vel.x * (float)dt;
        out.pos.y += n.s.vel.y * (float)dt;
        out.pos.z += n.s.vel.z * (float)dt;
        agl_follow(out, dist(Vec3{}, n.s.pos));
        if (fr < 1.f)
            out.vel = {n.s.vel.x * fr, n.s.vel.y * fr, n.s.vel.z * fr};
        return true;
    }

    // BUG-0650: the raw lead's position at sender time `t` (the newest state moved on, or the interpolated path)
    Vec3 lead_at(double t) const
    {
        const Snap& n = snaps_.back();
        if (t >= n.t) {
            double dt = std::min(fall_time(t - n.t, hz_.knee(kFallMargin)), kMaxLead); // (BUG-0674)
            Vec3 v = lead_velocity();
            return {n.s.pos.x + v.x * (float)dt, n.s.pos.y + v.y * (float)dt, n.s.pos.z + v.z * (float)dt};
        }
        for (size_t i = snaps_.size() - 1; i > 0; i--) {
            const Snap& a = snaps_[i - 1];
            const Snap& b = snaps_[i];
            if (t >= a.t) {
                float f = (float)((t - a.t) / std::max(b.t - a.t, 1e-3));
                return lerp(a.s.pos, b.s.pos, f);
            }
        }
        return snaps_.front().s.pos;
    }

    // BUG-0650 (g_lead_late_fix): the sender time the lead shows now (see there); `tl` is M1's (the sender's now).
    // Advanced once per local time; an earlier call (the path velocity's sample) gets the time it had then
    double late_time(double local_now, double tl) const
    {
        if (!lt_have_ || local_now - lt_local_ > 0.5 || age_n_ < 16) {
            lt_have_ = true;
            lt_t_ = tl;
            lt_local_ = local_now;
            lt_pos_ = lead_at(tl);
            lt_vel_ = {};
            lt_new_ = false;
            lt_rate_ = 1.f;
            return tl;
        }
        if (local_now <= lt_local_)
            return std::min(tl, lt_t_ - (lt_local_ - local_now));
        const double adv = local_now - lt_local_;
        const Vec3 lv = lead_velocity();
        // BUG-0410 (g_lead_keep_fix): the rate grows with how far the shown time is behind (kKeepTau s behind: 2x), so a
        // spike's 0.3 s is caught up in ~0.3 s and the mobile network's 20-60 ms of jitter at 1.1-1.2x: a rate that went
        // 1x -> 2x -> 1x with every late state drew stop-and-go there (tribe_walk_mobile uneven 11-15 %, lab 20261107-094647)
        const double rate = g_lead_keep_fix ? 1.0 + std::min(kKeepRate, std::max(0.0, tl - lt_t_) / kKeepTau)
                                            : 1.0 + std::min(kLateRate, kLateExtra / std::max(0.1, (double)dist(Vec3{}, lv)));
        double cont = std::min(tl, lt_t_ + adv);
        double t = std::min(tl, lt_t_ + adv * rate);
        if (lt_new_) {
            // a new state: of [the state before it, the continuation] the time nearest where the shown target was
            // going (the path between the two interpolated: when a latency spike ends, the states sent in it come
            // after newer ones and are dropped; the newest is then the first walking state, 2-3 m from the stand)
            double earliest = snaps_.size() >= 2 ? snaps_[snaps_.size() - 2].t : snaps_.back().t;
            if (g_lead_keep_fix && lt_excess_ < kLateNews) // BUG-0410: news on time: no step back (a frame of slack)
                earliest = std::max(earliest, std::min(cont, tl - 0.01));
            if (cont > earliest + 1e-3) {
                // BUG-0651 (g_lead_keep_fix): going on at the body's own velocity (the lead's), not the shown target's: its
                // catch-up velocity overshot, and after a frame that stepped back (velocity 0) every new state stepped
                // it back again (the target stood every other frame 0.07-0.1 s behind, net_tests test_lead_late)
                const Vec3 wv = g_lead_keep_fix ? lv : lt_vel_;
                Vec3 want{lt_pos_.x + wv.x * (float)adv, lt_pos_.y + wv.y * (float)adv, lt_pos_.z + wv.z * (float)adv};
                double best = cont;
                float bd = dist(lead_at(cont), want);
                for (int i = 0; i < kLateSteps; i++) {
                    double c = earliest + (cont - earliest) * i / kLateSteps;
                    float d = dist(lead_at(c), want);
                    if (d < bd - 0.05f) {
                        bd = d;
                        best = c;
                    }
                }
                t = best < cont ? best : t;
                // BUG-0674 (g_lead_keep_fix): no point of the new path is near where the shown target was going (the lead
                // went on the old way through a spike while the body turned round: 8 m off): continuity is lost anyway, so
                // the shown time is the lead's at once and the body closes the error (catch_prop). Stepped back, the shown
                // time went 0.75 s behind and the mirror 4.7 m behind that target (lab 20261107-110506 #1, member 2)
                if (g_lead_keep_fix && bd > kLateLost)
                    t = tl;
            }
            lt_new_ = false;
        }
        lt_rate_ = (float)std::max(1.0, std::min(rate, (t - lt_t_) / adv));
        Vec3 p = lead_at(t);
        lt_vel_ = {(p.x - lt_pos_.x) / (float)adv, (p.y - lt_pos_.y) / (float)adv, (p.z - lt_pos_.z) / (float)adv};
        if (dist(Vec3{}, lt_vel_) > 15.f)
            lt_vel_ = {};
        lt_pos_ = p;
        lt_t_ = t;
        lt_local_ = local_now;
        return t;
    }

public:
    // diagnostics (coop.bodyrec diag, K3): M1's raw lead target now (the newest state moved on to the sender's now)
    Vec3 raw_lead(double local_now) const { return snaps_.empty() ? Vec3{} : lead_at(local_now - offset_ + g_one_way); }
    double late_behind(double local_now) const { return lt_have_ ? local_now - offset_ + g_one_way - lt_t_ : 0; }
    // diagnostics (BUG-0619): the smoothing's offset now (m) and the raw lead target's jump at the last new state
    double jitter() const { return jitter_; }
    bool jittery() const { return jittery_; }
    // BUG-0651: the member lead is off (interpolated, 0.7.8) because of the link's latency
    bool lead_off() const { return jittery_; }
    // BUG-0651: the late lead target catches up (runs faster than the body) this frame
    bool late_catching() const { return lt_have_ && lt_rate_ > 1.01f; }
    float late_rate() const { return lt_have_ ? std::max(1.f, lt_rate_) : 1.f; }
    float smooth_off() const { return sm_have_ ? dist(Vec3{}, sm_off_) : 0.f; }
    // BUG-0619: true when the last sample(now) was the smoothed lead; `v` then is the shown target's velocity (the
    // lead's plus the offset's decay): the speed the mirror must walk at to stay on it
    bool smooth_velocity(Vec3& v) const
    {
        if (!sm_used_)
            return false;
        v = {sm_vel_.x + sm_offv_.x, sm_vel_.y + sm_offv_.y, sm_vel_.z + sm_offv_.z};
        return true;
    }
    mutable float last_lead_jump = 0;

private:
    // BUG-0619 (g_lead_smooth_fix): `out` (the raw lead at local_now) plus the decaying offset. Advanced once per
    // local time (a later call at the same or an earlier time, the path velocity's sample, adds the same offset)
    // is_lead: `out` is the raw lead (its new states jump: absorbed); else the interpolated state (continuous: an
    // offset left from a switch only closes, and the driving is 0.7.8's, smooth_velocity false)
    void smooth(double local_now, PlayerState& out, const Vec3& v, bool is_lead) const
    {
        sm_used_ = is_lead;
        if (!is_lead) {
            if (local_now > sm_t_ && sm_have_) {
                double dt = local_now - sm_t_;
                if (sm_last_lead_) { // the switch from the lead: its step back taken as an offset
                    Vec3 cont{sm_shown_.x + sm_vel_.x * (float)dt, sm_shown_.y + sm_vel_.y * (float)dt,
                              sm_shown_.z + sm_vel_.z * (float)dt};
                    sm_off_ = {cont.x - out.pos.x, cont.y - out.pos.y, cont.z - out.pos.z};
                    if (dist(Vec3{}, sm_off_) > smooth_max)
                        sm_off_ = {};
                    sm_last_lead_ = false;
                }
                float d = dist(Vec3{}, sm_off_);
                float nd = dt > 0.5 ? 0.f : std::max(0.f, std::max(d * (float)std::exp(-dt / kSmoothTau), d - (kSmoothK * dist(Vec3{}, v) + kSmoothRate) * (float)dt));
                float k = d > 1e-4f ? nd / d : 0.f;
                sm_off_ = {sm_off_.x * k, sm_off_.y * k, sm_off_.z * k};
                sm_t_ = local_now;
                sm_vel_ = v;
                sm_shown_ = {out.pos.x + sm_off_.x, out.pos.y + sm_off_.y, out.pos.z + sm_off_.z};
            } else if (!sm_have_) {
                sm_have_ = true;
                sm_off_ = {};
                sm_t_ = local_now;
                sm_vel_ = v;
                sm_shown_ = out.pos;
            }
            out.pos.x += sm_off_.x;
            out.pos.y += sm_off_.y;
            out.pos.z += sm_off_.z;
            return;
        }
        if (local_now > sm_t_) {
            double dt = local_now - sm_t_;
            sm_offv_ = {};
            bool snap_vel = !sm_have_ || dt > 0.5;
            if (snap_vel) {
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
                    snap_vel = true;
                } else if (d > 1e-4f) {
                    float sp = std::max(dist(Vec3{}, v), dist(Vec3{}, sm_vel_));
                    float rate = kSmoothK * sp + kSmoothRate;
                    float nd = std::max(d * (float)std::exp(-dt / kSmoothTau), d - rate * (float)dt);
                    nd = std::max(nd, 0.f);
                    float k = nd / d;
                    float r = (k - 1.f) / (float)dt;
                    sm_offv_ = {off.x * r, off.y * r, off.z * r};
                    off = {off.x * k, off.y * k, off.z * k};
                }
                sm_off_ = off;
            }
            sm_have_ = true;
            sm_last_lead_ = true;
            sm_t_ = local_now;
            // the shown velocity follows the lead's in kSmoothVelTau (not at once): every state changes the lead's
            // velocity (its speed, capped by the path between two states) and the target's speed stepped 8 -> 1 -> 5 m/s
            // in three frames, which the walked mirror followed a frame late (tribe_walk_mobile: uneven 11-12 %)
            // (at once from a stand, a stop or a real jump)
            if (snap_vel || dist(Vec3{}, sm_vel_) < 0.3f)
                sm_vel_ = v;
            else {
                float b = (float)std::min(1.0, dt / kSmoothVelTau);
                sm_vel_ = {sm_vel_.x + (v.x - sm_vel_.x) * b, sm_vel_.y + (v.y - sm_vel_.y) * b, sm_vel_.z + (v.z - sm_vel_.z) * b};
            }
            sm_shown_ = {out.pos.x + sm_off_.x, out.pos.y + sm_off_.y, out.pos.z + sm_off_.z};
        }
        out.pos.x += sm_off_.x;
        out.pos.y += sm_off_.y;
        out.pos.z += sm_off_.z;
    }
    // BUG-0650
    size_t age_n_ = 0; // states since clear()
    // BUG-0410: the lead horizons at the last kHorizonN states and the newest one's excess over their median (s)
    HorizonMedian<kHorizonN> hz_;
    // BUG-1093 (g_send_phase_fix): the interpolated extrapolation's knee over the last kHorizonLongN horizons: 32 states
    // were 3.2 s at 10 states/s but 1.6 s at 20/s, and a spike of the player network (+250 ms for 0.7 s) then filled half
    // of them: the knee rose to 0.15-0.17 s and a stop in the spike was shown 0.46 m past (lab 20261113-211453 #3)
    HorizonMedian<kHorizonLongN> hz_long_;
    // the knee of the interpolated extrapolation (BUG-0680): the usual render horizon + kPuppetFallMargin
    double interp_knee() const
    {
        const double med = g_send_phase_fix ? (double)hz_long_.med : (double)hz_.med;
        return hz_.n >= 8 ? std::max(0.0, med - g_one_way - delay) + kPuppetFallMargin : kPuppetFallMargin;
    }

    double lt_excess_ = 0;
    mutable bool lt_have_ = false, lt_new_ = false;
    mutable float lt_rate_ = 1.f; // the shown sender time's rate this frame (catching up: 1 + kLateRate)
    mutable double lt_t_ = 0, lt_local_ = 0;
    mutable Vec3 lt_pos_{}, lt_vel_{};
    mutable bool sm_have_ = false, sm_used_ = false, sm_last_lead_ = false, sm_closing_ = false, sm_lead_out_ = false;
    double jitter_ = 0; // s (BUG-0619)
    std::array<float, kJitterN> devs_{};
    size_t dev_n_ = 0;
    bool jittery_ = false;
    mutable double sm_t_ = 0;
    mutable Vec3 sm_off_{}, sm_offv_{}, sm_vel_{}, sm_shown_{};

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
            if (sender >= 0 && sender_s > sender)
                hz.add(now - t); // BUG-0674: how far past the previous one it was moved on (less the latency)
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
    // BUG-0674 (g_lead_fall_fix): past the usual horizon (with sender times) at a falling speed
    HorizonMedian<32> hz;
    Vec3 at(double now, double latency = 0.03) const
    {
        double a0 = std::max(now - t + latency, 0.0);
        if (sender >= 0)
            a0 = fall_time(a0, hz.knee(kFallMargin) + latency);
        float a = (float)std::min(a0, max_ahead);
        return {pos.x + vel.x * a, pos.y + vel.y * a, pos.z + vel.z * a};
    }
};

} // namespace net
