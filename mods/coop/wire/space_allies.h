// MSG_SPACE payloads of space_allies.cpp (kinds 60..61, partner colonies as an allied empire). Rules: wire/common.h.
#pragma once

#include <utility>

#include "common.h"

namespace wire::space_allies {

constexpr size_t kMaxRaids = 50; // the sender's cut (the reader takes what the count says)

// K_RAIDS 60 (any): u8 n, n x {u32 PlanetID, u8 bombers}: the raids on the sender's colonies right now
struct Raids {
    std::vector<std::pair<uint32_t, uint8_t>> raids;
    void write(net::Writer& w) const
    {
        size_t n = raids.size() < kMaxRaids ? raids.size() : kMaxRaids;
        w.u8((uint8_t)n);
        for (size_t i = 0; i < n; i++) {
            w.u32(raids[i].first);
            w.u8(raids[i].second);
        }
    }
    bool read(net::Reader& r)
    {
        raids.clear();
        uint8_t n = r.u8();
        for (uint8_t i = 0; i < n && r.ok(); i++) {
            uint32_t p = r.u32();
            uint8_t b = r.u8();
            if (r.ok())
                raids.push_back({p, b});
        }
        if (!r.ok())
            raids.clear(); // a cut list: the receiver keeps the raids it knows
        return r.ok();
    }
};

// K_DEFENDED 61 (any): u8 owner id, u32 PlanetID: the sender beat the raid on the owner's colony
struct Defended {
    uint8_t owner = 0;
    uint32_t planet = 0;
    void write(net::Writer& w) const
    {
        w.u8(owner);
        w.u32(planet);
    }
    bool read(net::Reader& r)
    {
        owner = r.u8();
        planet = r.u32();
        return r.ok();
    }
};

} // namespace wire::space_allies
