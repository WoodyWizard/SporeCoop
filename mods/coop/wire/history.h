// Payloads of MSG_PLAYER_HISTORY 75 (session 1.3, WP-H1 of docs/plan-0.7.17.md, BUG-1470): every player's own stage
// history (history_logic.h). The body after the message type is u8 kind, then the kind's struct. Rules: wire/common.h.
#pragma once

#include "common.h"

namespace wire::history {

enum Kind : uint8_t {
    K_RECORD = 1, // host -> one guest: its own history (Record)
    K_CELL = 2,   // guest -> host: the Cell card its own game decides from its own diet now (Cell)
    K_PICK = 3,   // guest -> host: prior-stage starting path pick for worlds started past Cell (Pick, BUG-1565)
};

constexpr uint8_t kFlagOwn = 1; // the record is the guest's own; without it the guest plays the shared history again

// K_RECORD: u8 flags, 5 x u32 the cards (cPlayer +0x10FC.. order, 0 = stage not done), f32 social, f32 combat.
// read(): false for a cut message or a progress that is not finite (the handler checks the card ids)
struct Record {
    uint8_t flags = 0;
    uint32_t cards[5] = {};
    float social = 0, combat = 0;
    void write(net::Writer& w) const
    {
        w.u8(flags);
        for (uint32_t c : cards)
            w.u32(c);
        w.f32(social);
        w.f32(combat);
    }
    bool read(net::Reader& r)
    {
        flags = r.u8();
        for (auto& c : cards)
            c = r.u32();
        social = r.f32();
        combat = r.f32();
        return r.ok() && std::isfinite(social) && std::isfinite(combat);
    }
};

// K_CELL: u32 the card (the handler checks it is a Cell card)
struct Cell {
    uint32_t card = 0;
    void write(net::Writer& w) const { w.u32(card); }
    bool read(net::Reader& r)
    {
        card = r.u32();
        return r.ok();
    }
};

// K_PICK (BUG-1565): 4 x u32 prior-stage cards (Cell, Creature, Tribe, Civ; 0 = keep current, or all four 0 =
// Wanderer blank prior path when in Space). Older hosts ignore unknown kind 3.
struct Pick {
    uint32_t cards[4] = {};
    void write(net::Writer& w) const
    {
        for (uint32_t c : cards)
            w.u32(c);
    }
    bool read(net::Reader& r)
    {
        for (auto& c : cards)
            c = r.u32();
        return r.ok();
    }
};

} // namespace wire::history
