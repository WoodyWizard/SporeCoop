// Native tests for cell_logic.h (make test): the decisions of the Cell stage sync. Each case runs
// with the fix on (must hold) and off (must show the bug), so the test is known to catch it.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

#include "cell_logic.h"
#include "view_centres.h"

using namespace cell_logic;

static int g_fail = 0;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                             \
        }                                                         \
    } while (0)

// BUG-0083: slot == highWater is not a live slot, and nothing past the capacity is read
static void slots()
{
    CHECK(slot_in_pool(0, 10, 16, true));
    CHECK(slot_in_pool(9, 10, 16, true));
    CHECK(!slot_in_pool(10, 10, 16, true));
    CHECK(!slot_in_pool(16, 16, 16, true)); // full pool: one object past the allocation
    CHECK(!slot_in_pool(12, 20, 12, true)); // a corrupt highWater above the capacity
    CHECK(slot_in_pool(10, 10, 16, false)); // the old bound let it through
    CHECK(slot_in_pool(16, 16, 16, false));
}

// BUG-0032: a lost or late chunk must not let the removal pass run on half a round
static void rounds()
{
    for (bool fix : {true, false}) {
        Rounds r;
        // round 1: 3 chunks, chunk 0 lost
        CHECK(r.on_chunk(1, 1, fix) == Rounds::NEW_ROUND);
        CHECK(r.on_chunk(1, 2, fix) == Rounds::SAME_ROUND);
        CHECK(r.complete(3, fix) == !fix); // old: removal ran, the 14 cells of chunk 0 were deleted
        // round 2 complete
        CHECK(r.on_chunk(2, 0, fix) == Rounds::NEW_ROUND);
        r.on_chunk(2, 1, fix);
        r.on_chunk(2, 2, fix);
        CHECK(r.complete(3, true) || !fix);
        // round 3 starts, then the last chunk of round 2 comes late (reordered)
        CHECK(r.on_chunk(3, 0, fix) == Rounds::NEW_ROUND);
        Rounds::Verdict late = r.on_chunk(2, 2, fix);
        CHECK(fix ? late == Rounds::OLD_CHUNK : late == Rounds::NEW_ROUND); // old: round 2 "again", seen = 1 chunk
        if (fix) { // round 3 goes on undisturbed
            CHECK(r.on_chunk(3, 1, fix) == Rounds::SAME_ROUND);
            CHECK(r.on_chunk(3, 2, fix) == Rounds::SAME_ROUND);
            CHECK(r.complete(3, fix));
        }
    }
    // wrap-around of the 16-bit round number, and a host that starts counting again
    Rounds r;
    r.on_chunk(0xFFFF, 0, true);
    CHECK(r.on_chunk(0, 0, true) == Rounds::NEW_ROUND);
    CHECK(r.on_chunk(0xFFFF, 0, true) == Rounds::OLD_CHUNK);
    r.on_chunk(5000, 0, true);
    CHECK(r.on_chunk(1, 0, true) == Rounds::NEW_ROUND); // far behind: a new session
    CHECK(r.on_chunk(1, 0, true) == Rounds::SAME_ROUND); // a duplicate
}

// BUG-0051: T = 45. The host eats 2 (47, sent), the guest eats 10 (55 here, level 1 at 50).
// The host's 47 arrives before the host counted the guest's meal.
static void meals()
{
    for (bool fix : {true, false}) {
        Meals m;
        int local = 55;
        uint32_t meal = m.sent(10);
        int d = m.diff(47, true, 0, 0, local, fix); // the total without our meal (ack 0)
        local += d;
        CHECK(fix ? local == 57 : local == 47); // old: below the level line, a level down
        d = m.diff(57, true, meal, 0, local, fix); // the host counted it
        local += d;
        CHECK(local == 57);
    }
    // a refused meal (eaten first by somebody else, eats_late): acknowledged without points
    Meals m;
    uint32_t meal = m.sent(10);
    int local = 55;
    local += m.diff(45, true, meal, 0, local, true);
    CHECK(local == 45);
    // not sent yet (this frame's meal) counts as on the way, an older host without the ack does not
    Meals p;
    CHECK(p.diff(45, true, 0, 4, 49, true) == 0);
    CHECK(p.diff(45, false, 0, 4, 49, true) == -4);
    // meals with no points (a kill) get numbers but are not waited for
    Meals k;
    k.sent(0);
    CHECK(k.unacked.empty());
}

