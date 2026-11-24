// Pure portable logic for BUG-0866: wider third-person FOV in Creature and Galactic Adventures
// (steps 0..6, +3 deg per step) and picking-ray recomputation matching the wide lens.
// No Windows headers so native Linux unit tests can verify all geometry and lifecycle rules.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace fov_logic {

constexpr int kMinStep = 0;
constexpr int kMaxStep = 6;
constexpr float kDegPerStep = 3.0f;
constexpr float kMaxTotalFovDeg = 165.0f;
constexpr int kWheelDelta = 120;

// Game IDs (verified GOG 2024 dc04aee5)
constexpr uint32_t kFollowCameraId = 0xB03AF999u; // Creature and Adventure follow camera
constexpr uint32_t kModeCreature = 0x01654C01u;
constexpr uint32_t kModeScenario = 0x01654C10u; // Galactic Adventures play mode
constexpr uint32_t kModeEditor = 0x00DBDBA1u;
constexpr uint32_t kModeGameEdit = 0x01654C06u;
constexpr uint32_t kModeLoadGame = 0x01654C08u;
constexpr uint32_t kModeCinematics = 0x8916F92Du;

inline int clamp_step(int step) { return std::clamp(step, kMinStep, kMaxStep); }

inline float extra_deg(int step) { return (float)clamp_step(step) * kDegPerStep; }

// Active stages where the wider third-person lens applies
inline bool is_fov_stage(uint32_t mode) { return mode == kModeCreature || mode == kModeScenario; }

// Modes that preserve the chosen FOV step (editors, load transitions, cinematics keep it;
// leaving to Tribe / Civ / Space / Cell / GalaxyGameEntry resets it to 0)
inline bool keeps_fov_mode(uint32_t mode)
{
    return is_fov_stage(mode) || mode == kModeEditor || mode == kModeGameEdit || mode == kModeLoadGame ||
           mode == kModeCinematics || mode == 0;
}

// Parse `coop.fov <0-6|reset>` argument
inline bool parse_step_arg(const char* s, int& out_step)
{
    if (!s || !*s)
        return false;
    if (!strcmp(s, "reset") || !strcmp(s, "off") || !strcmp(s, "default")) {
        out_step = 0;
        return true;
    }
    char* end = nullptr;
    long v = strtol(s, &end, 10);
    if (!end || *end != '\0')
        return false;
    if (v < kMinStep || v > kMaxStep)
        return false;
    out_step = (int)v;
    return true;
}

// Apply a WM_MOUSEWHEEL delta (negative = scroll down / zoom out = wider FOV, +1 step per notch;
// positive = scroll up / zoom in = narrower FOV, -1 step per notch). Accumulates fractional deltas
// in `remainder` for smooth-scroll mice. Returns the new clamped step.
inline int apply_wheel_delta(int cur_step, int wheel_delta, int& remainder)
{
    int step = clamp_step(cur_step);
    if (wheel_delta == 0)
        return step;
    // Reset partial accumulator if scroll direction reversed
    if ((remainder > 0 && wheel_delta < 0) || (remainder < 0 && wheel_delta > 0))
        remainder = 0;
    remainder += wheel_delta;
    while (remainder <= -kWheelDelta) {
        remainder += kWheelDelta;
        step = clamp_step(step + 1);
    }
    while (remainder >= kWheelDelta) {
        remainder -= kWheelDelta;
        step = clamp_step(step - 1);
    }
    return step;
}

struct ViewWindow {
    float x = 0.f; // camera +0x5C (half width at distance 1)
    float y = 0.f; // camera +0x60 (half height at distance 1)
};

