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
#include <unordered_set>
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

// BUG-0975: the number of tiles PopulateTiles 0xE7D370 would write for its active set (0xE5FA80: nine groups,
// layer 0 at levels base+2, +1, 0, -1 from the view box doubled, layer 1 at base+4 .. base from the background
// box, each group the tiles of size tile_sizes[level] / world_scale * step(level - level_of_food) * 0.85 that
// cover the box, 0xE5F610 / 0xE5FA00). The game writes them into a stack buffer of 511 entries without a
// bound: right after a fall of several levels at once (a guest at food 327 joining a host at food 0) the
// world scale G+0x514C is still the old level's (it eases to the new one over ~3 s) and the lowest levels'
// tiles are tiny: thousands of entries, the stack is overwritten up to its top (0xC0000005 at 0x00E5FA50).
// `vb`: Simulator::Cell::ViewBoxes (12 floats), `sizes`: the game's tile size table (>= 16 levels).
inline int level_of_food(int food) { return food >= 1000 ? 10 : food < 0 ? 1 : 1 + food / 100; }
inline double active_tiles(int base, int food, float world_scale, const float* vb, const float* sizes, int n_sizes)
{
    if (!vb || !sizes || !(world_scale > 0.f) || !std::isfinite(world_scale))
        return 1e9;
    for (int i = 0; i < 12; i++)
        if (!std::isfinite(vb[i]))
            return 1e9;
    auto step = [](int d) { // 0xE51FD0
        return d <= -1 ? 1.0 : d == 0 ? 0.5 : d == 1 ? 0.25 : d == 2 ? 0.125 : d == 3 ? 0.0625 : 0.03125;
    };
    int lof = level_of_food(food);
    double total = 0;
    static const int kLayer0[4] = {2, 1, 0, -1}, kLayer1[5] = {4, 3, 2, 1, 0};
    for (int layer = 0; layer < 2; layer++) {
        double x0, y0, x1, y1;
        if (layer == 0) { // the visible box doubled about its centre
            double cx = (vb[0] + vb[3]) * 0.5, cy = (vb[1] + vb[4]) * 0.5, w = vb[3] - vb[0], h = vb[4] - vb[1];
            x0 = cx - w, x1 = cx + w, y0 = cy - h, y1 = cy + h;
        } else {
            x0 = vb[6], y0 = vb[7], x1 = vb[9], y1 = vb[10];
        }
        const int* offs = layer == 0 ? kLayer0 : kLayer1;
        for (int k = 0; k < (layer == 0 ? 4 : 5); k++) {
            int level = base + offs[k];
            if (level < 0 || level >= n_sizes)
                return 1e9;
            double size = (double)sizes[level] / world_scale * step(level - lof) * 0.85;
            if (!(size > 0))
                return 1e9;
            double f = 1.0 / size;
            double nx = std::ceil(f * x1) - std::floor(f * x0), ny = std::ceil(f * y1) - std::floor(f * y0);
            if (nx > 0 && ny > 0)
                total += nx * ny;
        }
    }
    return total;
}
constexpr int kActiveTileCap = 480; // the game's buffer holds 511 (0x27F4 bytes of 20-byte entries)

// BUG-1309: the host's Cell state to one guest, paced to what the guest's link carries. A round of the host's
// state (MSG_CELL_STATE, unreliable chunks of 14 cells, 63 bytes each) went out whole ten times a second: ~400 cells
// at level 1 are ~260 KB/s, and a 512 kbit/s link (64 KB/s) carries a quarter. A link's queue drops what does not
// fit; the chunks of a round go out in pool order every time, so the same first ~7 chunks arrived in every round
// and the rest never: the guest kept ~100 of the host's ~400 cells (spread over the whole pool, not the nearest)
// and removed the others a second after they came (lab evo_screens_switch_slow: "c: 247 missing on the guest").
// Now a guest's round is nearest first, and the host sends it no faster than its link carries:
//  * the rate (bytes/s of cell state) follows the connection's own loss counters (Connection::stats): a window
//    with over kCutLoss of the packets lost is congestion, the rate becomes what of the cell state got through
//    (sent x (1 - loss)) x kCutKeep; a window under kRaiseLoss while the state used its rate raises it by kRaise;
//    above kCeil it is unlimited again (whole rounds every tick, as before). A lossy link that is not full (random
//    loss of a few %) never reaches kCutLoss and keeps whole rounds;
//  * a round lists at most the nearest cells that its rate sends in kRoundSpan (the guest removes a mirror that no
//    complete round listed, or that was not listed for 1 s), and goes out over several ticks within its tokens
//    (a burst of at most kBurst seconds of the rate: a link's queue holds a few hundred ms).
struct LinkPace {
    static constexpr double kUnlimited = 1e12;
    static constexpr double kWindow = 0.5;     // s between two looks at the loss counters
    static constexpr double kCutLoss = 0.15;   // loss fraction of a window that is congestion
    static constexpr double kRaiseLoss = 0.03; // loss fraction under which the rate may grow
    static constexpr double kCutKeep = 0.85;   // the share of what got through that the new rate keeps
    static constexpr double kRaise = 1.15;     // per window without loss
    static constexpr double kHold = 1.5;       // s after a cut without another cut (the loss of before still counts in)
    static constexpr double kFloor = 12e3;     // bytes/s: never slower (a round of the nearest ~70 cells every 0.4 s)
    static constexpr double kCeil = 2e6;       // bytes/s: above this, unlimited
    static constexpr double kRoundSpan = 0.4;  // s: a round is at most this much of the rate
    static constexpr double kBurst = 0.25;     // s of the rate: the tokens a tick may hold
    static constexpr size_t kPerCell = 66;     // bytes on the wire per cell (63 + its share of the chunk and packet headers)

