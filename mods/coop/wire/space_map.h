// MSG_SPACE kinds 10..13 of space_map.cpp (the galaxy map roster, a guest's kept colonies): payload structs, the rules
// in wire/common.h.
#pragma once

#include "../space_map.h" // (not this file: the feature header next to the wire directory)
#include "wire/common.h"

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

} // namespace wire::space_map
