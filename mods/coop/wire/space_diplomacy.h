// MSG_SPACE payloads of space_diplomacy.cpp (kinds 30..34, shared relations with the NPC empires). Rules: wire/common.h.
#pragma once

#include <utility>

#include "common.h"

namespace wire::space_diplomacy {

constexpr size_t kMaxEvents = 100; // per empire in a K_SNAP (the sender's cut)

// K_EVENT 30 (any): u32 npc political id, u8 first (the sender's empire is the event's first one), u32 event id,
// f32 scale, str8 the empire's name in the sender's copy
struct Event {
    uint32_t npc = 0;
    uint8_t first = 0;
    uint32_t ev = 0;
    float scale = 0;
    std::string name;
    void write(net::Writer& w) const
    {
        w.u32(npc);
        w.u8(first);
        w.u32(ev);
        w.f32(scale);
        w.str8(name);
    }
    bool read(net::Reader& r)
    {
        npc = r.u32();
        first = r.u8();
        ev = r.u32();
        scale = r.f32();
        name = r.str8();
        return r.ok();
    }
};

// K_STATE 31 (any): u32 npc, u8 first, u8 what (war, alliance, peace, break), str8 name
struct State {
    uint32_t npc = 0;
    uint8_t first = 0, what = 0;
    std::string name;
    void write(net::Writer& w) const
    {
        w.u32(npc);
        w.u8(first);
        w.u8(what);
        w.str8(name);
    }
    bool read(net::Reader& r)
    {
        npc = r.u32();
        first = r.u8();
        what = r.u8();
        name = r.str8();
        return r.ok();
    }
};

// K_SNAP 32 (host): one chunk of the host's table: u8 last, u8 reply_to (0xFF: the periodic table), u16 n,
// n x {u32 npc, u32 flags, u8 k, k x {u32 event id, f32 value}}. The flags as sent (the receiver masks them).
struct SnapRec {
    uint32_t npc = 0, flags = 0;
    std::vector<std::pair<uint32_t, float>> ev;
};
struct Snap {
    uint8_t last = 0, reply_to = 0xFF;
    std::vector<SnapRec> recs;
    void write(net::Writer& w) const
    {
        w.u8(last);
        w.u8(reply_to);
        w.u16((uint16_t)recs.size());
        for (auto& h : recs) {
            w.u32(h.npc);
            w.u32(h.flags);
            size_t k = h.ev.size() < kMaxEvents ? h.ev.size() : kMaxEvents;
            w.u8((uint8_t)k);
            for (size_t i = 0; i < k; i++) {
                w.u32(h.ev[i].first);
                w.f32(h.ev[i].second);
            }
        }
    }
    bool read(net::Reader& r)
    {
        last = r.u8();
        reply_to = r.u8();
        uint16_t n = r.u16();
        recs.clear();
        for (uint16_t i = 0; i < n && r.ok(); i++) {
            SnapRec h;
            h.npc = r.u32();
            h.flags = r.u32();
            uint8_t k = r.u8();
            for (uint8_t j = 0; j < k && r.ok(); j++) {
                uint32_t id = r.u32();
                float v = r.f32();
                h.ev.push_back({id, v});
            }
            recs.push_back(std::move(h));
        }
        if (!r.ok())
            recs.clear();
        return r.ok();
    }
};

// K_ASK 33 (guest): no payload: the host's table, please
struct Ask {
    void write(net::Writer&) const {}
    bool read(net::Reader& r) { return r.ok(); }
};

// K_CONTACT 34 (any): u32 npc, u8 level (2 contacted, anything else met), str8 name
struct Contact {
    uint32_t npc = 0;
    uint8_t level = 1;
    std::string name;
    void write(net::Writer& w) const
    {
        w.u32(npc);
        w.u8(level);
        w.str8(name);
    }
    bool read(net::Reader& r)
    {
        npc = r.u32();
        level = r.u8();
        name = r.str8();
        return r.ok();
    }
};

} // namespace wire::space_diplomacy
