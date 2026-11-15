// MSG_SPACE kinds 10..15 of space_map.cpp (the galaxy map roster, a guest's kept colonies and their cities): payload structs, the rules
// in wire/common.h.
#pragma once

#include "../space_map.h" // (not this file: the feature header next to the wire directory)
#include "common.h"

#include <cmath>
#include <cstring>

namespace wire::space_map {

// a roster's colony: u32 planet, str8 name (64 bytes at most), str8 star name (64), 3 x u32 spice key, str8 spice name
// (32), u16 cities, u8 home. read: star is the planet's low 24 bits
inline void put_colony(net::Writer& w, const spacemap::Colony& c)
{
    w.u32(c.planet);
    w.str8(c.name.substr(0, 64));
    w.str8(c.star_name.substr(0, 64));
    for (uint32_t k : c.spice)
        w.u32(k);
    w.str8(c.spice_name.substr(0, 32));
    w.u16(c.cities);
    w.u8(c.home ? 1 : 0);
}
inline spacemap::Colony get_colony(net::Reader& r)
{
    spacemap::Colony c;
    c.planet = r.u32();
    c.star = c.planet & 0xFFFFFFu;
    c.name = r.str8();
    c.star_name = r.str8();
    for (uint32_t& k : c.spice)
        k = r.u32();
    c.spice_name = r.str8();
    c.cities = r.u16();
    c.home = r.u8() & 1;
    return c;
}

// the roster's head (part 0): u32 home star, u32 home planet, u32 place, 3 x f32 galaxy position (0 without `with_pos`),
// str8 empire name (64), str8 name of the star it is at (64), u32 colour, u32 political id, u32 money.
// read: star_now is the caller's (from place)
inline void put_head(net::Writer& w, const spacemap::Roster& r, bool with_pos)
{
    w.u32(r.home_star);
    w.u32(r.home_planet);
    w.u32(r.place);
    w.f32(with_pos ? r.galaxy_pos.x : 0.f);
    w.f32(with_pos ? r.galaxy_pos.y : 0.f);
    w.f32(with_pos ? r.galaxy_pos.z : 0.f);
    w.str8(r.empire_name.substr(0, 64));
    w.str8(r.star_now_name.substr(0, 64));
    w.u32(r.color);
    w.u32(r.political_id);
    w.u32((uint32_t)r.money);
}
inline void get_head(net::Reader& rd, spacemap::Roster& r)
{
    r.home_star = rd.u32();
    r.home_planet = rd.u32();
    r.place = rd.u32();
    r.galaxy_pos.x = rd.f32();
    r.galaxy_pos.y = rd.f32();
    r.galaxy_pos.z = rd.f32();
    r.empire_name = rd.str8();
    r.star_now_name = rd.str8();
    r.color = rd.u32();
    r.political_id = rd.u32();
    r.money = (int)rd.u32();
}

// K_ROSTER (any): one part of a player's roster: u32 version, u8 part, u8 parts, [part 0: head], u8 n, n colonies.
// read: false for a cut part; head_ok tells whether version / part / parts were readable and part < parts (the
// receiver's parts bookkeeping uses them for a cut part as before)
struct Roster {
    uint32_t version = 0;
    uint8_t part = 0, parts = 1;
    spacemap::Roster head;                   // part 0 only (with its galaxy position)
    std::vector<spacemap::Colony> colonies;  // at most 255
    bool head_ok = false;
    void write(net::Writer& w) const
    {
        w.u32(version);
        w.u8(part);
        w.u8(parts);
        if (part == 0)
            put_head(w, head, true);
        uint8_t n = (uint8_t)(colonies.size() < 255 ? colonies.size() : 255);
        w.u8(n);
        for (uint8_t i = 0; i < n; i++)
            put_colony(w, colonies[i]);
    }
    bool read(net::Reader& r)
    {
        version = r.u32();
        part = r.u8();
        parts = r.u8();
        head_ok = r.ok() && parts && part < parts;
        if (!head_ok)
            return false;
        head = spacemap::Roster{};
        if (part == 0)
            get_head(r, head);
        colonies.clear();
        uint8_t n = r.u8();
        for (uint8_t k = 0; k < n && r.ok(); k++) {
            spacemap::Colony c = get_colony(r);
            if (r.ok())
                colonies.push_back(std::move(c));
        }
        return r.ok();
    }
};

// K_ROSTER_REQ (any): nothing; everybody sends its roster now
struct RosterReq {
    void write(net::Writer&) const {}
    bool read(net::Reader& r) { return r.ok(); }
};

// K_KEPT (host -> guest, BUG-0195): u8 target id, u8 n, n x u32 PlanetID (at most 200): its own colonies, kept
struct Kept {
    uint8_t to = 0;
    std::vector<uint32_t> planets;
    void write(net::Writer& w) const
    {
        w.u8(to);
        put_u32s8(w, planets, 200);
    }
    bool read(net::Reader& r)
    {
        to = r.u8();
        return r.ok() && get_u32s8(r, planets);
    }
};

// K_KEPT_DONE (guest -> host): u8 n, n x u32 PlanetID it could not make its own (the host drops them).
// C6: a cut list is not used (before: the planets read until the cut were dropped and the restore counted as done;
// now the host keeps waiting and sends K_KEPT again after 10 s; a whole list never comes cut from a SporeCoop peer)
struct KeptDone {
    std::vector<uint32_t> failed;
    void write(net::Writer& w) const { put_u32s8(w, failed); }
    bool read(net::Reader& r) { return get_u32s8(r, failed); }
};

// BUG-1151: a guest colony's cities as the guest's game keeps them in the planet record (cCivData +0x3C: cCityData,
// docs/re/space.md#space-colony-cities): what a restored colony gets back instead of FillPlanetDataForTechLevel's one
// generated city. The blob (u8 version 1, u8 n cities at most 16, per city: 3 f32 position, 4 f32 orientation, f32
// spice (+0x1C), i32 max size (+0x20), i32 size (+0x24), u8 turret bits (+0x28), i32 income (+0x2C), f32 happiness
// (+0x30), i32 specialty (+0x34), u32 wall style (+0xB8), str8 name (UTF-8, 64 bytes), u8 n buildings at most 64,
// n x 0x38 raw bytes of the building entry (+0x7C, plain data)).
constexpr size_t kCityMax = 16, kBuildingMax = 64, kBuildingBytes = 0x38;
struct City {
    float pos[3] = {}, rot[4] = {0, 0, 0, 1}, spice = 0, happy = 0;
    int32_t max_size = 0, size = 0, income = 0, specialty = -1;
    uint8_t turrets = 0;
    uint32_t wall = 0;
    std::string name;
    std::vector<std::vector<uint8_t>> buildings;
    bool operator==(const City& o) const
    {
        return !memcmp(pos, o.pos, sizeof pos) && !memcmp(rot, o.rot, sizeof rot) && spice == o.spice && happy == o.happy &&
               max_size == o.max_size && size == o.size && income == o.income && specialty == o.specialty &&
               turrets == o.turrets && wall == o.wall && name == o.name && buildings == o.buildings;
    }
};
inline std::vector<uint8_t> put_cities(const std::vector<City>& v)
{
    net::Writer w;
    w.u8(1);
    uint8_t n = (uint8_t)std::min<size_t>(v.size(), kCityMax);
    w.u8(n);
    for (uint8_t i = 0; i < n; i++) {
        const City& c = v[i];
        for (float f : c.pos)
            w.f32(f);
        for (float f : c.rot)
            w.f32(f);
        w.f32(c.spice);
        w.u32((uint32_t)c.max_size);
        w.u32((uint32_t)c.size);
        w.u8(c.turrets);
        w.u32((uint32_t)c.income);
        w.f32(c.happy);
        w.u32((uint32_t)c.specialty);
        w.u32(c.wall);
        w.str8(c.name.substr(0, std::min<size_t>(c.name.size(), 64)));
        uint8_t nb = (uint8_t)std::min<size_t>(c.buildings.size(), kBuildingMax);
        w.u8(nb);
        for (uint8_t b = 0; b < nb; b++) {
            std::vector<uint8_t> e = c.buildings[b];
            e.resize(kBuildingBytes);
            w.raw(e.data(), e.size());
        }
    }
    return w.data;
}
inline bool get_cities(const std::vector<uint8_t>& d, std::vector<City>& v)
{
    v.clear();
    net::Reader r(d.data(), d.size());
    if (r.u8() != 1)
        return false;
    uint8_t n = r.u8();
    if (!r.ok() || n > kCityMax)
        return false;
    for (uint8_t i = 0; i < n && r.ok(); i++) {
        City c;
        for (float& f : c.pos)
            f = r.f32();
        for (float& f : c.rot)
            f = r.f32();
        c.spice = r.f32();
        c.max_size = (int32_t)r.u32();
        c.size = (int32_t)r.u32();
        c.turrets = r.u8();
        c.income = (int32_t)r.u32();
        c.happy = r.f32();
        c.specialty = (int32_t)r.u32();
        c.wall = r.u32();
        c.name = r.str8();
        uint8_t nb = r.u8();
        if (!r.ok() || nb > kBuildingMax || c.name.size() > 64)
            return false;
        for (uint8_t b = 0; b < nb && r.ok(); b++) {
            std::vector<uint8_t> e(kBuildingBytes);
            r.raw(e.data(), e.size());
            c.buildings.push_back(std::move(e));
        }
        // the game's own ranges (a cut or crafted blob is no colony): finite numbers, sizes 0..64
        bool fin = true;
        for (float f : c.pos)
            fin &= std::isfinite(f) && std::fabs(f) < 1e6f;
        for (float f : c.rot)
            fin &= std::isfinite(f) && std::fabs(f) <= 1.01f;
        if (!fin || !std::isfinite(c.spice) || c.spice < 0 || c.spice > 1e6f || !std::isfinite(c.happy) || c.size < 0 ||
            c.size > 64 || c.max_size < 0 || c.max_size > 64)
            return false;
        v.push_back(std::move(c));
    }
    return r.ok() && r.left() == 0;
}

// K_COLONY (guest -> host, 14) and K_KEPT_COLONY (host -> guest, 15): one part of a colony's city blob, sent in parts of
// at most kPartBytes: u8 to (0: the host), u32 PlanetID, u32 hash of the whole blob, u16 total, u16 offset, u16 n, n bytes
constexpr size_t kPartBytes = 900, kBlobMax = 60000;
struct ColonyPart {
    uint8_t to = 0;
    uint32_t planet = 0, hash = 0;
    uint16_t total = 0, offset = 0;
    std::vector<uint8_t> bytes;
    void write(net::Writer& w) const
    {
        w.u8(to);
        w.u32(planet);
        w.u32(hash);
        w.u16(total);
        w.u16(offset);
        w.u16((uint16_t)bytes.size());
        w.raw(bytes.data(), bytes.size());
    }
    bool read(net::Reader& r)
    {
        to = r.u8();
        planet = r.u32();
        hash = r.u32();
        total = r.u16();
        offset = r.u16();
        uint16_t n = r.u16();
        if (!r.ok() || n > kPartBytes || total > kBlobMax || (size_t)offset + n > total || n > r.left())
            return false;
        bytes.resize(n);
        r.raw(bytes.data(), n);
        return r.ok();
    }
};

} // namespace wire::space_map
