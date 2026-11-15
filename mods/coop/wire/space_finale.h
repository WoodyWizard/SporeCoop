// MSG_SPACE payloads of space_finale.cpp (kinds 40..42, the galactic core together). Rules: wire/common.h.
#pragma once

#include "common.h"

namespace wire::space_finale {

// K_STATE 40 (any): u8 zone, u8 flags, u32 star, u32 previous star, f32 distance to the core
struct State {
    uint8_t zone = 0, flags = 0;
    uint32_t star = 0, prev = 0;
    float dist = 0;
    void write(net::Writer& w) const
    {
        w.u8(zone);
        w.u8(flags);
        w.u32(star);
        w.u32(prev);
        w.f32(dist);
    }
    bool read(net::Reader& r)
    {
        zone = r.u8();
        flags = r.u8();
        star = r.u32();
        prev = r.u32();
        dist = r.f32();
        return r.ok();
    }
};

// K_WAIT 41 and K_REACHED 42 (any): no payload (trailing bytes of a newer peer are left alone)
struct Wait {
    void write(net::Writer&) const {}
    bool read(net::Reader& r) { return r.ok(); }
};
using Reached = Wait;

} // namespace wire::space_finale
