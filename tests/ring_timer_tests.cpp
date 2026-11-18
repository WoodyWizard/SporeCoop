// BUG-9141: the revive ring's geometry and colours (mods/coop/ring_timer_logic.h).
#include "ring_timer_logic.h"

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

static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

int main()
{
    using namespace ring_timer;
    // 12 o'clock is straight up (screen y down), a quarter is 3 o'clock, a half 6 o'clock
    Pt top = on_circle(100, 100, 10, 0), right = on_circle(100, 100, 10, 0.25f), bottom = on_circle(100, 100, 10, 0.5f);
    CHECK(near(top.x, 100) && near(top.y, 90));
    CHECK(near(right.x, 110) && near(right.y, 100));
    CHECK(near(bottom.x, 100) && near(bottom.y, 110));

    // the full ring: a closed strip of 49 pairs, first and last at 12 o'clock
    auto full = arc_strip(0, 0, 6, 10, 0, 1);
    CHECK(full.size() == 2u * (kSegments + 1));
    CHECK(near(full.front().x, 0) && near(full.front().y, -10) && near(full.back().y, -6));
    // nothing left: nothing drawn; a bad radius: nothing
    CHECK(arc_strip(0, 0, 6, 10, left_start(0), 0).empty());
    CHECK(arc_strip(0, 0, 10, 6, 0, 1).empty());
    // three quarters left: the arc runs from 3 o'clock on to 12 o'clock (the hand ate the first quarter)
    CHECK(near(left_start(0.75f), 0.25f));
    auto three = arc_strip(0, 0, 6, 10, left_start(0.75f), 0.75f);
    CHECK(!three.empty() && near(three.front().x, 10) && near(three.front().y, 0));
    CHECK(near(three[three.size() - 2].x, 0) && near(three[three.size() - 2].y, -10));
    // start + frac never pass the full circle
    auto clipped = arc_strip(0, 0, 6, 10, 0.9f, 0.5f);
    CHECK(near(clipped[clipped.size() - 2].y, -10));
    // a sliver still gets one segment
    CHECK(arc_strip(0, 0, 6, 10, 0.999f, 0.001f).size() == 4u);

    // the revive pie: triangles from the centre, a half pie ends at 6 o'clock
    auto half = pie(0, 0, 5, 0.5f);
    CHECK(half.size() % 3 == 0 && half.size() == 3u * (kSegments / 2));
    CHECK(near(half[0].x, 0) && near(half[0].y, 0) && near(half.back().y, 5));
    CHECK(pie(0, 0, 5, 0).empty() && pie(0, 0, 0, 1).empty());

    // colours: amber at half or more, red at the end, the blink darkens only the last quarter
    CHECK(time_color(1) == kAmber && time_color(0.5f) == kAmber);
    CHECK(time_color(0) == kRed);
    CHECK(time_color(0.1f, false) != time_color(0.1f, true));
    CHECK(time_color(0.4f, false) == time_color(0.4f, true));
    CHECK(lerp_argb(0xFF000000, 0xFFFFFFFF, 0.5f) == 0xFF808080u);

    // the fraction: clamped, a bad total is 0
    CHECK(near(fraction(15, 30), 0.5f) && near(fraction(45, 30), 1) && near(fraction(-3, 30), 0) && fraction(5, 0) == 0);
    // the size follows the screen: 30 px at 1080, never below 16 or above 36
    CHECK(near(radius_for(1080), 30) && near(radius_for(300), 16) && near(radius_for(2160), 36));

    std::printf("ring timer: %d failures\n", g_fail);
    return g_fail ? 1 : 0;
}