// BUG-0219: a 0.7.2 host sends its total without the ack field: the guest's list of meals on the
// way grew by one per meal for the whole session (10,000 meals = 10,000 entries)
static void meals_old_host()
{
    for (bool fix : {true, false}) {
        Meals m;
        for (int i = 0; i < 10000; i++) {
            m.sent(5);
            m.diff(5 * (i + 1), false, 0, 0, 5 * (i + 1), fix);
        }
        printf("  meals with a host without acks (foodseq %s, meals_trim %s): unacked=%zu\n", fix ? "on" : "off",
               g_meals_trim ? "on" : "off", m.unacked.size());
        CHECK(m.unacked.empty());
    }
}

// BUG-0041: a small cell (0.5 of size 2 -> 1) eats 41 points over a level line; the game resizes
// it to the new level's size 3 inside AddFoodPoints
static void body()
{
    Body b = regrow(1.f, 3.f, 0.5f, 41, true);
    CHECK(b.factor == 1.f && std::fabs(b.target - 3.f) < 1e-4f);
    Body old = regrow(1.f, 3.f, 0.5f, 41, false);
    CHECK(old.target > 5.f); // 3 / 0.5 * 0.91 = 5.46: almost twice the level's size
    // a meal within the level: grows by 0.01 per point
    Body g = regrow(1.f, 1.f, 0.5f, 10, true);
    CHECK(std::fabs(g.factor - 0.6f) < 1e-4f && std::fabs(g.target - 1.2f) < 1e-4f);
}

static void ko()
{
    // BUG-0061: a knockout nobody refreshes ends (left 0: the 2 s floor)
    CHECK(!ko_stale(10.0, 9.0, 0.0, true));
    CHECK(ko_stale(10.0, 7.5, 0.0, true));
    CHECK(!ko_stale(100.0, 7.5, 0.0, false)); // kostale off: never
    // BUG-0238: one late refresh with 7 s still to run does not end it; past its own deadline it does
    CHECK(!ko_stale(12.5, 10.0, 7.0, true));
    CHECK(ko_stale(19.5, 10.0, 7.0, true));
    CHECK(ko_stale(12.5, 10.0, 7.0, true, false)); // grace off: ended after 2 s (the bug)
    CHECK(ko_stale(40.0, 10.0, 1e9, true));        // a bad `left` is capped (25.5 s)
    CHECK(ko_stale(12.5, 10.0, std::nan(""), true) && !ko_stale(11.5, 10.0, std::nan(""), true)); // NaN: the 2 s floor
}

// BUG-0239: the host acknowledged nothing while it was in an editor; now it acknowledges the meal
// it cannot count (as for a meal eaten first): the guest is back at the host's total at once
static void meals_away()
{
    for (bool fix : {true, false}) {
        Meals m;
        int local = 45 + 10;
        uint32_t dropped = m.sent(10);                         // the host is away: never applied
        uint32_t ack = fix ? dropped : 0;                      // the host's ack after its return
        local += m.diff(45, true, ack, 0, local, true);        // its next total, no points from us
        CHECK(fix ? local == 45 : local == 55);
        uint32_t next = m.sent(1);                             // a later meal, applied: host 46
        local += 1;
        int before = local;
        local += m.diff(46, true, next, 0, local, true);
        CHECK(local == 46);
        CHECK(fix ? before - local == 0 : before - local == 10); // old: the dip of 10 at this meal
    }
}

