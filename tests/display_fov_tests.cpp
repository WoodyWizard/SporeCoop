// Native unit tests for WP-Q1:
// - BUG-0865: frame rate cap (1-360 fps) clamping/parsing/pacing and borderless windowed mode logic
// - BUG-0866: wider third-person FOV (steps 0..6, +3 deg/step), mode lifecycle, wheel stepping,
//   and picking-ray recomputation inverse-projection accuracy.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "../sporekit/display_logic.h"
#include "fov_logic.h"

static int g_fail = 0;

#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                             \
        }                                                         \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(std::fabs((double)(a) - (double)(b)) <= (double)(eps))

static void test_fps_clamp_parse_presets()
{
    using namespace skdisp;
    CHECK(clamp_fps(-50) == 0);
    CHECK(clamp_fps(0) == 0);
    CHECK(clamp_fps(1) == 1);
    CHECK(clamp_fps(60) == 60);
    CHECK(clamp_fps(144) == 144);
    CHECK(clamp_fps(360) == 360);
    CHECK(clamp_fps(361) == 360);
    CHECK(clamp_fps(10000) == 360);

    int v = -1;
    CHECK(parse_fps_arg("auto", v) && v == 0);
    CHECK(parse_fps_arg("off", v) && v == 0);
    CHECK(parse_fps_arg("default", v) && v == 0);
    CHECK(parse_fps_arg("0", v) && v == 0);
    CHECK(parse_fps_arg("1", v) && v == 1);
    CHECK(parse_fps_arg("120", v) && v == 120);
    CHECK(parse_fps_arg("360", v) && v == 360);
    CHECK(!parse_fps_arg("-1", v));
    CHECK(!parse_fps_arg("361", v));
    CHECK(!parse_fps_arg("abc", v));
    CHECK(!parse_fps_arg("60fps", v));
    CHECK(!parse_fps_arg("", v));
    CHECK(!parse_fps_arg(nullptr, v));

    bool b = false;
    CHECK(parse_bool_arg("on", b) && b);
    CHECK(parse_bool_arg("1", b) && b);
    CHECK(parse_bool_arg("true", b) && b);
    CHECK(parse_bool_arg("yes", b) && b);
    CHECK(parse_bool_arg("off", b) && !b);
    CHECK(parse_bool_arg("0", b) && !b);
    CHECK(parse_bool_arg("false", b) && !b);
    CHECK(parse_bool_arg("no", b) && !b);
    CHECK(!parse_bool_arg("maybe", b));
    CHECK(!parse_bool_arg("", b));
    CHECK(!parse_bool_arg(nullptr, b));

    // Full preset cycle: 0 -> 30 -> 60 -> 90 -> 120 -> 144 -> 165 -> 240 -> 360 -> 0
    int p = 0;
    const int expected[] = {30, 60, 90, 120, 144, 165, 240, 360, 0};
    for (int e : expected) {
        p = next_fps_preset(p);
        CHECK(p == e);
    }
    CHECK(next_fps_preset(45) == 60);
    CHECK(next_fps_preset(100) == 120);
    CHECK(next_fps_preset(500) == 0);

    CHECK(fps_label(0) == "Auto");
    CHECK(fps_label(-5) == "Auto");
    CHECK(fps_label(120) == "120 fps");
    CHECK(fps_label(500) == "360 fps");

    // Game kFrameLimitMS override
    CHECK(desired_frame_ms(120, true, -1, -1, false, 16) == 0);
    CHECK(desired_frame_ms(120, true, 33, 5, true, 16) == 0);
    // Fix OFF: frame_target does not lift the game's cap
    CHECK(desired_frame_ms(120, false, -1, -1, false, 16) == 16);
    CHECK(desired_frame_ms(120, false, 50, -1, false, 16) == 50);
    CHECK(desired_frame_ms(120, false, -1, 5, true, 16) == 5);
    // Auto (0): existing frame_ms / menu_ms / game_own_ms precedence
    CHECK(desired_frame_ms(0, true, -1, -1, false, 16) == 16);
    CHECK(desired_frame_ms(0, true, 25, 5, true, 16) == 25);
    CHECK(desired_frame_ms(0, true, -1, 5, true, 16) == 5);
    CHECK(desired_frame_ms(0, true, -1, 5, false, 16) == 16);
}

