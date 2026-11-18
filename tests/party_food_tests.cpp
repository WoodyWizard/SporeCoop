// BUG-9154: the party's food share (mods/coop/party_food.h), owner decision 2P.
#include "party_food.h"

#include <cmath>
#include <cstdio>

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

static bool near(double a, double b) { return std::fabs(a - b) <= 1e-9; }

int main()
{
    using namespace party_food;
    // a big delivery: a quarter split, the gatherer keeps three quarters
    Split s = split(40, 1);
    CHECK(near(s.keep, 30) && near(s.each, 10));
    s = split(40, 3);
    CHECK(near(s.keep, 30) && near(s.each, 10.0 / 3));
    CHECK(near(created(40, 3), 0));
    // the owner's minimum: a quarter of 2 split three ways is 0.17 each, every partner still gets 1
    s = split(2, 3);
    CHECK(near(s.keep, 1.5) && near(s.each, 1));
    CHECK(near(created(2, 3), 2.5)); // 1.5 + 3 - 2: the party bonus
    s = split(4, 1);
    CHECK(near(s.keep, 3) && near(s.each, 1));
    CHECK(near(created(4, 1), 0));
    // nobody to share with, or no delivery: the gatherer keeps it, nothing is made
    s = split(10, 0);
    CHECK(near(s.keep, 10) && near(s.each, 0));
    s = split(0.3, 2);
    CHECK(near(s.keep, 0.3) && near(s.each, 0));
    s = split(-5, 2);
    CHECK(near(s.keep, 0) && near(s.each, 0));
    CHECK(near(created(0.3, 2), 0) && near(created(-5, 2), 0));
    // gifts: only what the giver has
    CHECK(can_gift(30, 30) && !can_gift(29.9, 30) && !can_gift(100, 0));
    std::printf(g_fail ? "party food: %d failures\n" : "party food: 0 failures\n", g_fail);
    return g_fail ? 1 : 0;
}
