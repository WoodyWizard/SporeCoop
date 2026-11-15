// MSG_SPACE payloads of space_planets.cpp (kinds 80..81, a shared colony's stored spice, BUG-0537). Rules: wire/common.h.
#pragma once

#include <utility>

#include "common.h"

namespace wire::space_planets {

constexpr size_t kMaxColonies = 48, kMaxCities = 16; // per K_SPICE part / per colony (space_planets.cpp)

// K_SPICE 80 (host): u32 empire pol, u8 n, n x {u32 PlanetID, u8 cities, cities x f32 stored spice}. The host sends
// at most kMaxColonies per part. Read as before: at most kMaxColonies colonies and kMaxCities values of each are taken;
// a colony with more values than that is not skipped (the next colony is read from the following bytes, as the hand
// parse did); the list keeps the order and repeats of the message (the handler appends a repeated planet's values).
struct Spice {
    uint32_t pol = 0;
    std::vector<std::pair<uint32_t, std::vector<float>>> colonies;
    void write(net::Writer& w) const
    {
        w.u32(pol);
        w.u8((uint8_t)colonies.size());
        for (auto& [id, v] : colonies) {
            w.u32(id);
            w.u8((uint8_t)v.size());
            for (float x : v)
                w.f32(x);
        }
    }
    bool read(net::Reader& r)
    {
        pol = r.u32();
        uint8_t n = r.u8();
        colonies.clear();
        for (int k = 0; k < n && k < (int)kMaxColonies; k++) {
            uint32_t id = r.u32();
            uint8_t m = r.u8();
            std::vector<float> v;
            for (int i = 0; i < m && i < (int)kMaxCities; i++)
                v.push_back(r.f32());
            colonies.push_back({id, std::move(v)});
        }
        return r.ok();
    }
};

// K_SPICE_TAKEN 81 (guest): u32 empire pol, u32 PlanetID, u8 cities, cities x f32 what is left in the sender's game.
// At most kMaxCities values are taken (the rest of a longer list is left unread, as before).
struct SpiceTaken {
    uint32_t pol = 0, id = 0;
    std::vector<float> left;
    void write(net::Writer& w) const
    {
        w.u32(pol);
        w.u32(id);
        w.u8((uint8_t)left.size());
        for (float x : left)
            w.f32(x);
    }
    bool read(net::Reader& r)
    {
        pol = r.u32();
        id = r.u32();
        uint8_t n = r.u8();
        left.clear();
        for (int i = 0; i < n && i < (int)kMaxCities; i++)
            left.push_back(r.f32());
        return r.ok();
    }
};

} // namespace wire::space_planets
