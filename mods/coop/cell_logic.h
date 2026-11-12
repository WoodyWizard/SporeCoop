// Portable decisions of the Cell stage sync (cellsync.cpp, modes.cpp), unit-tested in
// tests/cell_tests.cpp (make test). Each takes the switch of its fix (`coop.pool fix <name> 0|1`):
// false gives the old behaviour, so the unit test shows that it catches the bug.
#pragma once

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
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

// BUG-0174: the key PopulateTiles (0xE7D370) sorts its tile lists by: the layer, the level and the
// tile coordinates as bytes, compared as a signed int (docs/re/cell.md#cell-partner-tiles).
inline int32_t tile_key(int32_t layer, int32_t level, int32_t x, int32_t y)
{
    return (int32_t)(((uint32_t)layer << 24) | (((uint32_t)level & 0xFF) << 16) | (((uint32_t)x & 0xFF) << 8) |
                     ((uint32_t)y & 0xFF));
}

// BUG-0174: is `key` in a game tile list (a sorted vector of {key, tile object index} pairs between
// begin and end)? The host's list and the partners' lists must not populate the same tile twice.
inline bool tile_list_has(const int32_t* begin, const int32_t* end, int32_t key)
{
    if (!begin || !end || end <= begin || ((end - begin) & 1))
        return false;
    std::ptrdiff_t lo = 0, hi = (end - begin) / 2;
    while (lo < hi) {
        std::ptrdiff_t mid = (lo + hi) / 2;
        int32_t k = begin[mid * 2];
        if (k == key)
            return true;
        if (k < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return false;
}

// Class BUG-0420 (a world event on one cell acted on more than once): a once-only ledger of the cells
// a player's game already acted on, by pool index (generation included), for `ttl` seconds.
//  * BUG-0632: a guest's lethal bite (ACT_BREAK / ACT_KILL) arrives 2-3 times while the guest keeps
//    biting the mirror; the host's break does not set the dying flag until its animation ends, so the
//    second one broke the cell again (a second death, a second loot drop). The host applies a kill of
//    an index once, the guest sends it once.
//  * BUG-0636: the guest's mirror of a cell it ate was made again from the next state chunk before the
//    host's state dropped it, and eaten again. The guest keeps a tombstone of the host indices it ate
//    and makes no mirror of them until the host stops listing them (or the ttl ends).
// `first(key, now, fix)`: true the first time (records it); with fix false always true (the old code).
struct Once {
    double ttl = 5.0;
    std::unordered_map<uint32_t, double> at; // key -> when it was acted on

    explicit Once(double t = 5.0) : ttl(t) {}
    bool has(uint32_t key, double now) const
    {
        auto it = at.find(key);
        return it != at.end() && now - it->second < ttl;
    }
    bool first(uint32_t key, double now, bool fix)
    {
        if (!fix)
            return true;
        if (has(key, now))
            return false;
        at[key] = now;
        if (at.size() > 512)
            prune(now);
        return true;
    }
    void drop(uint32_t key) { at.erase(key); }
    void prune(double now)
    {
        for (auto it = at.begin(); it != at.end();)
            it = now - it->second >= ttl ? at.erase(it) : std::next(it);
    }
    void reset() { at.clear(); }
};

// BUG-0792: the spot of session player `id` next to the spawn anchor (the host's cell), so that 3-4 players placed
// after a load or a knockout respawn do not land on one spot: the host (0) at the anchor, guests 1-3 on a ring of
// `radius` at 0, 120 and 240 degrees, later ids on wider rings. Radius clamped to 3..20 (not finite: 4).
inline void player_spawn_offset(int id, float radius, float* dx, float* dy)
{
    if (!(radius == radius) || radius > 1e6f || radius < -1e6f)
        radius = 4.f;
    radius = radius < 3.f ? 3.f : radius > 20.f ? 20.f : radius;
    *dx = *dy = 0.f;
    if (id <= 0)
        return;
    int slot = (id - 1) % 3, ring = (id - 1) / 3;
    float r = radius * (1.f + 0.5f * (float)ring);
    static const float kC[3] = {1.f, -0.5f, -0.5f}, kS[3] = {0.f, 0.8660254f, -0.8660254f};
    *dx = r * kC[slot];
    *dy = r * kS[slot];
}

// BUG-0959: the word of the host's trace line for a guest's kill (MSG_CELL_EAT). A kill of a knocked-out guest is
// refused (BUG-0714 / BUG-0509) and says so; before, it said "applied" and then the next line refused it.
inline const char* kill_word(bool again, bool alive, bool knocked_out)
{
    return again ? "refused (killed here already)"
           : !alive ? "refused (gone or dying here)"
           : knocked_out ? "refused (knocked out)"
                         : "applied";
}

} // namespace cell_logic
