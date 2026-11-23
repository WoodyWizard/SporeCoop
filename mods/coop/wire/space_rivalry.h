// MSG_SPACE kinds 50..57 of space_rivalry.cpp (Rivals mode: own budgets, relations, PvP): payload structs, the rules
// in wire/common.h.
#pragma once

#include <vector>

#include "common.h"
#include "../net/protocol.h"

namespace wire::space_rivalry {

constexpr uint8_t kFamily = 8;       // the space family of net/messages.def
constexpr uint16_t kAdoptMinor = 3;  // space 1.3 (0.7.17) takes K_SET have 3 (BUG-1482); 0.7.16.1 has space 1.2

// does a peer whose HELLO listed `families` (`has`: it sent CAP_FAMILIES) take K_SET have 3?
inline bool reads_adopt(bool has_families, const std::vector<net::FamilyRev>& families)
{
    if (!has_families)
        return false;
    for (const auto& f : families)
        if (f.family == kFamily)
            return f.rev == 1 && f.minor >= kAdoptMinor;
    return false;
}

// K_STATE (host): u8 rivals, u16 split seq, i32 split total, i32 split share, u16 merge seq, u8 n x {u8 id, i32 the
// budget the last merge counted}, u8 n x {u8 a, u8 b, u8 relation}, u8 n x {u8 from, u8 to, u8 kind} (the open
// proposals). The relation byte is as sent (the receiver takes values over 2 as 2, War)
struct State {
    struct Credit {
        uint8_t id;
        int32_t v;
    };
    struct Rel {
        uint8_t a, b, rel;
    };
    struct Prop {
        uint8_t from, to, kind;
    };
    bool rivals = false;
    uint16_t split = 0, merge = 0;
    int32_t total = 0, share = 0;
    std::vector<Credit> credits; // at most 255 of each list (4 players: a few)
    std::vector<Rel> rels;
    std::vector<Prop> props;
    void write(net::Writer& w) const
    {
        w.u8(rivals);
        w.u16(split);
        w.u32((uint32_t)total);
        w.u32((uint32_t)share);
        w.u16(merge);
        w.u8((uint8_t)credits.size());
        for (auto& c : credits) {
            w.u8(c.id);
            w.u32((uint32_t)c.v);
        }
        w.u8((uint8_t)rels.size());
        for (auto& x : rels) {
            w.u8(x.a);
            w.u8(x.b);
            w.u8(x.rel);
        }
        w.u8((uint8_t)props.size());
        for (auto& p : props) {
            w.u8(p.from);
            w.u8(p.to);
            w.u8(p.kind);
        }
    }
    bool read(net::Reader& r)
    {
        rivals = r.u8() != 0;
        split = r.u16();
        total = (int32_t)r.u32();
        share = (int32_t)r.u32();
        merge = r.u16();
        credits.clear();
        rels.clear();
        props.clear();
        uint8_t n = r.u8();
        for (int i = 0; i < n && r.ok(); i++) {
            Credit c;
            c.id = r.u8();
            c.v = (int32_t)r.u32();
            credits.push_back(c);
        }
        n = r.u8();
        for (int i = 0; i < n && r.ok(); i++) {
            Rel x;
            x.a = r.u8();
            x.b = r.u8();
            x.rel = r.u8();
            rels.push_back(x);
        }
        n = r.u8();
        for (int i = 0; i < n && r.ok(); i++) {
            Prop p;
            p.from = r.u8();
            p.to = r.u8();
            p.kind = r.u8();
            props.push_back(p);
        }
        return r.ok();
    }
};

// K_REQ (guest): u8 action, u8 target: a request the host decides
struct Req {
    uint8_t action = 0, target = 0;
    void write(net::Writer& w) const
    {
        w.u8(action);
        w.u8(target);
    }
    bool read(net::Reader& r)
    {
        action = r.u8();
        target = r.u8();
        return r.ok();
    }
};

// K_EVENT (host): u8 what, u8 a, u8 b: what happened (the line every game shows)
struct Event {
    uint8_t what = 0, a = 0, b = 0;
    void write(net::Writer& w) const
    {
        w.u8(what);
        w.u8(a);
        w.u8(b);
    }
    bool read(net::Reader& r)
    {
        what = r.u8();
        a = r.u8();
        b = r.u8();
        return r.ok();
    }
};

// K_HIT (any): u8 victim, f32 damage, u32 damage type, 3 x f32 direction: my shot hit your ship (at war).
// The damage is above 0 and at most 1e6 (the receiver dropped any other before, after its own checks: the same); a
// direction that is not finite is the receiver's to replace
struct Hit {
    uint8_t victim = 0;
    float dmg = 0;
    int type = 0;
    float dir[3] = {0, 0, 1};
    void write(net::Writer& w) const
    {
        w.u8(victim);
        w.f32(dmg);
        w.u32((uint32_t)type);
        w.f32(dir[0]);
        w.f32(dir[1]);
        w.f32(dir[2]);
    }
    bool read(net::Reader& r)
    {
        victim = r.u8();
        dmg = r.f32();
        type = (int)r.u32();
        for (float& d : dir)
            d = r.f32();
        return r.ok() && dmg > 0 && dmg <= 1e6f;
    }
};

// K_MONEY (any): i32 my budget (rivals mode). The range check (BUG-1110) is the receiver's: it has a switch
struct Money {
    int v = 0;
    void write(net::Writer& w) const { w.u32((uint32_t)v); }
    bool read(net::Reader& r)
    {
        v = (int)r.u32();
        return r.ok();
    }
};

// K_MERGE_FIX (guest): i32 the difference to add to the merged money; |d| < 1e8.
// C6: -2^31 is refused (before: std::abs of it overflowed, which in practice let it through)
struct MergeFix {
    int d = 0;
    void write(net::Writer& w) const { w.u32((uint32_t)d); }
    bool read(net::Reader& r)
    {
        d = (int)r.u32();
        return r.ok() && d > -100000000 && d < 100000000;
    }
};

// K_ASK (guest): nothing: my budget, if the host knows it
struct Ask {
    void write(net::Writer&) const {}
    bool read(net::Reader& r) { return r.ok(); }
};

// K_SET (host): u8 to, u8 have (0 not known, 1 kept, 2 a newcomer's 0, 3 the host gave it a stored player's budget by
// `coop.store adopt`: taken even when the budget is known, BUG-1482, space 1.3; a 0.7.16.1 guest drops it when its
// budget is known, so the host sends it only to space 1.3 peers), i32 budget. The range check (BUG-1110) is the
// receiver's: it has a switch
struct Set {
    uint8_t to = 0, have = 0;
    int v = 0;
    void write(net::Writer& w) const
    {
        w.u8(to);
        w.u8(have);
        w.u32((uint32_t)v);
    }
    bool read(net::Reader& r)
    {
        to = r.u8();
        have = r.u8();
        v = (int)r.u32();
        return r.ok();
    }
};

} // namespace wire::space_rivalry
