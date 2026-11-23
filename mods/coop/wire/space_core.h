// MSG_SPACE payloads of spacecoop.cpp (kinds 1..9: rescue, raids, missions, terraforming, spice gifts, sculpting and
// colours). Rules: wire/common.h.
#pragma once

#include "common.h"

namespace wire::space_core {

constexpr size_t kSculptBytes = 0x38; // the sculpt message's transform block, as the game passes it

// K_DOWN 1 (any): u8 state (1 disabled, 2 repaired, 3 destroyed), u8 seconds until it is lost
struct Down {
    uint8_t state = 0, secs = 0;
    void write(net::Writer& w) const
    {
        w.u8(state);
        w.u8(secs);
    }
    bool read(net::Reader& r)
    {
        state = r.u8();
        secs = r.u8();
        return r.ok();
    }
};

// K_RAID 2 (any): u32 place (the colony's PlanetID), u8 the raid mission's state (3 started, 5 defended, 6 lost)
struct Raid {
    uint32_t place = 0;
    uint8_t state = 0;
    void write(net::Writer& w) const
    {
        w.u32(place);
        w.u8(state);
    }
    bool read(net::Reader& r)
    {
        place = r.u32();
        state = r.u8();
        return r.ok();
    }
};

// K_MISSION 3 (any): f32 the mission's money reward, u8 n, n x key: its reward tools. Refused (as before): fewer bytes
// than the n keys need.
struct Mission {
    float reward = 0;
    std::vector<Key3> tools;
    void write(net::Writer& w) const
    {
        w.f32(reward);
        w.u8((uint8_t)tools.size());
        for (auto& k : tools)
            put_key(w, k);
    }
    bool read(net::Reader& r)
    {
        reward = r.f32();
        uint8_t n = r.u8();
        tools.clear();
        if (!r.ok() || r.left() < (size_t)n * 12)
            return false; // cut: no notice, no tools
        for (int i = 0; i < n; i++)
            tools.push_back(get_key(r));
        return r.ok();
    }
};

// K_TERRA 4 (any): u32 place, 3 x f32 atmosphere / temperature / water deltas since the last one;
// K_TERRA_REC 9 (any, BUG-0883): u32 place, 3 x f32 the planet record's values now. Non-finite values are the
// handler's to refuse (apply_terra since BUG-1489, which also clamps a delta to +-1; apply_terra_rec).
struct Terra {
    uint32_t place = 0;
    float v[3] = {};
    void write(net::Writer& w) const
    {
        w.u32(place);
        for (float f : v)
            w.f32(f);
    }
    bool read(net::Reader& r)
    {
        place = r.u32();
        for (float& f : v)
            f = r.f32();
        return r.ok();
    }
};
using TerraRec = Terra;

// K_GIFT 5 and K_GIFT_BACK 6 (any, addressed): u8 to, key (the spice), u32 n. The count's range check is the
// handler's (it depends on space.gift_refund).
struct Gift {
    uint8_t to = 0;
    Key3 key;
    int32_t n = 0;
    void write(net::Writer& w) const
    {
        w.u8(to);
        put_key(w, key);
        w.u32((uint32_t)n);
    }
    bool read(net::Reader& r)
    {
        to = r.u8();
        key = get_key(r);
        n = (int32_t)r.u32();
        return r.ok();
    }
};

// K_SCULPT 7 (any, BUG-0763): u32 place, u32 the sculpt effect id, 0x38 bytes of the game's transform block
struct Sculpt {
    uint32_t place = 0, effect = 0;
    uint8_t xf[kSculptBytes] = {};
    void write(net::Writer& w) const
    {
        w.u32(place);
        w.u32(effect);
        w.raw(xf, kSculptBytes);
    }
    bool read(net::Reader& r)
    {
        place = r.u32();
        effect = r.u32();
        r.raw(xf, kSculptBytes);
        return r.ok();
    }
};

// K_COLOUR 8 (any, BUG-0763): u32 place, u32 ramp, u32 colour a, u32 colour b (the SetColors arguments)
struct Colour {
    uint32_t place = 0, ramp = 0, a = 0, b = 0;
    void write(net::Writer& w) const
    {
        w.u32(place);
        w.u32(ramp);
        w.u32(a);
        w.u32(b);
    }
    bool read(net::Reader& r)
    {
        place = r.u32();
        ramp = r.u32();
        a = r.u32();
        b = r.u32();
        return r.ok();
    }
};

} // namespace wire::space_core
