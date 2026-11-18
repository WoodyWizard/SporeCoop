// One authoritative city's economic state. Larger rosters use reliable splitting.
#pragma once
#include <cmath>
#include <vector>
#include "net/protocol.h"

namespace civ {
struct EconomicCity {
    uint8_t index = 0;
    uint32_t faction = 0;
    net::Vec3 position;
    void write(net::Writer& w) const {
        w.u8(index); w.u32(faction);
        w.f32(position.x); w.f32(position.y); w.f32(position.z);
    }
    bool read(net::Reader& r) {
        index=r.u8(); faction=r.u32();
        position={r.f32(),r.f32(),r.f32()};
        return r.ok() && index<250 && faction && std::isfinite(position.x) &&
               std::isfinite(position.y) && std::isfinite(position.z);
    }
};
struct EconomicRoute { EconomicCity partner; uint8_t kind = 0; };
struct EconomicCredit { uint32_t faction; float value; };
struct EconomicState {
    static constexpr size_t max_routes = 60;
    uint32_t world = 0, sequence = 0, buyer = 0xFFFFFFFFu;
    EconomicCity city;
    float progress = 0;
    std::vector<EconomicRoute> routes;
    std::vector<EconomicCredit> credits;
    void write(net::Writer& w) const {
        w.u32(world); w.u32(sequence); city.write(w);
        w.f32(progress); w.u32(buyer); w.u8(uint8_t(routes.size()));
        for (auto& route : routes) { route.partner.write(w); w.u8(route.kind); }
        w.u8(uint8_t(credits.size()));
        for (auto credit:credits) { w.u32(credit.faction); w.f32(credit.value); }
    }
    bool read(net::Reader& r) {
        world=r.u32(); sequence=r.u32();
        if (!city.read(r)) return false;
        progress=r.f32(); buyer=r.u32(); auto count=r.u8();
        if (!world || !std::isfinite(progress) || progress<0 || progress>100 || count>max_routes) return false;
        bool seen[250]{};
        routes.clear();
        for (unsigned i=0;i<count;++i) {
            EconomicRoute route;
            if (!route.partner.read(r)) return false;
            route.kind=r.u8();
            if (!r.ok() || route.kind>2 || route.partner.index==city.index || seen[route.partner.index]) return false;
            seen[route.partner.index]=true; routes.push_back(route);
        }
        count=r.u8();
        if (!r.ok() || count>max_routes) return false;
        credits.clear();
        for (unsigned i=0;i<count;++i) {
            EconomicCredit credit{r.u32(),r.f32()};
            if (!r.ok() || !credit.faction || !std::isfinite(credit.value)) return false;
            for (auto prior:credits) if (prior.faction==credit.faction) return false;
            credits.push_back(credit);
        }
        return r.ok() && !r.left();
    }
};
} // namespace civ
