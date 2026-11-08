// F2 of 0.7.12 "Find each other" (docs/plan-0.7.12.md, docs/08-ui.md#ui-find-each-other): the portable part.
//   * the player colours: a palette of 8, the host gives every player one (the first free of the first 4 in join
//     order), a player can ask for another free one (the lobby); MSG_COLORS carries the table and the requests;
//   * the arrow geometry: where on the screen edge the arrow of a partner off screen goes and where it points;
//   * the minimap's triangle placement (a world offset to minimap pixels).
// No game calls here: `make test` checks it (tests/validate_tests.cpp test_find).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "net/buffer.h"

namespace findx {

// ---- the player colours ----
constexpr int kColors = 8;
constexpr int kAuto = 4; // auto-assigned: the first free of these, in join order
struct ColorDef {
    const char* key;   // the console / UI name
    const char* en;
    const char* ru;
    uint32_t argb;
};
inline const ColorDef kPalette[kColors] = {
    {"yellow", "Yellow", "Жёлтый", 0xFFFFD040}, {"blue", "Blue", "Синий", 0xFF4FA8FF},
    {"pink", "Pink", "Розовый", 0xFFFF5FC8},    {"green", "Green", "Зелёный", 0xFF5CE05C},
    {"orange", "Orange", "Оранжевый", 0xFFFF9030}, {"purple", "Purple", "Фиолетовый", 0xFFB07CFF},
    {"cyan", "Cyan", "Бирюзовый", 0xFF40E0E0},  {"red", "Red", "Красный", 0xFFFF5050},
};

inline int color_find(const std::string& key)
{
    for (int i = 0; i < kColors; i++)
        if (key == kPalette[i].key)
            return i;
    char* e = nullptr;
    long v = strtol(key.c_str(), &e, 10);
    if (!key.empty() && e && !*e && v >= 0 && v < kColors)
        return (int)v;
    return -1;
}

// id -> palette index (-1: none); ids are the session's player ids (0 = the host)
struct Colors {
    std::array<int8_t, 256> c;
    Colors() { c.fill(-1); }
    int of(uint8_t id) const { return c[id]; }
    bool taken(int idx, int except = -1) const
    {
        for (int i = 0; i < 256; i++)
            if (i != except && c[i] == idx)
                return true;
        return false;
    }
    bool operator==(const Colors& o) const { return c == o.c; }
};

// the host gives a newcomer (or itself) a colour: its own if it has one, else the first free of the first kAuto,
// else the first free one, else by its id
inline int auto_pick(Colors& t, uint8_t id)
{
    if (t.c[id] >= 0)
        return t.c[id];
    int pick = -1;
    for (int i = 0; i < kColors && pick < 0; i++)
        if (!t.taken(i))
            pick = i;
    if (pick < 0)
        pick = id % kColors;
    t.c[id] = (int8_t)pick;
    return pick;
}

// a player asks for colour idx: false when it is not a colour or another player has it (*why says which)
enum class Refused { None, Range, Taken };
inline Refused can_take(const Colors& t, uint8_t id, int idx)
{
    if (idx < 0 || idx >= kColors)
        return Refused::Range;
    if (t.taken(idx, id))
        return Refused::Taken;
    return Refused::None;
}

// ---- MSG_COLORS: u8 kind; 0 host -> guests the table {u8 n, n x {u8 id, u8 idx}} (reliable, on a join, a leave
// and every change); 1 guest -> host the request {u8 idx}
inline void write_table(net::Writer& w, const Colors& t)
{
    w.u8(0);
    int n = 0;
    for (int i = 0; i < 256; i++)
        n += t.c[i] >= 0;
    w.u8((uint8_t)n);
    for (int i = 0; i < 256; i++)
        if (t.c[i] >= 0) {
            w.u8((uint8_t)i);
            w.u8((uint8_t)t.c[i]);
        }
}

inline void write_request(net::Writer& w, int idx)
{
    w.u8(1);
    w.u8((uint8_t)idx);
}

// kind 0: `out` replaced by the host's table (false: cut short or a bad index; nothing changes)
inline bool read_table(net::Reader& r, Colors& out)
{
    Colors t;
    unsigned n = r.u8();
    if (!r.ok())
        return false;
    for (unsigned i = 0; i < n; i++) {
        uint8_t id = r.u8(), idx = r.u8();
        if (!r.ok() || idx >= kColors)
            return false;
        t.c[id] = (int8_t)idx;
    }
    out = t;
    return true;
}

// ---- the arrow: a partner off screen ----
// sx, sy: its projected position (filled: the projection gave a point, i.e. in front of the camera); on: the
// projection says it is in the view. The arrow sits on an ellipse-ish rectangle inside the screen edge (margins mx,
// my) on the ray from the centre towards it and points along that ray. Behind the camera (not filled) the caller
// passes the direction it found (a point on the way that is in front); none at all: straight down.
struct Arrow {
    bool on_screen = false; // the partner is in the view: no arrow, its name tag shows
    float x = 0, y = 0;     // the arrow's tip
    float ang = 0;          // radians, screen space (x right, y down): atan2(dy, dx) of the direction
};

inline bool in_view(float sx, float sy, int sw, int sh, float pad = 0)
{
    return sx >= pad && sy >= pad && sx < sw - pad && sy < sh - pad;
}

inline bool arrow_place(bool filled, bool on, float sx, float sy, int sw, int sh, float mx, float my, Arrow& a)
{
    a = Arrow{};
    if (sw <= 0 || sh <= 0)
        return false;
    if (filled && (!std::isfinite(sx) || !std::isfinite(sy)))
        return false;
    if (filled && on && in_view(sx, sy, sw, sh)) {
        a.on_screen = true;
        a.x = sx, a.y = sy;
        return true;
    }
    float cx = sw / 2.f, cy = sh / 2.f;
    float dx = filled ? sx - cx : 0.f, dy = filled ? sy - cy : 1.f;
    if (std::fabs(dx) < 1e-3f && std::fabs(dy) < 1e-3f)
        dy = 1.f;
    float kx = std::fabs(dx) > 1e-6f ? (cx - mx) / std::fabs(dx) : 1e30f;
    float ky = std::fabs(dy) > 1e-6f ? (cy - my) / std::fabs(dy) : 1e30f;
    float k = std::min(kx, ky);
    a.x = std::clamp(cx + dx * k, mx, sw - mx);
    a.y = std::clamp(cy + dy * k, my, sh - my);
    a.ang = std::atan2(dy, dx);
    return true;
}

// the arrow's shape: a chevron-like head (tip at the arrow point) of `size` px, as triangles (6 floats each);
// grow: the outline (the same shape `grow` px larger, drawn first in a dark colour). Returns the triangle count.
inline int arrow_tris(const Arrow& a, float size, float grow, float* out /* >= 12 floats */)
{
    float c = std::cos(a.ang), s = std::sin(a.ang);
    auto P = [&](float fx, float fy, float* o) { // forward / side in arrow space -> screen
        o[0] = a.x + fx * c - fy * s;
        o[1] = a.y + fx * s + fy * c;
    };
    float L = size + grow * 2.f, W = size * 0.62f + grow, notch = size * 0.32f;
    float back = -L;
    // two triangles: tip - left wing - notch, tip - notch - right wing
    P(grow, 0, out + 0);
    P(back, -W, out + 2);
    P(back + notch, 0, out + 4);
    P(grow, 0, out + 6);
    P(back + notch, 0, out + 8);
    P(back, W, out + 10);
    return 2;
}

// where the label (the name and the distance) goes: inside the screen from the arrow, never under it
inline void label_at(const Arrow& a, float size, int text_w, int sw, int sh, int& x, int& y)
{
    float c = std::cos(a.ang), s = std::sin(a.ang);
    float bx = a.x - c * (size * 1.6f + 14.f), by = a.y - s * (size * 1.6f + 10.f);
    x = (int)std::clamp(bx - text_w / 2.f, 4.f, (float)std::max(4, sw - text_w - 4));
    y = (int)std::clamp(by - 10.f, 4.f, (float)std::max(4, sh - 24));
}

// "80 m", "1.2 km" (Space: light years are the galaxy map's, not here)
inline std::string dist_text(float m)
{
    char b[32];
    if (!std::isfinite(m) || m < 0)
        return "";
    if (m < 1000.f)
        snprintf(b, sizeof b, "%d m", (int)(m + 0.5f));
    else
        snprintf(b, sizeof b, "%.1f km", m / 1000.f);
    return b;
}

// the angle between two screen directions, degrees (0..180)
inline float angle_diff_deg(float a, float b)
{
    float d = std::fabs(a - b);
    while (d > 6.2831853f)
        d -= 6.2831853f;
    if (d > 3.14159265f)
        d = 6.2831853f - d;
    return d * 57.29578f;
}

// ---- the minimap triangle: a world offset (east / north metres from the map's centre, in the map's own axes) to
// pixels from the map's centre; false when outside the map's disc (radius r_px). scale: px per metre
inline bool map_place(float ex, float ny, float scale, float r_px, float& px, float& py)
{
    px = ex * scale;
    py = -ny * scale;
    float d = std::sqrt(px * px + py * py);
    if (!std::isfinite(d))
        return false;
    if (d > r_px) { // at the rim, pointing out: the partner is beyond the map's reach
        px *= r_px / d;
        py *= r_px / d;
        return false;
    }
    return true;
}

} // namespace findx
