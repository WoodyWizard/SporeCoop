// BUG-9145: the depth cube's drag axis (mods/coop/editor_depth_axis.h).
#include "editor_depth_axis.h"

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
    using namespace editor_depth_axis;
    // the part's line across the screen: 16 px for the 0.05 step to the right -> 320 px per unit, along +x
    Axis a = drag_axis(400, 300, 416, 300, true, 300);
    CHECK(!a.fallback && near(a.sx, 1) && near(a.sy, 0) && near(a.px_per_unit, 320));
    // diagonal, down and left
    a = drag_axis(400, 300, 394, 308, true, 300);
    CHECK(!a.fallback && near(a.sx, -0.6f) && near(a.sy, 0.8f) && near(a.px_per_unit, 200));
    // the line points at the camera: 1 px for the step -> up / down at the cube's scale
    a = drag_axis(400, 300, 401, 300, true, 280);
    CHECK(a.fallback && a.sx == 0 && a.sy == -1 && near(a.px_per_unit, 280));
    // the step's end behind the camera (not written: NaN) -> the fallback, never a refused press
    a = drag_axis(400, 300, NAN, NAN, true, 0);
    CHECK(a.fallback && near(a.px_per_unit, 300));
    a = drag_axis(400, 300, 420, 300, false, 250);
    CHECK(a.fallback && near(a.px_per_unit, 250));
    // off the screen is fine: only the direction and the length count
    a = drag_axis(790, 300, 830, 330, true, 300);
    CHECK(!a.fallback && near(a.sx, 0.8f) && near(a.sy, 0.6f) && near(a.px_per_unit, 1000));
    // exactly the threshold still drags along the line
    a = drag_axis(0, 0, 3, 0, true, 300);
    CHECK(!a.fallback && near(a.px_per_unit, 60));
    std::printf("editor depth axis: %d failures\n", g_fail);
    return g_fail ? 1 : 0;
}
