// Portable payload structs of the game modules' messages (C6 of docs/plan-0.7.15.md, audit NET-4): what a handler
// reads from another player is decoded here, without the game, so the decoder fuzzer (tests/fuzz_decoders.cpp) and the
// golden-byte tests (tests/wire_tests.cpp) reach it. docs/04-netcode.md#netcode-wire-structs has the rules.
//
// One header per feature (wire/space_map.h, wire/space_trade.h, ...), namespace wire::<feature>, one struct per
// message kind (the kind numbers stay in the feature's `enum Kind`, tests/validate_tests.cpp reads them there). A struct
// is the payload after the MSG_SPACE kind and origin bytes (or after the message type for a top-level message):
//   void write(net::Writer&) const   the bytes the feature sent before the struct existed, byte for byte
//   bool read(net::Reader&)          false: the message is not used (cut, a count over its limit, a value no peer
//                                    sends); trailing bytes are left alone (a newer peer's appended field)
// The handler is then decode -> check against local state -> apply: read() holds only the checks that rejected the
// whole message before (or that it now rejects: listed as a behaviour change in the struct's comment, "C6:"); checks
// that gate one part of the action stay in the handler. An optional trailing field an older peer does not send is a
// `has_x` flag that read() sets from r.left() and write() follows.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../net/buffer.h"

namespace wire {

// a resource key (instance, type, group) as the game's ResourceKey: three u32 on the wire
struct Key3 {
    uint32_t v[3] = {};
    bool operator==(const Key3& o) const { return v[0] == o.v[0] && v[1] == o.v[1] && v[2] == o.v[2]; }
    bool operator!=(const Key3& o) const { return !(*this == o); }
};
inline void put_key(net::Writer& w, const Key3& k)
{
    for (uint32_t x : k.v)
        w.u32(x);
}
inline Key3 get_key(net::Reader& r)
{
    Key3 k;
    for (auto& x : k.v)
        x = r.u32();
    return k;
}

inline bool finite3(const float* v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

// a count-prefixed list of u32 (planet ids and the like): u8 n, n x u32; write cuts at `max`
inline void put_u32s8(net::Writer& w, const std::vector<uint32_t>& v, size_t max = 255)
{
    uint8_t n = (uint8_t)(v.size() < max ? v.size() : max);
    w.u8(n);
    for (uint8_t i = 0; i < n; i++)
        w.u32(v[i]);
}
inline bool get_u32s8(net::Reader& r, std::vector<uint32_t>& v)
{
    v.clear();
    uint8_t n = r.u8();
    for (uint8_t i = 0; i < n && r.ok(); i++)
        v.push_back(r.u32());
    if (!r.ok())
        v.clear();
    return r.ok();
}

} // namespace wire
