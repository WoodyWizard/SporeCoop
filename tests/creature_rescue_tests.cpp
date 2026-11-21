// BUG-9283: the rules of a downed creature in the Creature stage (mods/coop/creature_rescue_logic.h)
#include <cmath>
#include <cstdio>

#include "creature_rescue_logic.h"

static int g_fails = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                \
            g_fails++;                                                                                                 \
        }                                                                                                              \
    } while (0)

using namespace creature_rescue;

static void test_down()
{
    std::printf("a lethal hit downs the own creature only with a partner here\n");
    DownIn in;
    in.fix = in.creature = in.connected = in.partner_here = true;
    in.damage = 30;
    in.health = 30;
    CHECK(should_down(in));
    in.damage = 29.9f;
    CHECK(!should_down(in)); // not lethal: the hit lands
    in.damage = 1e6f;
    CHECK(should_down(in));
    DownIn x = in;
    x.partner_here = false;
    CHECK(!should_down(x)); // alone: the game's death (or the pack's one-off rescue)
    x = in;
    x.fix = false;
    CHECK(!should_down(x));
    x = in;
    x.creature = false;
    CHECK(!should_down(x));
    x = in;
    x.connected = false;
    CHECK(!should_down(x));
    x = in;
    x.health = 0;
    CHECK(!should_down(x)); // dead already
    x = in;
    x.damage = NAN;
    CHECK(!should_down(x));
    CHECK(down_seconds(true) == 60 && down_seconds(false) == 30);
}

static void test_window()
{
    std::printf("the window: a revive after 1.5 s within reach, the timeout, the give-up\n");
    const double win = 60;
    Watch w;
    CHECK(begin_ok(w, 1));
    CHECK(!begin_ok(w, 0));
    w = {1, true, 100, -1, false};
    CHECK(!begin_ok(w, 2)); // one at a time
    CHECK(step(w, false, 110, win) == Step::Waiting);
    CHECK(step(w, true, 120, win) == Step::Waiting);
    CHECK(revive_progress(w, 120.75) > 0.49f && revive_progress(w, 120.75) < 0.51f);
    CHECK(step(w, true, 121.4, win) == Step::Waiting);
    CHECK(step(w, true, 121.5, win) == Step::Revived);
    // a partner who walks away starts over
    w = {2, true, 100, -1, false};
    CHECK(step(w, true, 110, win) == Step::Waiting);
    CHECK(step(w, false, 111, win) == Step::Waiting);
    CHECK(w.near_since < 0);
    CHECK(step(w, true, 111.2, win) == Step::Waiting);
    CHECK(step(w, true, 112.0, win) == Step::Waiting);
    CHECK(step(w, true, 112.7, win) == Step::Revived);
    // nobody came
    w = {3, true, 100, -1, false};
    CHECK(step(w, false, 159.9, win) == Step::Waiting);
    CHECK(step(w, false, 160.0, win) == Step::Expired);
    CHECK(seconds_left(w, 130, win) == 30);
    // a revive that would end after the window's end does not count
    w = {4, true, 100, -1, false};
    CHECK(step(w, true, 159.0, win) == Step::Waiting);
    CHECK(step(w, true, 160.6, win) == Step::Expired);
    // the give-up ends it at once, even with a partner near
    w = {5, true, 100, -1, false};
    w.gave_up = true;
    CHECK(step(w, true, 101, win) == Step::Expired);
    // after the verdict: an older or repeated request of the same knockout starts nothing
    w.active = false;
    CHECK(!begin_ok(w, 5));
    CHECK(!begin_ok(w, 4));
    CHECK(begin_ok(w, 6));
    CHECK(begin_ok(Watch{0xFFFFFFFFu, false}, 1)); // the counter wraps
}

static void test_wipe()
{
    std::printf("the party wipe: everybody down for 2 s\n");
    double since = -1;
    CHECK(!wipe_due(since, false, false, 10)); // nobody down
    CHECK(!wipe_due(since, true, true, 10));   // someone stands
    CHECK(since < 0);
    CHECK(!wipe_due(since, true, false, 10));
    CHECK(!wipe_due(since, true, false, 12));
    CHECK(wipe_due(since, true, false, 12.1));
    CHECK(since < 0);
    CHECK(!wipe_due(since, true, false, 20));
    CHECK(!wipe_due(since, true, true, 21)); // a partner got up meanwhile: the clock starts over
    CHECK(!wipe_due(since, true, false, 22));
    CHECK(!wipe_due(since, true, false, 23.5));
    CHECK(wipe_due(since, true, false, 24.5));
}

static void test_dna()
{
    std::printf("the DNA a knockout costs: 10 %%, rounded up, never more than the DNA\n");
    CHECK(dna_cost(100) == 10);
    CHECK(dna_cost(101) == 11);
    CHECK(dna_cost(5) == 1);
    CHECK(dna_cost(1) == 1);
    CHECK(dna_cost(0.5f) == 0);
    CHECK(dna_cost(0) == 0);
    CHECK(dna_cost(NAN) == 0);
    CHECK(dna_cost(-3) == 0);
}

static void test_wire()
{
    std::printf("K_LINES round trip and malformed input\n");
    std::vector<Line> lines = {{0, 600, 0}, {2, 123, 55}}, back;
    net::Writer w;
    write_lines(w, lines);
    net::Reader r(w.data.data(), w.data.size());
    CHECK(read_lines(r, back) && back == lines);
    net::Writer e;
    write_lines(e, {});
    net::Reader re(e.data.data(), e.data.size());
    CHECK(read_lines(re, back) && back.empty());
    // a bad player, too many tenths, a revive over 100, a short buffer, a trailing byte
    for (int bad = 0; bad < 5; bad++) {
        net::Writer b;
        b.u8(1);
        b.u8(bad == 0 ? 4 : 1);
        b.u16(bad == 1 ? 601 : 10);
        if (bad != 3)
            b.u8(bad == 2 ? 101 : 50);
        if (bad == 4)
            b.u8(0);
        net::Reader rb(b.data.data(), b.data.size());
        CHECK(!read_lines(rb, back));
    }
    net::Writer many;
    many.u8(5);
    net::Reader rm(many.data.data(), many.data.size());
    CHECK(!read_lines(rm, back));
    CHECK(tenths_left(59.95) == 600 && tenths_left(0.01) == 1 && tenths_left(-1) == 0 && tenths_left(1e9) == kMaxTenths);
}

int main()
{
    test_down();
    test_window();
    test_wipe();
    test_dna();
    test_wire();
    if (g_fails) {
        std::printf("creature_rescue_tests: %d FAILED\n", g_fails);
        return 1;
    }
    std::printf("creature_rescue_tests: all passed\n");
    return 0;
}