    double rate = kUnlimited;
    double tokens = 0;
    double win_at = -1, hold_until = -1;
    uint32_t win_sent = 0, win_lost = 0;
    uint64_t win_cell = 0;
    uint32_t cuts = 0, raises = 0;
    float last_loss = 0;

    bool limited() const { return rate < kUnlimited; }
    // the connection's packets sent / lost so far and the cell state bytes sent to this guest so far (all
    // monotonic); true when the rate was cut in this call
    bool observe(double now, uint32_t sent, uint32_t lost, uint64_t cell_bytes)
    {
        if (win_at < 0) {
            win_at = now;
            win_sent = sent;
            win_lost = lost;
            win_cell = cell_bytes;
            return false;
        }
        double dt = now - win_at;
        if (dt < kWindow)
            return false;
        uint32_t dp = sent - win_sent, dl = lost - win_lost;
        uint64_t dc = cell_bytes - win_cell;
        win_at = now;
        win_sent = sent;
        win_lost = lost;
        win_cell = cell_bytes;
        if (dp < 15)
            return false; // too few packets to tell
        double loss = std::min(1.0, (double)dl / dp);
        last_loss = (float)loss;
        double cell_rate = dc / dt;
        if (loss > kCutLoss) {
            if (now < hold_until)
                return false;
            double got = cell_rate * (1.0 - loss) * kCutKeep;
            double next = std::max(kFloor, std::min(rate, got));
            hold_until = now + kHold;
            if (next < rate) {
                rate = next;
                tokens = std::min(tokens, rate * kBurst);
                cuts++;
                return true;
            }
            return false;
        }
        if (loss < kRaiseLoss && limited() && cell_rate >= 0.7 * rate) { // the state used its rate: room for more?
            rate *= kRaise;
            raises++;
            if (rate > kCeil)
                rate = kUnlimited;
        }
        return false;
    }
    // a tick of dt s: the bytes it may send
    void refill(double dt)
    {
        if (!limited())
            return;
        tokens = std::min(tokens + rate * std::max(0.0, dt), rate * kBurst);
    }
    // the most cells a new round may list (of `want`)
    size_t round_cells(size_t want) const
    {
        if (!limited())
            return want;
        size_t n = (size_t)(rate * kRoundSpan / kPerCell);
        return std::min(want, std::max<size_t>(14, n));
    }
    // a chunk of `bytes` may go now (unlimited: always; else while the tokens last, one chunk over is fine)
    bool may_send() const { return !limited() || tokens > 0; }
    void spent(size_t bytes)
    {
        if (limited())
            tokens -= (double)bytes;
    }
};

// BUG-1333 (cellsync_kin.cpp): this game's own cell (the kind-5 answer S+0x10) as the population last saw it, and the
// one before. For `window` s after it changed, a partner whose cell is still the previous one is on its way to follow
// the change (a guest without a cell of its own, BUG-0508): its kin get the world's cell, not the one it is leaving.
struct OwnCellFollow {
    uint32_t now_key[3] = {}, prev_key[3] = {};
    double changed_at = -1e9;
    // the population sees `key` as this game's own cell at time t
    void seen(const uint32_t key[3], double t)
    {
        if (key[0] == now_key[0] && key[1] == now_key[1] && key[2] == now_key[2])
            return;
        if (now_key[0]) {
            std::copy(now_key, now_key + 3, prev_key);
            changed_at = t;
        }
        std::copy(key, key + 3, now_key);
    }
    // a partner with cell `k` at time t follows the change (gets the world's kin)
    bool follows(const uint32_t k[3], double t, double window) const
    {
        return prev_key[0] && k[0] == prev_key[0] && k[1] == prev_key[1] && k[2] == prev_key[2] && t - changed_at < window;
    }
};

