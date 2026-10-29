// Portable decisions of the Cell stage sync (cellsync.cpp, modes.cpp), unit-tested in
// tests/cell_tests.cpp (make test). Each takes the switch of its fix (`coop.pool fix <name> 0|1`):
// false gives the old behaviour, so the unit test shows that it catches the bug.
#pragma once

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <deque>
#include <utility>

namespace cell_logic {

// BUG-0083: a cell pool slot is live only in [0, highWater) and inside the allocation [0, capacity).
// The old test (`slot > highWater`) let slot == highWater through, one object past the used range.
inline bool slot_in_pool(uint32_t slot, uint32_t high_water, uint32_t capacity, bool fix)
{
    if (!fix)
        return slot <= high_water;
    return slot < high_water && slot < capacity;
}

// BUG-0093: the slot argument of `coop.cell spawn <slot>`: a decimal number (no sign, nothing after
// it) of a slot inside the pool's live range [1, highWater) and below 4096. atoi took "99999" or
// "-1" and the command read a model pointer far outside the pool. The caller still checks that the
// slot holds a live cell (its index's low 16 bits are the slot).
inline bool parse_cell_slot(const char* s, uint32_t high_water, uint32_t* out)
{
    if (!s || !*s)
        return false;
    uint32_t v = 0;
    for (const char* p = s; *p; p++) {
        if (*p < '0' || *p > '9' || v > 100000)
            return false;
        v = v * 10 + (uint32_t)(*p - '0');
    }
    if (v == 0 || !slot_in_pool(v, high_water, 4096, true))
        return false;
    *out = v;
    return true;
}

// BUG-0032: the chunks of the host's state rounds (MSG_CELL_STATE {cycle, chunk, chunks}), unreliable.
// The guest removes the mirrors a round did not list only when it saw every chunk of that round; a
// chunk of an older round (reordered) is dropped instead of starting the round again.
struct Rounds {
    uint16_t cycle = 0xFFFF;
    bool started = false;
    std::bitset<256> got;

    enum Verdict { NEW_ROUND, SAME_ROUND, OLD_CHUNK };
    // a chunk arrived: which round it belongs to (the caller clears its "seen" set on NEW_ROUND)
    Verdict on_chunk(uint16_t c, uint8_t chunk, bool fix)
    {
        Verdict v = SAME_ROUND;
        if (!started || c != cycle) {
            // within 64 rounds behind (6 s at 10 Hz): a late chunk. Further back: the host started
            // counting again (a new session), take it as new.
            int16_t d = (int16_t)(uint16_t)(c - cycle);
            if (fix && started && d < 0 && d > -64)
                return OLD_CHUNK;
            cycle = c;
            started = true;
            got.reset();
            v = NEW_ROUND;
        }
        got.set(chunk);
        return v;
    }
    // the last chunk of the round arrived: may the unlisted mirrors be removed?
    bool complete(uint8_t chunks, bool fix) const
    {
        if (!fix)
            return true;
        for (unsigned i = 0; i < chunks; i++)
            if (!got.test(i))
                return false;
        return true;
    }
    void reset() { *this = Rounds{}; }
};

// BUG-0051: the guest's meals sent to the host and not yet in a total the host sent back.
// The host echoes the last meal number it applied (MSG_CELL_FOOD ack); the guest sets its total to
// host total + what is still on the way, instead of the host total alone (which took a fresh meal
// away again until the next total, a level down and up when the meal crossed a level line).
// BUG-0219: a host that sends no ack (0.7.2) never acknowledges a meal: the list is cleared instead
// of growing by one entry per meal for the whole session (also on the off side of pool.foodseq).
// false = the 0.7.3.0 code (coop.fix pool.meals_trim off, cell_tests --old-trim)
inline bool g_meals_trim = true;

struct Meals {
    uint32_t seq = 0;
    std::deque<std::pair<uint32_t, int>> unacked; // meal number, points