// BUG-0160: a unit quaternion from any matrix, hp 0..255 on the wire, no arrow at a NaN position
static float qnorm(const Quat& q) { return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); }
static void numerics()
{
    float zero[9] = {0};
    CHECK(std::fabs(qnorm(mat_to_quat(zero, true)) - 1.f) < 1e-5f);
    CHECK(mat_to_quat(zero, true).w == 1.f);                    // identity
    CHECK(std::fabs(qnorm(mat_to_quat(zero, false)) - 0.5f) < 1e-5f); // old: (0, 0, 0.5, 0)
    float half[9] = {0.5f, 0, 0, 0, 0.5f, 0, 0, 0, 0.5f};          // a scaled rotation
    CHECK(std::fabs(qnorm(mat_to_quat(half, true)) - 1.f) < 1e-5f);
    for (int i = 0; i < 64; i++) { // rotations about z and x: unchanged by the fix (already unit)
        float a = i * 0.1f, c = std::cos(a), s = std::sin(a);
        float rz[9] = {c, -s, 0, s, c, 0, 0, 0, 1}, rx[9] = {1, 0, 0, 0, c, -s, 0, s, c};
        for (float* m : {rz, rx}) {
            Quat f = mat_to_quat(m, true), o = mat_to_quat(m, false);
            CHECK(std::fabs(qnorm(f) - 1.f) < 1e-4f);
            CHECK(std::fabs(f.x - o.x) + std::fabs(f.y - o.y) + std::fabs(f.z - o.z) + std::fabs(f.w - o.w) < 1e-3f);
        }
    }
    CHECK(hp_byte(-1, true) == 0);
    CHECK(hp_byte(300, true) == 255);
    CHECK(hp_byte(42, true) == 42);
    CHECK(hp_byte(-1, false) == 255); // old: a dead cell's -1 arrived as full health
    float x = 0, y = 0;
    float nan = std::nanf("");
    CHECK(!edge_point(960, 540, nan, nan, 70, 50, 1, 1, x, y, true));
    CHECK(edge_point(960, 540, nan, nan, 70, 50, 1, 1, x, y, false) && !(std::fabs(x) < 1e7f)); // old: x = cx + NaN * 1e9
    CHECK(edge_point(960, 540, 5000, 10, 70, 50, 1, 1, x, y, true) && x >= 0 && x <= 1920 && y >= 0 && y <= 1080);
    CHECK(std::fabs(x - 1850) < 1); // the right edge less the margin
}

// BUG-0093: coop.cell spawn's slot argument stays inside the pool
static void spawn_slot()
{
    uint32_t v = 0;
    CHECK(parse_cell_slot("3", 10, &v) && v == 3);
    CHECK(parse_cell_slot("9", 10, &v) && v == 9);
    CHECK(!parse_cell_slot("10", 10, &v)); // highWater itself is not live
    CHECK(!parse_cell_slot("99999", 10, &v));
    CHECK(!parse_cell_slot("-1", 10, &v));
    CHECK(!parse_cell_slot("0", 10, &v));
    CHECK(!parse_cell_slot("3x", 10, &v));
    CHECK(!parse_cell_slot("", 10, &v));
    CHECK(!parse_cell_slot("4096", 5000, &v)); // a corrupt highWater: never past 4095
    CHECK(!parse_cell_slot("99999999999", 5000, &v));
}

// BUG-0174: the tile keys of PopulateTiles' sorted lists and the host's view centres
static void partner_tiles()
{
    CHECK(tile_key(0, 2, 3, 4) == 0x00020304);
    CHECK(tile_key(1, 0, -1, -2) == 0x0100FFFE);
    CHECK(tile_key(0, 1, -1, 0) < tile_key(0, 2, 0, 0)); // sorted by level before the coordinates
    int32_t l[] = {tile_key(0, 1, 0, 0), 7, tile_key(0, 1, 0, 1), 9, tile_key(0, 1, 5, -3), 11, tile_key(1, 1, 0, 0), 13};
    for (int i = 0; i < 4; i++)
        CHECK(tile_list_has(l, l + 8, l[i * 2]));
    CHECK(!tile_list_has(l, l + 8, tile_key(0, 1, 0, 2)));
    CHECK(!tile_list_has(l, l, l[0]));
    CHECK(!tile_list_has(nullptr, nullptr, 0));
    views::Set c;
    CHECK(c.add(2, {100, 0, 0}));
    c.set_host({0, 0, 0});
    CHECK(c.size() == 2 && c.list()[0].player == 0 && c.list()[1].player == 2);
    c.set_host({1, 1, 0});
    CHECK(c.size() == 2 && c.list()[0].pos.x == 1);
    CHECK(c.add(2, {50, 0, 0}) && c.size() == 2 && c.find(2)->pos.x == 50);
    CHECK(c.near_any(45, 0, 6) && !c.near_any(25, 0, 6));
    float nan = std::nanf("");
    CHECK(!c.add(3, {nan, 0, 0}) && !c.find(3)); // BUG-0292: a NaN partner is no centre
    CHECK(!c.add(3, {1e9f, 0, 0}));
    for (int i = 3; i < 20; i++)
        c.add((uint8_t)i, {(float)i, 0, 0});
    CHECK(c.size() == views::kMaxCentres); // bounded
    views::Box b{-20, -15, 20, 15};
    views::Box m = b.centred(100, 50);
    CHECK(m.x0 == 80 && m.x1 == 120 && m.y0 == 35 && m.y1 == 65);
}

