// MSG_SPACE kinds 70..79 of space_trade.cpp (tool gifts, spice sales and offers between players; a guest's own cargo
// hold across a rejoin, BUG-0678): payload structs, the rules in wire/common.h. Every payload starts with
// {u8 to, u32 id}: the player it is for (0 = the host) and the deal id (the hold's serial for K_INV*).
#pragma once

#include <algorithm>

#include "net/validate.h"
#include "wire/common.h"

namespace wire::space_trade {

// the header every trade message starts with (the receiver checks `to` before it reads the rest)
struct Head {
    uint8_t to = 0;
    uint32_t id = 0;
    void write(net::Writer& w) const
    {
        w.u8(to);
        w.u32(id);
    }
    bool read(net::Reader& r)
    {
        to = r.u8();
        id = r.u32();
        return r.ok();
    }
};

// K_TOOL (any): {to, id, key[3], u32 n, str8 name}: n tool charges for `to`; n 1..999
struct Tool : Head {
    Key3 key;
    int n = 0;
    std::string name;
    void write(net::Writer& w) const
    {
        Head::write(w);
        put_key(w, key);
        w.u32((uint32_t)n);
        w.str8(name);
    }
    bool read(net::Reader& r)
    {
        if (!Head::read(r))
            return false;
        key = get_key(r);
        n = (int)r.u32();
        name = r.str8();
        return r.ok() && n > 0 && n <= 999;
    }
};

// K_TOOL_ACK (any): {to, id, u32 accepted, u32 back}: the receiver kept `got`, `back` returns
struct ToolAck : Head {
    int got = 0, back = 0;
    void write(net::Writer& w) const
    {
        Head::write(w);
        w.u32((uint32_t)got);
        w.u32((uint32_t)back);
    }
    bool read(net::Reader& r)
    {
        if (!Head::read(r))
            return false;
        got = (int)r.u32();
        back = (int)r.u32();
        return r.ok();
    }
};

// K_SELL (any): {to, id, u32 planet, key[3], u32 n, u32 price, str8 name}: spice sold to `to`'s colony; n 1..999.
// K_OFFER (any, rivals) has the same layout: an offer `to` may accept; n 1..999 and n x price an int
struct Sell : Head {
    uint32_t planet = 0;
    Key3 key;
    int n = 0, price = 0;
    std::string name;
    void write(net::Writer& w) const
    {
        Head::write(w);
        w.u32(planet);
        put_key(w, key);
        w.u32((uint32_t)n);
        w.u32((uint32_t)price);
        w.str8(name);
    }
    bool read(net::Reader& r)
    {
        if (!Head::read(r))
            return false;
        planet = r.u32();
        key = get_key(r);
        n = (int)r.u32();
        price = (int)r.u32();
        name = r.str8();
        return r.ok() && n > 0 && n <= 999;
    }
};
struct Offer : Sell {
    bool read(net::Reader& r)
    {
        int sum = 0;
        return Sell::read(r) && net::money_total(n, price, sum);
    }
};

// K_SELL_ACK (any): {to, id, u32 accepted}: spice taken (the seller is paid for these), the rest returns
struct SellAck : Head {
    int got = 0;
    void write(net::Writer& w) const
    {
        Head::write(w);
        w.u32((uint32_t)got);
    }
    bool read(net::Reader& r)
    {
        if (!Head::read(r))
            return false;
        got = (int)r.u32();
        return r.ok();
    }
};

// K_OFFER_ACK (any, rivals): {to, id, u32 accepted, u8 reason}: accepted = paid and taken, 0 = declined
struct OfferAck : Head {
    int got = 0;
    uint8_t reason = 0;
    void write(net::Writer& w) const
    {
        Head::write(w);
        w.u32((uint32_t)got);
        w.u8(reason);
    }
    bool read(net::Reader& r)
    {
        if (!Head::read(r))
            return false;
        got = (int)r.u32();
        reason = r.u8();
        return r.ok();
    }
};

// K_OFFER_CANCEL (any, rivals): {to, id}: the seller withdraws
using OfferCancel = Head;

// ---- a cargo hold (BUG-0678): u16 cargo slots, u16 n (at most 250), n x {u8 type, key[3], u32 count, u32 ammo}
// type: cSpaceInventoryItem +0x14 (2 tool, 4 trading object / spice, 0/1/5/6 other cargo); count 0..100000; ammo the
// tool's charges or -1 (-1..100000)
struct InvItem {
    uint8_t type = 0;
    Key3 key;
    int count = 0;
    int ammo = -1;
};
struct Inv {
    int slots = 0;
    std::vector<InvItem> items;
};
inline void put_inv(net::Writer& w, const Inv& v)
{
    w.u16((uint16_t)std::clamp(v.slots, 0, 0xFFFF));
    uint16_t n = (uint16_t)std::min<size_t>(v.items.size(), 250);
    w.u16(n);
    for (uint16_t i = 0; i < n; i++) {
        const InvItem& x = v.items[i];
        w.u8(x.type);
        put_key(w, x.key);
        w.u32((uint32_t)x.count);
        w.u32((uint32_t)x.ammo);
    }
}
inline bool get_inv(net::Reader& r, Inv& v)
{
    v = Inv{};
    v.slots = r.u16();
    uint16_t n = r.u16();
    if (!r.ok() || n > 250)
        return false;
    for (uint16_t i = 0; i < n && r.ok(); i++) {
        InvItem x;
        x.type = r.u8();
        x.key = get_key(r);
        x.count = (int)r.u32();
        x.ammo = (int)r.u32();
        if (x.count < 0 || x.count > 100000 || x.ammo < -1 || x.ammo > 100000)
            return false;
        v.items.push_back(x);
    }
    return r.ok();
}

// K_INV (guest -> host): {to 0, serial, u8 fresh, hold}: what my cargo hold has now (fresh: the first one of a
// Space world loaded from the host)
struct InvMsg : Head {
    bool fresh = false;
    Inv inv;
    void write(net::Writer& w) const
    {
        Head::write(w);
        w.u8(fresh ? 1 : 0);
        put_inv(w, inv);
    }
    bool read(net::Reader& r)
    {
        if (!Head::read(r))
            return false;
        fresh = r.u8() != 0;
        return get_inv(r, inv);
    }
};

// K_INV_KEPT (host -> guest): {to, serial, u8 have[, hold]}: your own hold, kept since you were here (have 0: nothing
// kept, serial 0: the hold of this world is yours)
struct InvKept : Head {
    bool have = false;
    Inv inv;
    void write(net::Writer& w) const
    {
        Head::write(w);
        w.u8(have ? 1 : 0);
        if (have)
            put_inv(w, inv);
    }
    bool read(net::Reader& r)
    {
        if (!Head::read(r))
            return false;
        have = r.u8() != 0;
        if (!r.ok())
            return false;
        inv = Inv{};
        return !have || get_inv(r, inv);
    }
};

// K_INV_DONE (guest -> host): {to 0, serial, u16 restored, u16 failed}: applied (or not needed). The host never read
// the two counts: a message without them is used as before (has_counts false)
struct InvDone : Head {
    bool has_counts = true;
    uint16_t restored = 0, failed = 0;
    void write(net::Writer& w) const
    {
        Head::write(w);
        if (!has_counts)
            return;
        w.u16(restored);
        w.u16(failed);
    }
    bool read(net::Reader& r)
    {
        if (!Head::read(r))
            return false;
        has_counts = r.left() >= 4;
        restored = failed = 0;
        if (has_counts) {
            restored = r.u16();
            failed = r.u16();
        }
        return r.ok();
    }
};

} // namespace wire::space_trade