static void test_frame_pacer()
{
    using namespace skdisp;
    PacerState st{};

    // Auto / 0 returns 0 and resets deadline
    CHECK(pace_step(st, 0, 1000000000LL) == 0);
    CHECK(st.next_ns == 0);

    // 120 fps -> 8,333,333 ns period
    const int64_t period = frame_period_ns(120);
    CHECK(period == 8333333LL);

    // First frame initializes deadline without sleeping
    int64_t t = 10000000000LL;
    CHECK(pace_step(st, 120, t) == 0);
    CHECK(st.next_ns == t + period);

    // Steady loop: render takes 2 ms, pacer asks to wait remaining ~6.333 ms, zero drift over 100 frames
    int64_t start_t = t;
    for (int i = 0; i < 100; i++) {
        t += 2000000LL; // 2 ms render
        int64_t w = pace_step(st, 120, t);
        CHECK(w == period - 2000000LL);
        t += w;
    }
    CHECK(t - start_t == 100LL * period);

    // Small overrun (1.5x period): no sleep, advances by 1 period
    t += period + period / 2;
    CHECK(pace_step(st, 120, t) == 0);
    // Next frame finishes quickly: still absorbs jitter cleanly
    t += 1000000LL;
    int64_t w_after = pace_step(st, 120, t);
    CHECK(w_after > 0 && w_after < period);
    t += w_after;

    // Large hitch (500 ms load): resyncs cleanly instead of burst-catching up
    t += 500000000LL;
    CHECK(pace_step(st, 120, t) == 0);
    CHECK(st.next_ns == t + period);
    // Very next frame after hitch should wait normally, not skip waits
    t += 1000000LL;
    CHECK(pace_step(st, 120, t) == period - 1000000LL);

    // Timer sleep split (high-res leaves 0.5 ms tail, fallback leaves 1.5 ms tail)
    CHECK(timer_sleep_100ns(400000LL, true) == 0);
    CHECK(timer_sleep_100ns(6000000LL, true) == 55000LL);  // 5.5 ms in 100ns units
    CHECK(timer_sleep_100ns(1400000LL, false) == 0);
    CHECK(timer_sleep_100ns(6000000LL, false) == 45000LL); // 4.5 ms in 100ns units
}

