// Host-authoritative religious progress, without replaying conversion side effects.
#pragma once
#include "civ_economy.h"

namespace civ {
struct ReligiousFollowers { uint32_t faction; int32_t count; };
struct ReligiousState {
    static constexpr size_t max_factions = 128;
    uint32_t world = 0, sequence = 0, converter = 0xFFFFFFFFu;
    EconomicCity city;
    float progress = 0;
    std::vector<ReligiousFollowers> followers;
    void write(net::Writer& w) const {
        w.u32(world); w.u32(sequence); city.write(w);
        w.f32(progress); w.u32(converter); w.u8(uint8_t(followers.size()));
        for (auto entry:followers) { w.u32(entry.faction); w.u32(uint32_t(entry.count)); }
    }
    bool read(net::Reader& r) {
        world=r.u32(); sequence=r.u32();
        if (!city.read(r)) return false;
        progress=r.f32(); converter=r.u32(); auto count=r.u8();
        if (!world || !r.ok() || !std::isfinite(progress) || progress<0 || progress>100 || count>max_factions) return false;
        followers.clear();
        for (unsigned i=0;i<count;++i) {
            ReligiousFollowers entry{r.u32(),int32_t(r.u32())};
            if (!r.ok() || !entry.faction || entry.count<0) return false;
            for (auto prior:followers) if (prior.faction==entry.faction) return false;
            followers.push_back(entry);
        }
        return r.ok() && !r.left();
    }
};
} // namespace civ
