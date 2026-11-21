// The Tribe members' animations, holds, phases, air height, the theme and party music and the game-over / pause waits
// the host sends its guests (tribe.cpp): `u8 kind` and the kind's payload. C7 of docs/plan-0.7.15.md (audit STG-5,
// BUG-1160): until 0.7.14 they went as sub-kinds of MSG_NPC_ANIM, which npc.cpp passed on to tribe::on_member_anims
// through a whitelist (every new kind needed an edit there, and the world gate of the NPC type decided for them).
// Since 0.7.15 they have their own type, MSG_TRIBE_ANIM (the tribe family, minor kMinor), dispatched by tribe.cpp.
// The bytes after the type are the same in both (golden bytes: tests/wire_tests.cpp).
//
// Compatibility (docs/04-netcode.md#netcode-compat): the host sends MSG_TRIBE_ANIM only to a guest whose HELLO
// listed the tribe family at rev 1 minor >= kMinor; to every older guest (0.7.14 and before: no family list; a 0.7.15
// build before C7: tribe 1.0) it sends MSG_NPC_ANIM as before, which those guests read as they always did. A guest
// takes both types: an older host (0.7.14) sends MSG_NPC_ANIM, whose tribe kinds npc.cpp still passes on.
// `coop.fix tribe.anim_msg off`: MSG_NPC_ANIM to everyone (0.7.14).
// Rules of the wire structs: wire/common.h.
#pragma once

#include <utility>
#include <vector>

#include "common.h"
#include "../net/protocol.h"