static void test_borderless_logic()
{
    using namespace skdisp;
    const uint32_t ws_visible = 0x10000000u;
    const uint32_t ws_clipsib = 0x04000000u;
    const uint32_t normal_style = ws_visible | ws_clipsib | kStyleOverlappedWindow;
    const uint32_t stripped = borderless_style(normal_style);

    CHECK(has_window_borders(normal_style));
    CHECK(!has_window_borders(stripped));
    CHECK((stripped & ws_visible) == ws_visible);
    CHECK((stripped & ws_clipsib) == ws_clipsib);

    const uint32_t normal_ex = 0x00040000u | kExBorderMask; // WS_EX_APPWINDOW + border edges
    const uint32_t stripped_ex = borderless_exstyle(normal_ex);
    CHECK((stripped_ex & kExBorderMask) == 0);
    CHECK((stripped_ex & 0x00040000u) == 0x00040000u);

    const RectI win_rect{100, 80, 1540, 1160};
    const RectI mon_rect{0, 0, 2560, 1440};
    const RectI bad_rect{0, 0, 0, 0};

    BorderlessState st{};
    st.enabled = true;
    st.video_windowed = true;
    st.applied = false;
    st.have_saved = false;

    // Windowed + enabled + fix ON -> Apply
    CHECK(decide_borderless(st, true, normal_style, win_rect, mon_rect) == BorderlessAction::Apply);
    // Invalid monitor rect -> None
    CHECK(decide_borderless(st, true, normal_style, win_rect, bad_rect) == BorderlessAction::None);
    // Fix OFF -> never Apply
    CHECK(decide_borderless(st, false, normal_style, win_rect, mon_rect) == BorderlessAction::None);

    // Once applied and matching monitor rect + borderless style -> None
    st.applied = true;
    st.have_saved = true;
    st.saved_style = normal_style;
    st.saved_rect = win_rect;
    CHECK(decide_borderless(st, true, stripped, mon_rect, mon_rect) == BorderlessAction::None);

    // Spore restores borders or resizes window -> maintain_borderless triggers Apply again
    CHECK(decide_borderless(st, true, normal_style, mon_rect, mon_rect) == BorderlessAction::Apply);
    CHECK(decide_borderless(st, true, stripped, win_rect, mon_rect) == BorderlessAction::Apply);

    // Exclusive fullscreen -> never touch window
    st.video_windowed = false;
    CHECK(decide_borderless(st, true, normal_style, win_rect, mon_rect) == BorderlessAction::None);
    st.enabled = false;
    CHECK(decide_borderless(st, true, stripped, mon_rect, mon_rect) == BorderlessAction::None);

    // Back to windowed, disabled while applied -> Restore
    st.video_windowed = true;
    CHECK(decide_borderless(st, true, stripped, mon_rect, mon_rect) == BorderlessAction::Restore);
    st.applied = false;
    CHECK(decide_borderless(st, true, normal_style, win_rect, mon_rect) == BorderlessAction::None);

    // Enabled while applied, then fix switched OFF -> Restore
    st.enabled = true;
    st.applied = true;
    CHECK(decide_borderless(st, false, stripped, mon_rect, mon_rect) == BorderlessAction::Restore);
}

static void test_fov_steps_and_lifecycle()
{
    using namespace fov_logic;
    CHECK(clamp_step(-3) == 0);
    CHECK(clamp_step(0) == 0);
    CHECK(clamp_step(6) == 6);
    CHECK(clamp_step(99) == 6);
    CHECK_NEAR(extra_deg(0), 0.0, 1e-6);
    CHECK_NEAR(extra_deg(3), 9.0, 1e-6);
    CHECK_NEAR(extra_deg(6), 18.0, 1e-6);

    int s = -1;
    CHECK(parse_step_arg("0", s) && s == 0);
    CHECK(parse_step_arg("6", s) && s == 6);
    CHECK(parse_step_arg("reset", s) && s == 0);
    CHECK(parse_step_arg("off", s) && s == 0);
    CHECK(parse_step_arg("default", s) && s == 0);
    CHECK(!parse_step_arg("-1", s));
    CHECK(!parse_step_arg("7", s));
    CHECK(!parse_step_arg("wide", s));
    CHECK(!parse_step_arg("", s));
    CHECK(!parse_step_arg(nullptr, s));

    // Active stage rules
    CHECK(is_fov_stage(kModeCreature));
    CHECK(is_fov_stage(kModeScenario));
    CHECK(!is_fov_stage(kModeEditor));
    CHECK(!is_fov_stage(0x01654C00u)); // Cell
    CHECK(!is_fov_stage(0x01654C02u)); // Tribe
    CHECK(!is_fov_stage(0x01654C04u)); // Civ
    CHECK(!is_fov_stage(0x01654C05u)); // Space
    CHECK(!is_fov_stage(0x02CCD1D2u)); // GalaxyGameEntry

    // Lifecycle retention: Creature / Scenario / Editors / Load / Cinematics keep; other stages reset
    CHECK(keeps_fov_mode(kModeCreature));
    CHECK(keeps_fov_mode(kModeScenario));
    CHECK(keeps_fov_mode(kModeEditor));
    CHECK(keeps_fov_mode(kModeGameEdit));
    CHECK(keeps_fov_mode(kModeLoadGame));
    CHECK(keeps_fov_mode(kModeCinematics));
    CHECK(!keeps_fov_mode(0x01654C00u)); // Cell
    CHECK(!keeps_fov_mode(0x01654C02u)); // Tribe
    CHECK(!keeps_fov_mode(0x01654C04u)); // Civ
    CHECK(!keeps_fov_mode(0x01654C05u)); // Space
    CHECK(!keeps_fov_mode(0x02CCD1D2u)); // GalaxyGameEntry

    // Ctrl+Scroll stepping (scroll down = -120 = widen +1; scroll up = +120 = narrow -1)
    int rem = 0;
    int step = 0;
    step = apply_wheel_delta(step, -120, rem);
    CHECK(step == 1 && rem == 0);
    step = apply_wheel_delta(step, -360, rem); // 3 notches down
    CHECK(step == 4 && rem == 0);
    step = apply_wheel_delta(step, -600, rem); // clamp at 6
    CHECK(step == 6 && rem == 0);
    step = apply_wheel_delta(step, +120, rem); // 1 notch up
    CHECK(step == 5 && rem == 0);
    // Smooth-scroll fractional deltas
    step = apply_wheel_delta(step, -60, rem);
    CHECK(step == 5 && rem == -60);
    step = apply_wheel_delta(step, -60, rem);
    CHECK(step == 6 && rem == 0);
    // Direction reversal clears stale remainder
    step = apply_wheel_delta(step, +60, rem);
    CHECK(step == 6 && rem == 60);
    step = apply_wheel_delta(step, -60, rem);
    CHECK(step == 6 && rem == -60);
    step = apply_wheel_delta(step, +1200, rem); // clamp at 0
    CHECK(step == 0 && rem == 0);
}