    uint32_t sent(int points)
    {
        ++seq;
        if (points)
            unacked.push_back({seq, points});
        return seq;
    }
    // the change the guest applies to its own total for a host total (has_ack: a host that sends it)
    int diff(int host_total, bool has_ack, uint32_t ack, int pending, int local_total, bool fix)
    {
        if (!fix || !has_ack) {
            if (g_meals_trim)
                unacked.clear();
            return host_total - local_total;
        }
        while (!unacked.empty() && (int32_t)(ack - unacked.front().first) >= 0)
            unacked.pop_front();
        int on_way = pending;
        for (auto& m : unacked)
            on_way += m.second;
        return host_total + on_way - local_total;
    }
    void reset() { *this = Meals{}; }
};

// BUG-0041: the avatar's target size after a small respawn, scaled by the body factor (0.5 .. 1).
// `before` / `after`: the target size around the game's AddFoodPoints. A meal that crossed a level
// line made the game resize the avatar to the full size of the new level inside AddFoodPoints: the
// old code took that full size for the small one and divided it by the factor again (up to 2x).
struct Body {
    float factor;  // the new body factor (1: full size, nothing more to do)
    float target;  // the target size to write
};
inline Body regrow(float before, float after, float body, int amount, bool fix)
{
    bool resized = std::abs(after - before) > before * 0.01f;
    if (fix && resized)
        return {1.f, after}; // the game sized it for the new level: full size again
    if (amount <= 0 || body >= 1.f || after <= 0)
        return {body, after};
    float f = std::min(1.f, body + amount * 0.01f);
    float full = after / body;
    return {f, full * f};
}

// BUG-0061: a remote player's "knocked out" is refreshed every 0.25 s by its owner. Without a
// refresh for this long it is stale (the player left, its "up again" was lost).
constexpr double kKoStale = 2.0;
// BUG-0238: one late refresh (a retransmit under loss, a hitch) ended a running knockout after 2 s:
// the partner was drawn "up", counted as a helper, and the next refresh replayed the death effect.
// A knockout cannot outlive its own deadline (`left`, seconds still to run at the last refresh;
// the owner respawns when it runs out), so it is stale only after that plus a grace, never sooner
// than kKoStale. `left` is a byte of tenths (at most 25.5 s). grace off: the 2 s of BUG-0061.
constexpr double kKoGrace = 2.0;
inline bool ko_stale(double now, double refreshed, double left, bool fix, bool grace = true)
{
    if (!fix)
        return false;
    if (!grace)
        return now - refreshed > kKoStale;
    double l = std::clamp(std::isfinite(left) ? left : 0.0, 0.0, 25.5);
    return now - refreshed > std::max(kKoStale, l + kKoGrace);
}

// BUG-0160: a rotation matrix (row-major 3x3, a cell's +0x5C) as a quaternion. A zero or degenerate
// matrix (a cell whose rotation is not written yet) gave (0, 0, 0.5, 0): the other side's
// quat_to_mat drew the cell squashed to half its size. Fixed: the result is normalized, identity
// when there is nothing to normalize.
struct Quat {
    float x, y, z, w;
};
inline Quat mat_to_quat(const float* m, bool fix = true)
{
    if (fix) {
        float f = 0;
        for (int i = 0; i < 9; i++)
            f += m[i] * m[i];
        if (!std::isfinite(f) || f < 1e-6f)
            return {0, 0, 0, 1};
    }
    Quat q;
    float tr = m[0] + m[4] + m[8];
    if (tr > 0) {
        float s = std::sqrt(tr + 1.f) * 2;
        q = {(m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s, 0.25f * s};
    } else if (m[0] > m[4] && m[0] > m[8]) {
        float s = std::sqrt(1.f + m[0] - m[4] - m[8]) * 2;
        q = {0.25f * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s, (m[7] - m[5]) / s};
    } else if (m[4] > m[8]) {
        float s = std::sqrt(1.f + m[4] - m[0] - m[8]) * 2;
        q = {(m[1] + m[3]) / s, 0.25f * s, (m[5] + m[7]) / s, (m[2] - m[6]) / s};
    } else {
        float s = std::sqrt(1.f + m[8] - m[0] - m[4]) * 2;
        q = {(m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25f * s, (m[3] - m[1]) / s};
    }
    if (!fix)
        return q;
    float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!std::isfinite(n) || n < 1e-6f)
        return {0, 0, 0, 1};
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

// BUG-0160: a cell's hp in the state's byte. A negative hp (an overkill bite) went out as 255 and
// the guest's mirror looked fully healthy until the dead flag came.
inline uint8_t hp_byte(int hp, bool fix = true) { return fix ? (uint8_t)std::clamp(hp, 0, 255) : (uint8_t)hp; }

// BUG-0160: where an off-screen marker's arrow goes on the screen edge. (dx, dy) is the target's
// screen position from the centre (cx, cy); (mx, my) the margins; (tx, ty) the smallest |dx| / |dy|
// that counts. A NaN position (world_to_screen of a NaN, a camera not set up) gave k = 1e9 and an
// out-of-range float-to-int conversion. Fixed: false for a non-finite position, the point is
// clamped to the screen.
inline bool edge_point(float cx, float cy, float dx, float dy, float mx, float my, float tx, float ty, float& x, float& y,
                       bool fix = true)
{
    if (fix && (!std::isfinite(dx) || !std::isfinite(dy)))
        return false;
    float k = std::min(std::fabs(dx) > tx ? (cx - mx) / std::fabs(dx) : 1e9f, std::fabs(dy) > ty ? (cy - my) / std::fabs(dy) : 1e9f);
    x = cx + dx * k;
    y = cy + dy * k;
    if (fix) {
        x = std::clamp(x, 0.f, 2 * cx);
        y = std::clamp(y, 0.f, 2 * cy);
    }
    return true;
}

} // namespace cell_logic