// BUG-0632 / BUG-0636 (class BUG-0420): an event on one cell is acted on once within the ttl
static void once()
{
    for (bool fix : {true, false}) {
        Once o(5.0);
        CHECK(o.first(7, 0.0, fix));
        CHECK(o.first(7, 0.6, fix) == !fix); // the second break 0.6 s later: refused with the fix
        CHECK(o.first(8, 0.6, fix));         // another cell is its own
        CHECK(o.first(7, 5.1, fix));         // after the ttl the index may come again (a reused slot)
    }
    Once o(5.0);
    o.first(1, 0.0, true);
    CHECK(o.has(1, 1.0) && !o.has(1, 6.0));
    o.drop(1);
    CHECK(!o.has(1, 1.0));
    for (uint32_t k = 0; k < 600; k++)
        o.first(k, (double)k, true);
    CHECK(o.at.size() <= 512); // bounded: old entries pruned
}

// BUG-0959: the host's trace word for a guest's kill: a knocked-out guest's kill is refused, never "applied"
static void kill_word_ko()
{
    CHECK(!strcmp(kill_word(false, true, false), "applied"));
    CHECK(!strcmp(kill_word(false, true, true), "refused (knocked out)"));
    CHECK(!strcmp(kill_word(true, true, true), "refused (killed here already)"));
    CHECK(!strcmp(kill_word(false, false, true), "refused (gone or dying here)"));
}

// BUG-0792: 3-4 players placed around the host: one spot each, at the radius, the host at the anchor
static void player_spawn_slots()
{
    float x[4], y[4];
    for (int id = 0; id < 4; id++)
        player_spawn_offset(id, 4.f, &x[id], &y[id]);
    CHECK(x[0] == 0.f && y[0] == 0.f);
    for (int a = 1; a < 4; a++) {
        CHECK(std::fabs(std::hypot(x[a], y[a]) - 4.f) < 1e-3f);
        for (int b = a + 1; b < 4; b++)
            CHECK(std::hypot(x[a] - x[b], y[a] - y[b]) > 6.f); // 4 x sqrt(3) apart
    }
    float u, v;
    player_spawn_offset(1, NAN, &u, &v);
    CHECK(std::isfinite(u) && std::fabs(u - 4.f) < 1e-3f);
    player_spawn_offset(2, 100.f, &u, &v);
    CHECK(std::hypot(u, v) <= 20.001f);
    player_spawn_offset(4, 4.f, &u, &v); // a fifth player: the next ring, not on guest 1's spot
    CHECK(std::hypot(u - x[1], v - y[1]) > 1.f);
}

// BUG-0975: the active set of a pass right after a fall of three levels does not fit the game's buffer; settled it does
static void active_set()
{
    const float sizes[17] = {10, 30, 100, 300, 1000, 3000, 1e4f, 3e4f, 1e5f, 3e5f, 1e6f, 3e6f, 1e7f, 3e7f, 1e8f, 3e8f, 1e9f};
    const float vb[12] = {-10, -7.5f, 0, 10, 7.5f, 0, -20, -15, 0, 20, 15, 0}; // ViewBoxes about the origin
    CHECK(level_of_food(0) == 1 && level_of_food(99) == 1 && level_of_food(327) == 4 && level_of_food(1000) == 10);
    double settled1 = active_tiles(1, 0, 1.f, vb, sizes, 17), settled4 = active_tiles(4, 327, 30.f, vb, sizes, 17);
    CHECK(settled1 < kActiveTileCap && settled4 < kActiveTileCap);
    CHECK(active_tiles(1, 0, 30.f, vb, sizes, 17) > 511); // 327 -> 0 with the level 4 scale: the crash
    CHECK(active_tiles(1, 0, NAN, vb, sizes, 17) > 511);  // garbage: no pass
}


