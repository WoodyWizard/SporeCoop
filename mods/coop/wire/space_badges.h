// MSG_SPACE kinds 85..87 of space_badges.cpp (a guest's own badges and captain rank across a rejoin, BUG-0786):
// payload structs, the rules in wire/common.h. Every payload starts with {u8 to, u32 serial}.
#pragma once

#include <algorithm>
#include <utility>

#include "wire/common.h"

namespace wire::space_badges {

constexpr size_t kMaxEntries = 1000;

// the badge manager's tables: three lists of u16 n (at most 1000), n x {u32 key, i32 value}, then u32 the tracked
// card. A badge state outside 0..16 is no table (states 1..3 seen)
struct Badges {
    std::vector<std::pair<uint32_t, int32_t>> badge, stage, event;
    uint32_t card = 0;
    bool operator==(const Badges& o) const
    {
        return badge == o.badge && stage == o.stage && event == o.event && card == o.card;
    }
};
inline void put_list(net::Writer& w, const std::vector<std::pair<uint32_t, int32_t>>& v)
{
    uint16_t n = (uint16_t)std::min<size_t>(v.size(), kMaxEntries);
    w.u16(n);
    for (uint16_t i = 0; i < n; i++) {
        w.u32(v[i].first);
        w.u32((uint32_t)v[i].second);
    }
}
inline bool get_list(net::Reader& r, std::vector<std::pair<uint32_t, int32_t>>& v)
{
    v.clear();
    uint16_t n = r.u16();
    if (!r.ok() || n > kMaxEntries)
        return false;
    for (uint16_t i = 0; i < n && r.ok(); i++) {
        uint32_t k = r.u32();
        int32_t val = (int32_t)r.u32();
        v.push_back({k, val});
    }
    return r.ok();
}
inline void put_badges(net::Writer& w, const Badges& b)
{
    put_list(w, b.badge);
    put_list(w, b.stage);
    put_list(w, b.event);
    w.u32(b.card);
}
inline bool get_badges(net::Reader& r, Badges& b)
{
    if (!get_list(r, b.badge) || !get_list(r, b.stage) || !get_list(r, b.event))
        return false;
    b.card = r.u32();
    for (auto& [k, s] : b.badge)
        if (s < 0 || s > 16)
            return false;
    return r.ok();
}

// the header: {u8 to, u32 serial}
struct Head {
    uint8_t to = 0;
    uint32_t serial = 0;
    void write(net::Writer& w) const
    {
        w.u8(to);
        w.u32(serial);
    }
    bool read(net::Reader& r)
    {
        to = r.u8();
        serial = r.u32();
        return r.ok();
    }
};

// K_BADGES (guest -> host): {to 0, serial, u8 fresh, tables}: my badges now
struct Table : Head {
    bool fresh = false;
    Badges b;
    void write(net::Writer& w) const
    {
        Head::write(w);
        w.u8(fresh ? 1 : 0);
        put_badges(w, b);
    }
    bool read(net::Reader& r)
    {
        if (!Head::read(r))
            return false;
        fresh = r.u8() != 0;
        return get_badges(r, b);
    }
};

// K_BADGES_KEPT (host -> guest): {to, serial, u8 have[, tables]}: your own badges, kept since you were here
struct Kept : Head {
    bool have = false;
    Badges b;
    void write(net::Writer& w) const
    {
        Head::write(w);
        w.u8(have ? 1 : 0);
        if (have)
            put_badges(w, b);
    }
    bool read(net::Reader& r)
    {
        if (!Head::read(r))
            return false;
        have = r.u8() != 0;
        if (!r.ok())
            return false;
        b = Badges{};
        return !have || get_badges(r, b);
    }
};

// K_BADGES_DONE (guest -> host): {to 0, serial}: written (or not needed)
using Done = Head;

} // namespace wire::space_badges
