// Native unit tests for the Creature editor depth handle pure logic (editor_depth_logic.h, BUG-0871).
#include "editor_depth_logic.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace editor_depth;

static int g_fail = 0;

static void check(bool cond, const char* msg, int line)
{
    if (!cond) {
        fprintf(stderr, "FAIL line %d: %s\n", line, msg);
        g_fail++;
    }
}
#define CK(c) check((c), #c, __LINE__)
#define NEAR(a, b, eps) check(std::fabs((a) - (b)) <= (eps), #a " ~= " #b, __LINE__)

static void test_eligibility()
{
    // Ear / eye / mouth / weapon / detail: all exclusion flags false -> eligible
    CK(part_eligible(false, false, false, false, false, false));
    // Spine vertebra
    CK(!part_eligible(true, false, false, false, false, false));
    // Limb connectors
    CK(!part_eligible(false, true, false, false, false, false));
    CK(!part_eligible(false, false, true, false, false, false));
    // Capabilities: limb, hand, foot
    CK(!part_eligible(false, false, false, true, false, false));
    CK(!part_eligible(false, false, false, false, true, false));
    CK(!part_eligible(false, false, false, false, false, true));
}

static void test_spine_distance_and_normal()
{
    std::vector<Vec3> spine = {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 2.0f, 0.0f}};
    Vec3 closest{};
    float d = distance_to_spine({0.6f, 0.5f, 0.0f}, spine, &closest);
    NEAR(d, 0.6f, 1e-4f);
    NEAR(closest.x, 0.0f, 1e-4f);
    NEAR(closest.y, 0.5f, 1e-4f);
    NEAR(closest.z, 0.0f, 1e-4f);

    // Endpoint clamping
    float d2 = distance_to_spine({0.0f, 3.0f, 0.0f}, spine, &closest);
    NEAR(d2, 1.0f, 1e-4f);
    NEAR(closest.y, 2.0f, 1e-4f);

    // Single vertebra
    std::vector<Vec3> one = {{0.0f, 1.0f, 0.0f}};
    NEAR(distance_to_spine({0.3f, 1.4f, 0.0f}, one), 0.5f, 1e-4f);

    // Empty spine fallback
    std::vector<Vec3> empty;
    NEAR(distance_to_spine({1.0f, 0.0f, 0.0f}, empty), 0.5f, 1e-4f);

    // Outward normal from EditorRigblock +0xA8 pin frame: 2nd row negated
    float pin[9] = {
        0.0f, 1.0f, 0.0f,
        -0.6f, -0.8f, 0.0f, // 2nd row -> negated is (+0.6, +0.8, 0)
        0.0f, 0.0f, 1.0f,
    };
    Vec3 n = outward_normal(pin, {0.6f, 0.5f, 0.0f}, spine);
    NEAR(n.x, 0.6f, 1e-4f);
    NEAR(n.y, 0.8f, 1e-4f);
    NEAR(n.z, 0.0f, 1e-4f);

    // Fallback when pin frame 2nd row is zero
    float zero_pin[9] = {};
    Vec3 nf = outward_normal(zero_pin, {0.6f, 0.5f, 0.0f}, spine);
    NEAR(nf.x, 1.0f, 1e-4f);
    NEAR(nf.y, 0.0f, 1e-4f);
    NEAR(nf.z, 0.0f, 1e-4f);
}

static void test_limits_and_mirror()
{
    // Depth-bound outward limit: 0.3 * 0.4 = 0.12 < 0.2 * 1.0 = 0.20
    DepthLimits l1 = compute_limits(0.4f, 1.0f);
    NEAR(l1.max_offset, 0.12f, 1e-4f);
    NEAR(l1.min_offset, -0.30f, 1e-4f); // 0.75 * 0.4 -> stays >= 0.25 * 0.4 = 0.10 from spine

    // Scale-bound outward limit: 0.3 * 2.0 = 0.60 > 0.2 * 0.5 = 0.10
    DepthLimits l2 = compute_limits(2.0f, 0.5f);
    NEAR(l2.max_offset, 0.10f, 1e-4f);
    NEAR(l2.min_offset, -1.50f, 1e-4f);

    // Handle world position: 0.18 * scale out along normal
    Vec3 hp = handle_world_pos({1.0f, 2.0f, 3.0f}, {0.0f, 1.0f, 0.0f}, 2.0f);
    NEAR(hp.x, 1.0f, 1e-4f);
    NEAR(hp.y, 2.36f, 1e-4f);
    NEAR(hp.z, 3.0f, 1e-4f);

    // Mirror across X=0 plane
    Vec3 d = mirror_delta({0.1f, -0.05f, 0.02f});
    NEAR(d.x, -0.1f, 1e-5f);
    NEAR(d.y, -0.05f, 1e-5f);
    NEAR(d.z, 0.02f, 1e-5f);

    Vec3 mp = mirror_pos({0.45f, 1.2f, -0.3f});
    NEAR(mp.x, -0.45f, 1e-5f);
    NEAR(mp.y, 1.2f, 1e-5f);
    NEAR(mp.z, -0.3f, 1e-5f);
}

