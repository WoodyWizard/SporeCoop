// MSG_SPACE kinds 100..102 of space_empires.cpp (every player's own empire in Space, BUG-9279): payload structs, the
// rules in wire/common.h. Every payload starts with {u8 to, u32 serial}.
#pragma once

#include <algorithm>
#include <vector>

#include "common.h"
#include "space_diplomacy.h"

namespace wire::space_empires {

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

// K_ASK (guest -> host): {to 0, serial 0}: my Space world opened, which empire is mine?
using Ask = Head;

// K_EMPIRE (host -> guest): {to, serial, u8 have, u8 seat, i32 money}: have 1 = your own empire (party seat `seat`),
// its money; have 0 = you play the host's empire (the shared money). A money outside 0..99 999 999 is no message.
struct Empire : Head {
    bool have = false;
    uint8_t seat = 0;
    int32_t money = 0;
    void write(net::Writer& w) const
    {
        Head::write(w);
        w.u8(have ? 1 : 0);
        w.u8(seat);
        w.u32((uint32_t)money);
    }
    bool read(net::Reader& r)
    {
        if (!Head::read(r))
            return false;
        have = r.u8() != 0;
        seat = r.u8();
        money = (int32_t)r.u32();
        return r.ok() && money >= 0 && money <= 99999999 && seat < 8;
    }
};

// K_REPORT (guest -> host): {to 0, serial (the K_EMPIRE it answers), i32 money}: my own empire's money now
struct Report : Head {
    int32_t money = 0;
    void write(net::Writer& w) const
    {
        Head::write(w);
        w.u32((uint32_t)money);
    }
    bool read(net::Reader& r)
    {
        if (!Head::read(r))
            return false;
        money = (int32_t)r.u32();
        return r.ok();
    }
};

// K_DIPLO (guest -> host: {to 0, serial (the K_EMPIRE it answers)}) and K_DIPLO_SET (host -> guest {to, serial (its
// K_EMPIRE)}): one part of an own empire's diplomacy table. {Head, u16 version, u8 idx, u8 count, u16 len, len bytes};
// count 0: no table (K_DIPLO_SET: the host has none for you). The parts joined are table_bytes().
constexpr size_t kPartMax = 900, kPartsMax = 64;
struct Part : Head {
    uint16_t version = 0;
    uint8_t idx = 0, count = 0;
    std::vector<uint8_t> data;
    void write(net::Writer& w) const
    {
        Head::write(w);
        w.u16(version);
        w.u8(idx);
        w.u8(count);
        w.u16((uint16_t)data.size());
        for (uint8_t b : data)
            w.u8(b);
    }
    bool read(net::Reader& r)
    {
        if (!Head::read(r))
            return false;
        version = r.u16();
        idx = r.u8();
        count = r.u8();
        const unsigned n = r.u16();
        if (!r.ok() || n > kPartMax || count > kPartsMax || (count && idx >= count) || (!count && n))
            return false;
        data.resize(n);
        for (unsigned i = 0; i < n; i++)
            data[i] = r.u8();
        return r.ok();
    }
};

// a diplomacy table as bytes: a K_SNAP of the diplomacy feature (last 1, reply_to 0)
inline std::vector<uint8_t> table_bytes(const std::vector<space_diplomacy::SnapRec>& t)
{
    space_diplomacy::Snap s;
    s.last = 1;
    s.reply_to = 0;
    s.recs = t;
    net::Writer w;
    s.write(w);
    return w.data;
}
inline bool table_read(const std::vector<uint8_t>& b, std::vector<space_diplomacy::SnapRec>& out)
{
    net::Reader r(b.data(), b.size());
    space_diplomacy::Snap s;
    if (!s.read(r) || r.left() || s.last != 1)
        return false;
    out = std::move(s.recs);
    return true;
}
// a table's bytes cut into parts (none: one part with count 0)
inline std::vector<Part> parts_of(const std::vector<uint8_t>& b, uint8_t to, uint32_t serial, uint16_t version)
{
    std::vector<Part> v;
    const size_t n = b.empty() ? 0 : (b.size() + kPartMax - 1) / kPartMax;
    if (n > kPartsMax)
        return v;
    for (size_t i = 0; i < (n ? n : 1); i++) {
        Part p;
        p.to = to;
        p.serial = serial;
        p.version = version;
        p.idx = (uint8_t)i;
        p.count = (uint8_t)n;
        if (n)
            p.data.assign(b.begin() + i * kPartMax, b.begin() + std::min(b.size(), (i + 1) * kPartMax));
        v.push_back(std::move(p));
    }
    return v;
}
// joins parts of one version: add() true when the table is whole (bytes in `out`)
struct Joiner {
    uint16_t version = 0;
    uint8_t count = 0;
    std::vector<std::vector<uint8_t>> got;
    std::vector<bool> have;
    bool add(const Part& p, std::vector<uint8_t>& out)
    {
        if (!p.count) {
            out.clear();
            got.clear();
            have.clear();
            count = 0;
            return true;
        }
        if (p.version != version || p.count != count || got.size() != p.count) {
            version = p.version;
            count = p.count;
            got.assign(p.count, {});
            have.assign(p.count, false);
        }
        got[p.idx] = p.data;
        have[p.idx] = true;
        for (bool h : have)
            if (!h)
                return false;
        out.clear();
        for (auto& g : got)
            out.insert(out.end(), g.begin(), g.end());
        got.clear();
        have.clear();
        count = 0;
        return true;
    }
};

} // namespace wire::space_empires
