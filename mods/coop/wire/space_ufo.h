// MSG_UFO_LEAD (id 78, family space 1.3 of 0.7.17, BUG-1288, WP-S1): the NPC ships of a star system or planet without
// the host, simulated by one guest (the leader the host names) and shown by the other guests there, every message
// through the host. The host's own place keeps MSG_UFO_STATE / MSG_UFO_HIT / MSG_UFO_DAMAGE of 0.7.16 unchanged; a peer
// before space 1.3 is never sent MSG_UFO_LEAD and never named (squadron.cpp, peer_families / host_families).
// The idea and a first version: the outside branch fix/agy-open-bugs (its id 74, review out/research/agy-review-space.md).
//
// After the type byte:
//   K_STATE  (0, unreliable, leader -> host -> followers): u8 kind, u8 leader, u32 mode, u32 world, u16 n, n bytes: an
//            MSG_UFO_STATE body (a ship chunk or a shot list) of the leader's game
//   K_HIT    (1, reliable, follower -> host -> leader): u8 kind, u8 leader, u32 mode, u32 world, u8 shooter (the host
//            writes the sender's id), u16 ship id, f32 damage, i32 type, 3 x f32 dir
//   K_DAMAGE (2, reliable, leader -> host -> victim): u8 kind, u8 leader, u32 mode, u32 world, u8 victim, f32 damage,
//            i32 type, 3 x f32 dir, u16 attacker ship id
//   K_LEADS  (3, reliable, host -> guests): u8 kind, u8 n (<= 16), n x (u32 mode, u32 world, u8 leader): the places
//            without the host that have a guest leader (squadron_logic.h step_leads / announced)
// Rules of the wire structs: wire/common.h.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "common.h"
#include "../net/protocol.h"

namespace wire::space_ufo {

constexpr uint8_t kFamily = 8;          // the space family of net/messages.def
constexpr uint16_t kMinor = 3;          // space 1.3 (0.7.17) reads MSG_UFO_LEAD; 0.7.16.1 has space 1.2
constexpr size_t kMaxStateBody = 1150;  // one packet with the 12-byte header (kMaxPacket 1200 - 31 - 4 = 1165)
constexpr float kMaxDamage = 100000.f;  // as MSG_UFO_HIT's net::sane_damage bound
constexpr uint8_t kMaxLeads = 16;

enum Kind : uint8_t {
    K_STATE = 0,
    K_HIT = 1,
    K_DAMAGE = 2,
    K_LEADS = 3,
};

// does a peer whose HELLO / WELCOME listed `families` (`has`: it sent CAP_FAMILIES) read MSG_UFO_LEAD?
inline bool supports(bool has_families, const std::vector<net::FamilyRev>& families)
{
    if (!has_families)
        return false;
    for (const auto& f : families)
        if (f.family == kFamily)
            return f.rev == 1 && f.minor >= kMinor;
    return false;
}

// the kind byte of an MSG_UFO_LEAD payload, `r` not moved
inline bool peek_kind(const net::Reader& r, uint8_t& kind)
{
    if (!r.ok() || r.left() < 1)
        return false;
    kind = *r.cur();
    return true;
}

struct LeadState {
    uint8_t leader = 0;
    uint32_t mode = 0;
    uint32_t world = 0;
    std::vector<uint8_t> body;

    void write(net::Writer& w) const
    {
        uint16_t n = (uint16_t)std::min(body.size(), kMaxStateBody);
        w.u8(K_STATE);
        w.u8(leader);
        w.u32(mode);
        w.u32(world);
        w.u16(n);
        if (n)
            w.raw(body.data(), n);
    }

