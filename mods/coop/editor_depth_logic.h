// Pure math and rules for the Creature editor depth handle (BUG-0871).
// Portable (no Windows, no SporeKit): tests/editor_depth_tests.cpp runs it natively.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace editor_depth {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float len_sq(Vec3 v) { return dot(v, v); }
inline float len(Vec3 v) { return std::sqrt(len_sq(v)); }

inline Vec3 normalize_or(Vec3 v, Vec3 fallback)
{
    float l = len(v);
    if (!std::isfinite(l) || l < 1e-4f)
        return fallback;
    return v * (1.0f / l);
}

// Parts excluded from depth nudging: spine vertebrae, limbs (ball/socket connectors or limb capability), hands, feet.
inline bool part_eligible(bool is_vertebra, bool has_ball_connector, bool has_socket_connector, bool cap_limb,
                          bool cap_hand, bool cap_foot)
{
    return !(is_vertebra || has_ball_connector || has_socket_connector || cap_limb || cap_hand || cap_foot);
}

inline Vec3 closest_point_on_segment(Vec3 p, Vec3 a, Vec3 b)
{
    Vec3 ab = b - a;
    float d2 = len_sq(ab);
    if (d2 < 1e-8f)
        return a;
    float t = std::clamp(dot(p - a, ab) / d2, 0.0f, 1.0f);
    return a + ab * t;
}

inline float distance_to_spine(Vec3 p, const std::vector<Vec3>& spine, Vec3* out_closest = nullptr)
{
    if (spine.empty()) {
        if (out_closest)
            *out_closest = {0.0f, 0.0f, 0.0f};
        return 0.5f;
    }
    if (spine.size() == 1) {
        if (out_closest)
            *out_closest = spine[0];
        return len(p - spine[0]);
    }
    float best_d2 = 1e30f;
    Vec3 best_pt = spine[0];
    for (size_t i = 0; i + 1 < spine.size(); ++i) {
        Vec3 c = closest_point_on_segment(p, spine[i], spine[i + 1]);
        float d2 = len_sq(p - c);
        if (d2 < best_d2) {
            best_d2 = d2;
            best_pt = c;
        }
    }
    if (out_closest)
        *out_closest = best_pt;
    return std::sqrt(best_d2);
}

// Outward skin normal from EditorRigblock +0xA8 attach frame (3x3 by rows: negated second row points out of the skin).
// Falls back to the direction away from the nearest spine segment when the frame row is degenerate.
inline Vec3 outward_normal(const float pin_frame[9], Vec3 part_pos, const std::vector<Vec3>& spine)
{
    if (pin_frame) {
        Vec3 n{-pin_frame[3], -pin_frame[4], -pin_frame[5]};
        float l = len(n);
        if (std::isfinite(l) && l >= 1e-4f)
            return n * (1.0f / l);
    }
    Vec3 closest{};
    distance_to_spine(part_pos, spine, &closest);
    return normalize_or(part_pos - closest, {1.0f, 0.0f, 0.0f});
}

struct DepthLimits {
    float min_offset = -0.25f; // inward (negative along outward normal)
    float max_offset = 0.15f;  // outward (positive along outward normal)
};

// Limits from the baseline body depth (first-seen distance to the spine) and part scale:
//   outward: at most min(0.30 * base_depth, 0.20 * scale)
//   inward:  no closer than 0.25 * base_depth to the spine (at most 0.75 * base_depth inward)
inline DepthLimits compute_limits(float base_depth, float part_scale)
{
    float d = (std::isfinite(base_depth) && base_depth > 0.05f) ? base_depth : 0.05f;
    float s = (std::isfinite(part_scale) && part_scale > 0.05f) ? part_scale : 1.0f;
    float max_out = std::min(0.30f * d, 0.20f * s);
    float max_in = 0.75f * d;
    return {-max_in, max_out};
}

inline Vec3 handle_world_pos(Vec3 part_pos, Vec3 normal, float part_scale)
{
    float s = (std::isfinite(part_scale) && part_scale > 0.05f) ? part_scale : 1.0f;
    return part_pos + normal * (0.18f * s);
}

// Creature editor symmetry plane is X = 0.
inline Vec3 mirror_delta(Vec3 delta) { return {-delta.x, delta.y, delta.z}; }
inline Vec3 mirror_pos(Vec3 pos) { return {-pos.x, pos.y, pos.z}; }