// Widen the perspective camera view window by step * 3 degrees vertically while keeping aspect ratio.
inline ViewWindow widen_view_window(ViewWindow stock, int step)
{
    step = clamp_step(step);
    if (step <= 0 || stock.x <= 0.f || stock.y <= 0.f || !std::isfinite(stock.x) || !std::isfinite(stock.y))
        return stock;
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kRadToDeg = 180.0 / kPi;
    constexpr double kDegToRad = kPi / 180.0;
    double base_fov_deg = 2.0 * std::atan((double)stock.y) * kRadToDeg;
    double wide_fov_deg = std::min((double)kMaxTotalFovDeg, base_fov_deg + (double)step * (double)kDegPerStep);
    double aspect = (double)stock.x / (double)stock.y;
    double wide_y = std::tan(wide_fov_deg * 0.5 * kDegToRad);
    double wide_x = wide_y * aspect;
    if (!std::isfinite(wide_x) || !std::isfinite(wide_y) || wide_x <= 0.0 || wide_y <= 0.0)
        return stock;
    return {(float)wide_x, (float)wide_y};
}

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

struct Viewport {
    uint16_t x = 0, y = 0, w = 0, h = 0;
};

// Project world point to screen pixels using camera-to-world matrix `viewer_mat` (cViewer+0x00:
// rows right[0..2], forward[4..6], up[8..10], pos[12..14]), view window `vw`, view offset
// `(off_x, off_y)` (cam+0x54/+0x58), and viewport `vp`. Matches sporekit/gamehooks.cpp & 0x7C4370.
inline bool project_world(const float viewer_mat[16], ViewWindow vw, float off_x, float off_y, Viewport vp, Vec3 world,
                          float& out_sx, float& out_sy)
{
    if (vp.w == 0 || vp.h == 0 || vw.x <= 0.f || vw.y <= 0.f)
        return false;
    float dx = world.x - viewer_mat[12];
    float dy = world.y - viewer_mat[13];
    float dz = world.z - viewer_mat[14];
    float cx = dx * viewer_mat[0] + dy * viewer_mat[1] + dz * viewer_mat[2];
    float cz = dx * viewer_mat[4] + dy * viewer_mat[5] + dz * viewer_mat[6];
    float cy = dx * viewer_mat[8] + dy * viewer_mat[9] + dz * viewer_mat[10];
    if (cz <= 1e-4f)
        return false;
    float nx = (cx / cz + off_x) / vw.x;
    float ny = (cy / cz + off_y) / vw.y;
    out_sx = (float)vp.x + (nx + 1.0f) * 0.5f * (float)vp.w;
    out_sy = (float)vp.y + (1.0f - ny) * 0.5f * (float)vp.h;
    return std::isfinite(out_sx) && std::isfinite(out_sy);
}

// Recompute the world picking ray for screen point `(sx, sy)` with widened view window `vw`
// (inverse of Spore's perspective projection 0x7C4370 / cViewer::GetCameraToPoint 0x7C4800).
inline bool compute_pick_ray(const float viewer_mat[16], ViewWindow vw, float off_x, float off_y, Viewport vp, float sx,
                             float sy, Vec3& out_origin, Vec3& out_dir)
{
    if (vp.w == 0 || vp.h == 0 || vw.x <= 0.f || vw.y <= 0.f)
        return false;
    float nx = 2.0f * (sx - (float)vp.x) / (float)vp.w - 1.0f;
    float ny = 1.0f - 2.0f * (sy - (float)vp.y) / (float)vp.h;
    float rx = nx * vw.x - off_x;
    float ry = ny * vw.y - off_y;
    out_origin = {viewer_mat[12], viewer_mat[13], viewer_mat[14]};
    float dx = viewer_mat[0] * rx + viewer_mat[4] + viewer_mat[8] * ry;
    float dy = viewer_mat[1] * rx + viewer_mat[5] + viewer_mat[9] * ry;
    float dz = viewer_mat[2] * rx + viewer_mat[6] + viewer_mat[10] * ry;
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(len > 1e-8f) || !std::isfinite(len))
        return false;
    float inv = 1.0f / len;
    out_dir = {dx * inv, dy * inv, dz * inv};
    return true;
}

} // namespace fov_logic
