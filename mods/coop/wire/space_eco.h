// MSG_SPACE payloads of space_eco.cpp (kind 90, a planet's ecosystem: the plant and animal species placed on it,
// BUG-1148, docs/re/space.md#space-ecosystem). Rules: wire/common.h.
#pragma once

#include "common.h"

namespace wire::space_eco {

// the game keeps at most 9 plant and 9 animal species per planet (docs/re/space.md#space-ecosystem); a list longer
// than this is no peer's
constexpr size_t kMaxSpecies = 12;

// one species of a planet's list: its key (cPlanetRecord +0xBC plants / +0xD0 animals) and where the sender's game
// placed it (planet coordinates; 0,0,0 when the sender does not know, e.g. it came with the planet)
struct Species {
    Key3 key;
    float pos[3] = {};
    bool operator==(const Species& o) const { return key == o.key; }
};

// K_ECO 90 (any): u32 PlanetID (index << 24 | star), u8 np, np x {key, 3 x f32 pos} the plant species in the record's
// order, u8 na, na x {key, 3 x f32 pos} the animal species. read() refuses a list over kMaxSpecies, a cut message and a
// non-finite position (no peer sends them).
struct Eco {
    uint32_t planet = 0;
    std::vector<Species> plants, animals;
    void write(net::Writer& w) const
    {
        w.u32(planet);
        for (auto* v : {&plants, &animals}) {
            w.u8((uint8_t)v->size());
            for (auto& s : *v) {
                put_key(w, s.key);
                for (float f : s.pos)
                    w.f32(f);
            }
        }
    }
    bool read(net::Reader& r)
    {
        planet = r.u32();
        for (auto* v : {&plants, &animals}) {
            v->clear();
            uint8_t n = r.u8();
            if (!r.ok() || n > kMaxSpecies || r.left() < (size_t)n * 24)
                return false;
            for (int i = 0; i < n; i++) {
                Species s;
                s.key = get_key(r);
                for (float& f : s.pos)
                    f = r.f32();
                if (!finite3(s.pos))
                    return false;
                v->push_back(s);
            }
        }
        return r.ok();
    }
};

} // namespace wire::space_eco