// ---- the Cell audit fixes of 0.7.17 (BUG-1507..1514): their decisions (tests/cell_tests.cpp audit_0717) ----

// BUG-1507 (CELL-4): a guest's own editor spend the host keeps while it has no Cell game: it goes when the host left
// the Cell stage (no Cell game, no editor), or, back in Cell, when it came in another world
inline bool owed_drop(bool fix, bool cell_or_editor, bool in_cell, uint32_t owed_world, uint32_t world)
{
    return fix && (!cell_or_editor || (in_cell && owed_world && world && owed_world != world));
}

// BUG-1510 (CELL-8): a list's entry of a tile holds its cells (a starved tile never; a shadow neither, with the fix)
inline bool tile_entry_populated(bool starved, bool shadow, bool fix) { return !starved && !(fix && shadow); }

// BUG-1511 (CELL-9): the game's break / swallow states (+0x18C, BreakOrSwallow): such a cell is dying
inline bool breaking_state(int st) { return st == 7 || st == 8 || st == 9; }

// BUG-1512 (CELL-11): the factor that brings a partner's pose (its world scale) into ours; 1 when either is not sane
inline float partner_scale_k(float theirs, float own)
{
    if (!(theirs > 0.f) || !(own > 0.f) || !std::isfinite(theirs) || !std::isfinite(own))
        return 1.f;
    return theirs / own;
}

// BUG-1513 (CELL-13): the flags of one cell of MSG_CELL_STATE: 1 dying, 2 dead, 4 a player's product, 8 the receiver's
// own product (an older guest reads only 1, 2, 4)
inline uint8_t state_flags(bool dying, bool dead, bool product, bool own_product)
{
    return (uint8_t)((dying ? 1 : 0) | (dead ? 2 : 0) | (product ? 4 : 0) | (product && own_product ? 8 : 0));
}

// BUG-1514 (CELL-14): EatFood's shrink in place (0xE7DFDB): level L -> L - 1, target size times sizes[L-1] / sizes[L];
// false: not a one-level shrink of a level of the table (17 levels)
inline bool plant_shrink(int level, int new_level, const float* sizes, float& factor)
{
    if (!sizes || new_level != level - 1 || new_level < 0 || level > 16 || !(sizes[level] > 0.f))
        return false;
    factor = sizes[new_level] / sizes[level];
    return std::isfinite(factor) && factor > 0.f;
}

// BUG-1408: a guest's game made its tiles while its population was the host's (h_Populate refuses the gameplay
// markers while the guest mirrors the host): a guest that starts its own pool (the host away, a Cell world of its own)
// keeps those tile objects, and the game populates a tile only when it makes it, so they stay empty (the own pool held
// only the cells of the few tiles made later at the edge, and lost them all when those tiles went). The tiles whose
// population was refused are noted; the guest's own pool populates them again when it starts with none of the host's
// cells (no mirror released, or another world), a few per tile pass.
struct RefusedTiles {
    std::unordered_set<uint32_t> idx; // tile objects (pool G+0x38 indices) made while the population was refused

    // one PopulateTile call ended: `refused` when a gameplay marker of it was refused, else the tile has its own cells
    void on_populated(uint32_t tile, bool refused)
    {
        if (refused)
            idx.insert(tile);
        else
            idx.erase(tile);
    }
    bool has(uint32_t tile) const { return idx.count(tile) != 0; }
    // drop the indices `alive` refuses (tile objects freed since; the pool reuses slots)
    template <class Alive> void prune(Alive alive)
    {
        for (auto it = idx.begin(); it != idx.end();)
            it = alive(*it) ? std::next(it) : idx.erase(it);
    }
};

// BUG-1408: the solo start refills the refused tiles when the guest's pool holds none of the host's cells: no mirror
// became a cell of its own game (released 0), or it plays another Cell world (the load dropped the host's)
inline bool solo_refill_due(bool fix, uint32_t released, bool elsewhere) { return fix && (released == 0 || elsewhere); }

// BUG-1408: how many refused tiles one tile pass populates: at most `per_pass`, none at or above the pool budget
inline uint32_t solo_refill_take(uint32_t pending, uint32_t per_pass, uint32_t live, uint32_t cap)
{
    if (live >= cap)
        return 0;
    return std::min(pending, per_pass);
}

} // namespace cell_logic