static void test_fov_math_and_pick_ray()
{
    using namespace fov_logic;
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kRadToDeg = 180.0 / kPi;

    // Stock Creature follow camera: +0x60 = 0.4097 (~44.56 deg), 4:3 aspect (1440x1080)
    const float aspect = 1440.0f / 1080.0f;
    const ViewWindow stock{0.4097f * aspect, 0.4097f};
    const double base_deg = 2.0 * std::atan((double)stock.y) * kRadToDeg;

    // Step 0 is exact identity
    ViewWindow w0 = widen_view_window(stock, 0);
    CHECK(w0.x == stock.x && w0.y == stock.y);

    // Every step 1..6 adds +3.0 deg vertically and preserves aspect ratio
    for (int st = 1; st <= 6; st++) {
        ViewWindow w = widen_view_window(stock, st);
        double deg = 2.0 * std::atan((double)w.y) * kRadToDeg;
        CHECK_NEAR(deg - base_deg, st * 3.0, 1e-3);
        CHECK_NEAR(w.x / w.y, stock.x / stock.y, 1e-5);
        CHECK(w.x > stock.x && w.y > stock.y);
    }

    // Defensive handling of degenerate / NaN view windows
    ViewWindow bad{0.f, 0.4f};
    CHECK(widen_view_window(bad, 3).x == 0.f);
    ViewWindow nan_vw{std::numeric_limits<float>::quiet_NaN(), 0.4f};
    CHECK(std::isnan(widen_view_window(nan_vw, 3).x));

    // Build an arbitrary rotated orthonormal camera-to-world matrix (rows: R, F, U, P)
    // Yaw = 37 deg, Pitch = -18 deg, Roll = 7 deg
    auto norm3 = [](float v[3]) {
        float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    };
    auto cross3 = [](const float a[3], const float b[3], float o[3]) {
        o[0] = a[1] * b[2] - a[2] * b[1];
        o[1] = a[2] * b[0] - a[0] * b[2];
        o[2] = a[0] * b[1] - a[1] * b[0];
    };
    float F[3] = {0.62f, 0.74f, -0.26f};
    norm3(F);
    float up_hint[3] = {0.11f, -0.08f, 0.99f};
    norm3(up_hint);
    float R[3], U[3];
    cross3(F, up_hint, R);
    norm3(R);
    cross3(R, F, U);
    norm3(U);
    float V[16] = {
        R[0],   R[1],   R[2],   0.f,
        F[0],   F[1],   F[2],   0.f,
        U[0],   U[1],   U[2],   0.f,
        123.4f, -45.6f, 208.9f, 1.f,
    };

    const Viewport vp{12, 8, 1440, 1080};
    const float offsets[][2] = {{0.0f, 0.0f}, {0.03f, -0.02f}};
    const float targets[][3] = {
        {0.0f, 15.0f, 0.0f},   // dead center
        {-7.5f, 18.0f, 3.2f},  // upper-left off-center NPC
        {9.2f, 22.0f, -4.1f},  // lower-right off-center NPC
        {4.0f, 8.0f, 2.5f},    // close mate
        {-18.0f, 40.0f, -6.0f} // far target
    };

    for (auto& off : offsets) {
        for (int st = 0; st <= 6; st++) {
            ViewWindow wide = widen_view_window(stock, st);
            for (auto& loc : targets) {
                // World point W = P + R*loc[0] + F*loc[1] + U*loc[2]
                Vec3 W{
                    V[12] + R[0] * loc[0] + F[0] * loc[1] + U[0] * loc[2],
                    V[13] + R[1] * loc[0] + F[1] * loc[1] + U[1] * loc[2],
                    V[14] + R[2] * loc[0] + F[2] * loc[1] + U[2] * loc[2],
                };
                float sx = 0.f, sy = 0.f;
                CHECK(project_world(V, wide, off[0], off[1], vp, W, sx, sy));

                Vec3 orig{}, dir{};
                CHECK(compute_pick_ray(V, wide, off[0], off[1], vp, sx, sy, orig, dir));
                CHECK_NEAR(orig.x, V[12], 1e-5);
                CHECK_NEAR(orig.y, V[13], 1e-5);
                CHECK_NEAR(orig.z, V[14], 1e-5);
                float dlen = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                CHECK_NEAR(dlen, 1.0, 1e-5);

                // Closest point on ray to W must match W within 1e-4 m
                float wx = W.x - orig.x, wy = W.y - orig.y, wz = W.z - orig.z;
                float t = wx * dir.x + wy * dir.y + wz * dir.z;
                CHECK(t > 0.f);
                float ex = wx - dir.x * t, ey = wy - dir.y * t, ez = wz - dir.z * t;
                float dist = std::sqrt(ex * ex + ey * ey + ez * ez);
                CHECK(dist < 1e-4f);

                // Contrast: at step 6 on an off-center target, a stock-lens ray misses badly (> 1.5 m!)
                if (st == 6 && off[0] == 0.f && std::fabs(loc[0]) > 7.f) {
                    Vec3 bad_o{}, bad_d{};
                    CHECK(compute_pick_ray(V, stock, 0.f, 0.f, vp, sx, sy, bad_o, bad_d));
                    float bt = wx * bad_d.x + wy * bad_d.y + wz * bad_d.z;
                    float bx = wx - bad_d.x * bt, by = wy - bad_d.y * bt, bz = wz - bad_d.z * bt;
                    float miss = std::sqrt(bx * bx + by * by + bz * bz);
                    CHECK(miss > 1.5f);
                }
            }
        }
    }
}

int main()
{
    test_fps_clamp_parse_presets();
    test_frame_pacer();
    test_borderless_logic();
    test_fov_steps_and_lifecycle();
    test_fov_math_and_pick_ray();
    if (g_fail) {
        printf("display_fov_tests: %d FAIL\n", g_fail);
        return 1;
    }
    printf("display_fov_tests: ALL OK\n");
    return 0;
}