namespace wire::tribe_anim {

constexpr uint8_t kFamily = 6;  // the tribe family of net/messages.def
constexpr uint16_t kMinor = 1;  // its minor since MSG_TRIBE_ANIM

enum Kind : uint8_t {
    K_START = 2,       // u8 n, n x {u16 member, u32 anim, u8 how, u8 a, i32 b}: behaviour animations started (BUG-0569)
    K_NOW = 3,         // u8 n, n x {u16 member, u32 anim}: what the members play now
    K_THEME_PLAY = 4,  // the Tribe theme music (BUG-0568): u8 slot, u32 sound, vec3
    K_THEME_MOVE = 5,  // u8 slot, vec3
    K_THEME_STOP = 6,  // u8 slot
    K_PARTY_PLAY = 7,  // the fire pit party music (BUG-0646): u32 track, u32 sound, vec3
    K_PARTY_STOP = 8,  // u32 track
    K_AIR = 9,         // u8 n, n x {u16 member, f32 height over the ground} (BUG-0946)
    K_IDLE = 10,       // u8 n, n x {u16 member, u32 anim, u8 how}: no behaviour animation (BUG-0936)
    K_HOLD = 11,       // u8 n, n x {u16 member, u32 anim}: a hold's start (anim) or end (0) (BUG-0914)
    K_HOLD_ITEM = 12,  // u8 n, n x {u16 member, u32 anim, u8 item}: a hold's start with its item (BUG-0971)
    K_PHASE = 14,      // u8 n, n x {u16 member, u32 anim, f32 t}: where in its animation (BUG-0988)
    K_PHASE_ANY = 15,  // u8 n, n x {u16, u32, f32 t, f32 a0, u16 b0}: the same for other animations (BUG-1069)
    K_TRACKS = 16,     // the format of 15: the other playing tracks and the idle (BUG-1072)
    K_GAMEOVER = 20,   // u8 why (0: down): the host's game-over caption is up (BUG-0960, BUG-1081)
    K_HOST_PAUSE = 21, // u8 up: the host's game is paused (BUG-1086)
    K_RESCUE = 22,     // the downed last villagers (BUG-9278): tribe_rescue_logic.h write_lines (a guest before it drops it)
};

// the kinds of the tribe members (npc.cpp's whitelist of 0.7.14); the NPC's own kinds of MSG_NPC_ANIM are 1 and 13
inline bool is_tribe_kind(uint8_t k)
{
    return (k >= K_START && k <= K_HOLD_ITEM) || (k >= K_PHASE && k <= K_TRACKS) || k == K_GAMEOVER || k == K_HOST_PAUSE ||
           k == K_RESCUE;
}
// the kinds a guest takes while its game is not in Tribe (an editor, a load): the party music's stop (BUG-0890) and the
// waits (their end must come; their start is applied only in Tribe, tribe.cpp)
inline bool any_mode(uint8_t k) { return k == K_PARTY_STOP || k == K_GAMEOVER || k == K_HOST_PAUSE; }

// the type the host sends these kinds to a peer as: its family list (has: it sent one) and the switch tribe.anim_msg
inline uint8_t type_for(bool has_families, const std::vector<net::FamilyRev>& families, bool fix)
{
    if (!fix || !has_families)
        return net::MSG_NPC_ANIM;
    for (auto& f : families)
        if (f.family == kFamily)
            return f.rev == 1 && f.minor >= kMinor ? net::MSG_TRIBE_ANIM : net::MSG_NPC_ANIM;
    return net::MSG_NPC_ANIM;
}

// K_GAMEOVER / K_HOST_PAUSE: u8 kind, u8 why (a reason, 0 down; the pause: 1 up)
struct Wait {
    uint8_t kind = K_GAMEOVER;
    uint8_t why = 0;
    void write(net::Writer& w) const
    {
        w.u8(kind);
        w.u8(why);
    }
    bool read(net::Reader& r)
    {
        kind = r.u8();
        why = r.u8();
        return r.ok() && (kind == K_GAMEOVER || kind == K_HOST_PAUSE);
    }
};

constexpr size_t kMaxList = 255; // a list's u8 count

// K_AIR: u8 kind, u8 n, n x {u16 member, f32 height over the ground}
struct Air {
    std::vector<std::pair<uint16_t, float>> v;
    void write(net::Writer& w) const
    {
        size_t n = v.size() < kMaxList ? v.size() : kMaxList;
        w.u8(K_AIR);
        w.u8((uint8_t)n);
        for (size_t i = 0; i < n; i++) {
            w.u16(v[i].first);
            w.f32(v[i].second);
        }
    }
    bool read(net::Reader& r)
    {
        v.clear();
        bool kind_ok = r.u8() == K_AIR;
        uint8_t n = r.u8();
        for (int i = 0; i < n && r.ok(); i++) {
            uint16_t id = r.u16();
            float agl = r.f32();
            if (r.ok())
                v.push_back({id, agl});
        }
        return r.ok() && kind_ok;
    }
};

// K_HOLD / K_HOLD_ITEM: u8 kind, u8 n, n x {u16 member, u32 anim (0: the hold ended)[, u8 item]}
struct Hold {
    bool with_item = false; // K_HOLD_ITEM
    struct One {
        uint16_t id = 0;
        uint32_t anim = 0;
        uint8_t item = 0;
    };
    std::vector<One> v;
    void write(net::Writer& w) const
    {
        size_t n = v.size() < kMaxList ? v.size() : kMaxList;
        w.u8(with_item ? K_HOLD_ITEM : K_HOLD);
        w.u8((uint8_t)n);
        for (size_t i = 0; i < n; i++) {
            w.u16(v[i].id);
            w.u32(v[i].anim);
            if (with_item)
                w.u8(v[i].item);
        }
    }
    bool read(net::Reader& r)
    {
        v.clear();
        uint8_t kind = r.u8();
        with_item = kind == K_HOLD_ITEM;
        if (kind != K_HOLD && kind != K_HOLD_ITEM)
            return false;
        uint8_t n = r.u8();
        for (int i = 0; i < n && r.ok(); i++) {
            One o;
            o.id = r.u16();
            o.anim = r.u32();
            o.item = with_item ? r.u8() : 0;
            if (r.ok())
                v.push_back(o);
        }
        return r.ok();
    }
};

} // namespace wire::tribe_anim