    bool read(net::Reader& r)
    {
        uint8_t kind = r.u8();
        leader = r.u8();
        mode = r.u32();
        world = r.u32();
        uint16_t n = r.u16();
        body.clear();
        if (!r.ok() || kind != K_STATE || leader == 0 || world == 0 || n == 0 || n > kMaxStateBody || r.left() < n)
            return false;
        body.assign(r.cur(), r.cur() + n);
        r.skip(n);
        return r.ok();
    }
};

struct LeadHit {
    uint8_t leader = 0;
    uint32_t mode = 0;
    uint32_t world = 0;
    uint8_t shooter = 0;
    uint16_t ship_id = 0;
    float dmg = 0;
    int32_t type = 0;
    float dir[3] = {0, 0, 1};

    void write(net::Writer& w) const
    {
        w.u8(K_HIT);
        w.u8(leader);
        w.u32(mode);
        w.u32(world);
        w.u8(shooter);
        w.u16(ship_id);
        w.f32(dmg);
        w.u32((uint32_t)type);
        w.f32(dir[0]);
        w.f32(dir[1]);
        w.f32(dir[2]);
    }

    bool read(net::Reader& r)
    {
        uint8_t kind = r.u8();
        leader = r.u8();
        mode = r.u32();
        world = r.u32();
        shooter = r.u8();
        ship_id = r.u16();
        dmg = r.f32();
        type = (int32_t)r.u32();
        for (float& d : dir)
            d = r.f32();
        return r.ok() && kind == K_HIT && leader != 0 && world != 0 && shooter != 0 && shooter != leader &&
               std::isfinite(dmg) && dmg >= 0.f && dmg <= kMaxDamage && wire::finite3(dir);
    }
};

struct LeadDamage {
    uint8_t leader = 0;
    uint32_t mode = 0;
    uint32_t world = 0;
    uint8_t victim = 0;
    float dmg = 0;
    int32_t type = 0;
    float dir[3] = {0, 0, 1};
    uint16_t attacker = 0xFFFF;

    void write(net::Writer& w) const
    {
        w.u8(K_DAMAGE);
        w.u8(leader);
        w.u32(mode);
        w.u32(world);
        w.u8(victim);
        w.f32(dmg);
        w.u32((uint32_t)type);
        w.f32(dir[0]);
        w.f32(dir[1]);
        w.f32(dir[2]);
        w.u16(attacker);
    }

    bool read(net::Reader& r)
    {
        uint8_t kind = r.u8();
        leader = r.u8();
        mode = r.u32();
        world = r.u32();
        victim = r.u8();
        dmg = r.f32();
        type = (int32_t)r.u32();
        for (float& d : dir)
            d = r.f32();
        attacker = r.u16();
        return r.ok() && kind == K_DAMAGE && leader != 0 && world != 0 && victim != 0 && victim != leader &&
               std::isfinite(dmg) && dmg >= 0.f && dmg <= kMaxDamage && wire::finite3(dir);
    }
};

struct LeadRow {
    uint32_t mode = 0;
    uint32_t world = 0;
    uint8_t leader = 0;
};

struct Leads {
    std::vector<LeadRow> rows;

    void write(net::Writer& w) const
    {
        uint8_t n = (uint8_t)std::min<size_t>(rows.size(), kMaxLeads);
        w.u8(K_LEADS);
        w.u8(n);
        for (uint8_t i = 0; i < n; i++) {
            w.u32(rows[i].mode);
            w.u32(rows[i].world);
            w.u8(rows[i].leader);
        }
    }

    bool read(net::Reader& r)
    {
        uint8_t kind = r.u8();
        uint8_t n = r.u8();
        rows.clear();
        if (!r.ok() || kind != K_LEADS || n > kMaxLeads)
            return false;
        for (uint8_t i = 0; i < n; i++) {
            LeadRow x;
            x.mode = r.u32();
            x.world = r.u32();
            x.leader = r.u8();
            if (!r.ok() || x.world == 0 || x.leader == 0 || x.leader == 0xFF)
                return false;
            rows.push_back(x);
        }
        return r.ok();
    }
};

} // namespace wire::space_ufo
