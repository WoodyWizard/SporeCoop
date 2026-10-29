// Native tests for cell_logic.h (make test): the decisions of the Cell stage sync. Each case runs
// with the fix on (must hold) and off (must show the bug), so the test is known to catch it.
#include <cmath>
#include <cstdio>

#include "cell_logic.h"

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

int main()
{
    slots();
    spawn_slot();
    rounds();
    meals();
    body();
    ko();
    meals_away();
    numerics();
    if (g_fail) {
        printf("cell_tests: %d FAILED\n", g_fail);
        return 1;
    }
    printf("cell_tests: all passed\n");
    return 0;
}