// BUG-1309: the host's Cell state over a 512 kbit/s link (a queue of 400 ms that drops what does not fit, as
// tools/tests/netem.py's rate cap): 400 cells, the guest's nearest are the first indices of the "pool" order
// reversed (the old order sends them last). Per second of the steady state: how many of the nearest 100 cells
// reached the guest. Old: whole rounds in pool order every 0.1 s, the link keeps the first ~7 chunks of each.
static void link_pace()
{
    struct Link { // bytes/s, the time its queue is free again
        double rate = 64000, free_at = 0, queue = 0.4;
        uint32_t sent = 0, lost = 0;
        bool pass(double now, size_t bytes)
        {
            sent++;
            double depart = std::max(now, free_at) + bytes / rate;
            if (depart - now > queue) {
                lost++;
                return false;
            }
            free_at = depart;
            return true;
        }
    };
    for (bool fix : {true, false}) {
        Link link;
        LinkPace pace;
        const int kCells = 400, per = 14;
        std::vector<int> round; // cell ids; id < 100: the guest's nearest
        size_t next = 0, chunks = 0;
        uint64_t cell_bytes = 0;
        int near_seen_min = kCells;
        std::vector<double> seen(kCells, -1);
        for (int tick = 0; tick < 300; tick++) { // 30 s
            double now = tick * 0.1;
            if (fix) {
                pace.observe(now, link.sent, link.lost, cell_bytes);
                pace.refill(0.1);
                if (next >= chunks) {
                    size_t n = pace.round_cells(kCells);
                    round.clear();
                    for (size_t i = 0; i < n; i++)
                        round.push_back((int)i); // nearest first
                    next = 0;
                    chunks = (round.size() + per - 1) / per;
                }
                while (next < chunks && pace.may_send()) {
                    size_t k = next++, bytes = 9 + 63 * std::min<size_t>(per, round.size() - k * per) + 35;
                    cell_bytes += bytes;
                    pace.spent(bytes);
                    if (link.pass(now, bytes))
                        for (size_t i = k * per; i < std::min(round.size(), k * per + per); i++)
                            seen[round[i]] = now;
                }
            } else {
                for (int k = 0; k * per < kCells; k++) { // pool order: the nearest are the last indices
                    size_t bytes = 9 + 63 * per + 35;
                    if (link.pass(now + k * 1e-4, bytes))
                        for (int i = k * per; i < std::min(kCells, k * per + per); i++)
                            seen[kCells - 1 - i] = now;
                }
            }
            link.pass(now + 0.05, 120); // the player's own state and the reliable messages
            if (tick % 10 == 9 && tick >= 100) { // a second of the steady state (from 10 s on)
                int near = 0;
                for (int i = 0; i < 100; i++)
                    near += seen[i] > now - 1.0; // listed within the last second: the guest keeps its mirror
                near_seen_min = std::min(near_seen_min, near);
            }
        }
        printf("  link_pace %s: nearest 100 seen per second at least %d, rate %.0f B/s, cuts %u, raises %u, lost %u of %u\n", fix ? "paced" : "old",
               near_seen_min, pace.limited() ? pace.rate : -1.0, pace.cuts, pace.raises, link.lost, link.sent);
        if (fix) {
            CHECK(near_seen_min == 100); // every one of the nearest 100, every second
            CHECK(pace.limited() && pace.rate < 64000 && pace.rate > 20000);
            CHECK(pace.cuts >= 1);
        } else
            CHECK(near_seen_min < 30); // the old order: the nearest came last and the link dropped them
    }
    // a lossy link that is not full (3 % random loss) keeps whole rounds: never cut
    LinkPace p;
    uint32_t sent = 0, lost = 0;
    for (int t = 0; t < 100; t++) {
        sent += 100;
        lost += 3;
        p.observe(t * 0.1, sent, lost, (uint64_t)t * 26000);
    }
    CHECK(!p.limited() && p.cuts == 0);
    // after a cut the rate grows back when the loss is gone, and is unlimited above the ceiling
    LinkPace q;
    q.observe(0, 0, 0, 0);
    q.observe(0.5, 300, 210, 130000); // 70 % lost of 260 KB/s
    CHECK(q.limited() && q.rate > 60000 && q.rate < 70000);
    double r0 = q.rate;
    uint32_t s = 300;
    uint64_t b = 130000;
    for (int k = 2; k < 200 && q.limited(); k++) {
        s += 100;
        b += (uint64_t)(q.rate * 0.5); // the state used its rate
        q.observe(k * 0.5, s, 210, b);
    }
    CHECK(!q.limited() && q.raises > 0 && r0 > 0);
    CHECK(q.round_cells(900) == 900);
    LinkPace f;
    f.rate = 13200; // 13.2 KB/s: 80 cells in 0.4 s
    CHECK(f.round_cells(400) == 80 && f.round_cells(30) == 30);
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--old-trim"))
            g_meals_trim = false; // the 0.7.3.0 code: meals_old_host must fail
    slots();
    spawn_slot();
    player_spawn_slots();
    rounds();
    meals();
    meals_old_host();
    body();
    ko();
    meals_away();
    numerics();
    partner_tiles();
    once();
    kill_word_ko();
    active_set();
    link_pace();
    if (g_fail) {
        printf("cell_tests: %d FAILED\n", g_fail);
        return 1;
    }
    printf("cell_tests: all passed\n");
    return 0;
}
