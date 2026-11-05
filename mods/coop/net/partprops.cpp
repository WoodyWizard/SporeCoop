// BUG-0559: part descriptions and the most similar part (partprops.h)
#include "partprops.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace partprops {

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

// the size of a single value of a property type (0: not a type this code knows)
static size_t value_size(uint16_t t)
{
    switch (t) {
    case 0x01: return 1;  // bool
    case 0x09: return 4;  // int32
    case 0x0A: return 4;  // uint32
    case 0x0D: return 4;  // float
    case 0x20: return 16; // key (instance, type, group, pad)
    case 0x22: return 0x208; // text (table, instance, 256 UTF-16 chars)
    case 0x30: return 8;  // vector2
    case 0x31: return 16; // vector3 (+ pad)
    case 0x32: return 16; // colorRGB
    case 0x33: return 16; // vector4
    case 0x34: return 16; // colorRGBA
    case 0x38: return 56; // transform
    case 0x39: return 24; // bbox
    default: return 0;
    }
}

float Part::size() const
{
    if (!has_box)
        return 0;
    float dx = box[3] - box[0], dy = box[4] - box[1], dz = box[5] - box[2];
    float s = std::sqrt(dx * dx + dy * dy + dz * dz);
    return std::isfinite(s) ? s : 0;
}

bool parse(const uint8_t* d, size_t n, uint32_t id, Part& out)
{
    out = Part{};
    out.id = id;
    if (!d || n < 4)
        return false;
    uint32_t cnt = be32(d);
    if (cnt > 4096)
        return false;
    size_t p = 4;
    for (uint32_t i = 0; i < cnt; i++) {
        if (n - p < 8)
            return false;
        uint32_t pid = be32(d + p);
        uint16_t t = be16(d + p + 4), fl = be16(d + p + 6);
        p += 8;
        out.props.push_back(pid);
        if (fl & 0x10) { // an array: count, item size, items (the strings of a part are never arrays)
            if (n - p < 8)
                return false;
            uint64_t c = be32(d + p), isz = be32(d + p + 4);
            p += 8;
            if (c * isz > n - p)
                return false;
            if (pid == 0x1CBB2F62 && t == 0x39 && c >= 1 && isz >= 24) { // the bounding box
                for (int k = 0; k < 6; k++)
                    memcpy(&out.box[k], d + p + 4 * k, 4);
                bool ok = true;
                for (float v : out.box)
                    ok &= std::isfinite(v) && std::fabs(v) < 1e4f;
                out.has_box = ok;
            }
            p += (size_t)(c * isz);
            continue;
        }
        if (t == 0x12 || t == 0x13) { // a string: length in characters, then the characters
            if (n - p < 4)
                return false;
            uint64_t len = (uint64_t)be32(d + p) * (t == 0x13 ? 2 : 1);
            p += 4;
            if (len > n - p)
                return false;
            p += (size_t)len;
            continue;
        }
        size_t sz = value_size(t);
        if (!sz || sz > n - p)
            return false;
        if (t == 0x09) {
            int v = (int)be32(d + p);
            if ((pid & 0x00FFFF00u) == 0x00C3E500u)
                out.abil.push_back({pid, v});
            else if (pid == 0x02166464)
                out.cost = v;
            else if (pid == 0x3C652302)
                out.rank[0] = v;
            else if (pid == 0x51FA9DFA)
                out.rank[1] = v;
            else if (pid == 0x03A289AC)
                out.rank[2] = v;
        }
        if ((pid & 0xFFFFFF00u) == 0xB00F0F00u)
            out.roles.push_back(pid);
        p += sz;
    }
    std::sort(out.roles.begin(), out.roles.end());
    out.roles.erase(std::unique(out.roles.begin(), out.roles.end()), out.roles.end());
    std::sort(out.abil.begin(), out.abil.end());
    std::sort(out.props.begin(), out.props.end());
    out.props.erase(std::unique(out.props.begin(), out.props.end()), out.props.end());
    return true;
}

int fe_count(uint32_t id)
{
    int c = 0;
    for (int i = 0; i < 4; i++)
        c += ((id >> (8 * i)) & 0xFF) == 0xFE;
    return c;
}

// the weights of tools/part_substitutes.py (the Creepy & Cute table), and the size: a part twice as
// large (or half as large) costs 3, as much as a missing ability
double distance(const Part& a, const Part& b)
{
    if (a.roles != b.roles)
        return std::numeric_limits<double>::infinity();
    double s = 0;
    size_t i = 0, j = 0;
    while (i < a.abil.size() || j < b.abil.size()) { // ability levels: each level apart 1, missing / extra 3
        if (j >= b.abil.size() || (i < a.abil.size() && a.abil[i].first < b.abil[j].first)) {
            s += std::fabs((double)a.abil[i].second) + 3;
            i++;
        } else if (i >= a.abil.size() || b.abil[j].first < a.abil[i].first) {
            s += std::fabs((double)b.abil[j].second) + 3;
            j++;
        } else {
            s += std::fabs((double)a.abil[i].second - (double)b.abil[j].second);
            i++;
            j++;
        }
    }
    static const int w[3] = {1, 1, 2};
    for (int k = 0; k < 3; k++) {
        int x = a.rank[k], y = b.rank[k];
        if (x == y)
            continue;
        s += w[k] * (x != kNone && y != kNone ? std::min(std::fabs((double)x - (double)y), 1e3) : 3.0);
    }
    if (a.cost != kNone && b.cost != kNone)
        s += std::min(std::fabs((double)a.cost - (double)b.cost), 1e5) / 100.0;
    size_t common = 0;
    for (i = 0, j = 0; i < a.props.size() && j < b.props.size();) {
        if (a.props[i] == b.props[j]) {
            common++;
            i++;
            j++;
        } else if (a.props[i] < b.props[j])
            i++;
        else
            j++;
    }
    s += 0.2 * (double)(a.props.size() + b.props.size() - 2 * common);
    float sa = a.size(), sb = b.size();
    if (sa > 1e-4f && sb > 1e-4f)
        s += 3.0 * std::fabs(std::log((double)sa / sb)) / std::log(2.0);
    return s;
}

uint32_t nearest(const Part& want, const std::vector<Part>& have)
{
    uint32_t best = 0;
    double bd = std::numeric_limits<double>::infinity();
    int fe = fe_count(want.id);
    for (auto& h : have) {
        if (h.id == want.id || !h.id || fe_count(h.id) != fe)
            continue;
        double d = distance(want, h);
        if (d < bd || (d == bd && best && h.id < best)) {
            bd = d;
            best = h.id;
        }
    }
    return std::isfinite(bd) ? best : 0;
}

} // namespace partprops