// Convert a 2D pixel drag along the projected 3D handle axis into a world-space depth delta.
inline float drag_delta(float dx_px, float dy_px, float axis_dx_px, float axis_dy_px, float world_step)
{
    float denom = axis_dx_px * axis_dx_px + axis_dy_px * axis_dy_px;
    if (!std::isfinite(denom) || denom < 4.0f || !std::isfinite(world_step) || world_step <= 0.0f)
        return 0.0f;
    float proj = (dx_px * axis_dx_px + dy_px * axis_dy_px) / denom;
    return proj * world_step;
}

enum HandleState : uint8_t { kDefault = 0, kHover = 1, kHeld = 2 };

// Spore editor handle colours (10CB4FA8!DB33584A default / rollover, 10CB4FA8!900A4DB5 power orange when held).
inline uint32_t cube_face_color(HandleState st, float light_dot)
{
    float r = 0.67f, g = 0.75f, b = 1.20f;
    if (st == kHover) {
        r = 0.54f;
        g = 0.81f;
        b = 0.54f;
    } else if (st == kHeld) {
        r = 0.93f;
        g = 0.48f;
        b = 0.16f;
    }
    float l = std::clamp(light_dot, -1.0f, 1.0f);
    float shade = 0.55f + 0.45f * (0.5f * l + 0.5f);
    auto ch = [](float v) -> uint32_t {
        int i = (int)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f);
        return (uint32_t)std::clamp(i, 0, 255);
    };
    return (0xE8u << 24) | (ch(r * shade) << 16) | (ch(g * shade) << 8) | ch(b * shade);
}

// 6 faces of the handle cube (outward-CCW in right-handed 3D), indices into the 8 corners built by build_cube().
constexpr uint8_t kCubeFaces[6][4] = {
    {0, 3, 2, 1}, // -w
    {4, 5, 6, 7}, // +w
    {0, 1, 5, 4}, // -v
    {3, 7, 6, 2}, // +v
    {0, 4, 7, 3}, // -u
    {1, 2, 6, 5}, // +u
};

inline void build_cube(Vec3 center, Vec3 axis, float half_size, Vec3 out_verts[8], Vec3 out_normals[6])
{
    Vec3 w = normalize_or(axis, {0.0f, 0.0f, 1.0f});
    Vec3 up = std::fabs(w.y) < 0.9f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
    Vec3 u = normalize_or(cross(up, w), {1.0f, 0.0f, 0.0f});
    Vec3 v = normalize_or(cross(w, u), {0.0f, 1.0f, 0.0f});
    Vec3 su = u * half_size;
    Vec3 sv = v * half_size;
    Vec3 sw = w * half_size;
    out_verts[0] = center - su - sv - sw;
    out_verts[1] = center + su - sv - sw;
    out_verts[2] = center + su + sv - sw;
    out_verts[3] = center - su + sv - sw;
    out_verts[4] = center - su - sv + sw;
    out_verts[5] = center + su - sv + sw;
    out_verts[6] = center + su + sv + sw;
    out_verts[7] = center - su + sv + sw;
    out_normals[0] = w * -1.0f;
    out_normals[1] = w;
    out_normals[2] = v * -1.0f;
    out_normals[3] = v;
    out_normals[4] = u * -1.0f;
    out_normals[5] = u;
}

// Tracks the baseline position & body depth of the currently selected part so repeated drags share the same limits,
// while external moves (native drag, Undo/Redo) automatically re-baseline.
struct DepthTracker {
    uintptr_t rigblock_key = 0;
    uint32_t part_inst = 0;
    Vec3 base_pos{};
    Vec3 last_written{};
    float base_depth = 0.5f;
    float offset = 0.0f;
    bool valid = false;

    void reset() { *this = DepthTracker{}; }

    bool sync(uintptr_t key, uint32_t inst, Vec3 cur_pos, float cur_spine_dist)
    {
        if (!valid || key != rigblock_key || inst != part_inst || len(cur_pos - last_written) > 1e-3f) {
            rigblock_key = key;
            part_inst = inst;
            base_pos = cur_pos;
            last_written = cur_pos;
            base_depth = cur_spine_dist;
            offset = 0.0f;
            valid = true;
            return true; // re-baselined
        }
        return false;
    }

    float apply(float target_offset, Vec3 normal, DepthLimits lim, Vec3& out_pos)
    {
        float clamped = std::clamp(target_offset, lim.min_offset, lim.max_offset);
        out_pos = base_pos + normal * clamped;
        offset = clamped;
        last_written = out_pos;
        return clamped;
    }
};

} // namespace editor_depth