static void test_drag_and_tracker()
{
    // Drag along projected screen axis: axis (50, 0) for 0.25 world units -> +25 px = +0.125 world units
    NEAR(drag_delta(25.0f, 10.0f, 50.0f, 0.0f, 0.25f), 0.125f, 1e-4f);
    NEAR(drag_delta(-50.0f, 0.0f, 50.0f, 0.0f, 0.25f), -0.25f, 1e-4f);
    // Degenerate projected axis (< 2 px) returns 0
    NEAR(drag_delta(30.0f, 30.0f, 0.5f, 0.5f, 0.25f), 0.0f, 1e-5f);

    // Tracker preserves baseline across consecutive drags and clamps to limits
    DepthTracker tr;
    Vec3 norm{1.0f, 0.0f, 0.0f};
    CK(tr.sync(0x1000, 0xABCD, {0.5f, 1.0f, 0.0f}, 0.5f));
    DepthLimits lim = compute_limits(tr.base_depth, 1.0f); // [-0.375, +0.15]
    Vec3 out{};
    float applied = tr.apply(0.50f, norm, lim, out);
    NEAR(applied, 0.15f, 1e-4f);
    NEAR(out.x, 0.65f, 1e-4f);
    // Same part at our written position -> does NOT re-baseline
    CK(!tr.sync(0x1000, 0xABCD, out, 0.65f));
    NEAR(tr.base_depth, 0.5f, 1e-4f);

    // Push inward past limit -> clamps to -0.375
    applied = tr.apply(-1.0f, norm, lim, out);
    NEAR(applied, -0.375f, 1e-4f);
    NEAR(out.x, 0.125f, 1e-4f);

    // External move (e.g. Undo back to 0.5) -> re-baselines
    CK(tr.sync(0x1000, 0xABCD, {0.5f, 1.0f, 0.0f}, 0.5f));
    NEAR(tr.offset, 0.0f, 1e-5f);
}

static void test_cube_geometry_and_colors()
{
    Vec3 verts[8], normals[6];
    Vec3 center{0.5f, 1.0f, -0.2f};
    Vec3 axis = normalize_or({0.3f, 0.7f, -0.5f}, {0.0f, 1.0f, 0.0f});
    float half = 0.04f;
    build_cube(center, axis, half, verts, normals);

    // Every corner is at distance sqrt(3) * half from center
    float expected_r = std::sqrt(3.0f) * half;
    for (int i = 0; i < 8; ++i)
        NEAR(len(verts[i] - center), expected_r, 1e-5f);

    // Every face quad in kCubeFaces must have outward-CCW winding matching normals[f]
    for (int f = 0; f < 6; ++f) {
        const auto& idx = kCubeFaces[f];
        Vec3 e1 = verts[idx[1]] - verts[idx[0]];
        Vec3 e2 = verts[idx[2]] - verts[idx[0]];
        Vec3 n = normalize_or(cross(e1, e2), {0, 0, 0});
        NEAR(dot(n, normals[f]), 1.0f, 1e-4f);
    }

    // Colours differ by state and stay valid opaque/translucent ARGB
    uint32_t c_def = cube_face_color(kDefault, 0.5f);
    uint32_t c_hov = cube_face_color(kHover, 0.5f);
    uint32_t c_hld = cube_face_color(kHeld, 0.5f);
    CK(c_def != c_hov && c_hov != c_hld && c_def != c_hld);
    CK((c_def >> 24) == 0xE8u);
    // Held orange has R > G > B
    CK(((c_hld >> 16) & 0xFF) > ((c_hld >> 8) & 0xFF));
    CK(((c_hld >> 8) & 0xFF) > (c_hld & 0xFF));
}

int main()
{
    test_eligibility();
    test_spine_distance_and_normal();
    test_limits_and_mirror();
    test_drag_and_tracker();
    test_cube_geometry_and_colors();
    if (g_fail) {
        fprintf(stderr, "editor_depth_tests: %d FAILED\n", g_fail);
        return 1;
    }
    printf("editor_depth_tests: all passed\n");
    return 0;
}
