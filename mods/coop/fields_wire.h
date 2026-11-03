// MSG_FIELDS codec and the compare rule of the field replication (fields.h). Portable: unit tests
// (tests/npc_tests.cpp) and the decoder fuzzer link it without the game.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "net/buffer.h"
#include "net/protocol.h"

namespace fields_wire {

constexpr int kMaxFields = 8;

struct Batch {
    uint8_t kind = 0;
    std::vector<uint8_t> fids;  // the fields of every entry, in this order
    std::vector<uint16_t> ids;  // the objects' net ids
    std::vector<float> vals;    // ids.size() x fids.size(), row by row
};

// a mirror's value and the owner's: different beyond the relative tolerance (a NaN is always different)
inline bool differs(float mirror, float wanted, float tol)
{
    if (!std::isfinite(mirror) || !std::isfinite(wanted))
        return true;
    return std::fabs(mirror - wanted) > tol * std::fmax(std::fabs(wanted), 1e-3f);
}

// a value worth applying: finite and inside the field's sane range (a hostile or broken packet must not
// make a creature 1e30 m tall)
inline bool sane(float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; }

// one batch in packets of at most net::kMaxPacket - 64 bytes
inline std::vector<net::Writer> encode(const Batch& b)
{
    std::vector<net::Writer> out;
    size_t nf = b.fids.size();
    if (!nf || nf > (size_t)kMaxFields || b.vals.size() != b.ids.size() * nf)
        return out;
    size_t head = 1 + 1 + nf + 2, per = (net::kMaxPacket - 64 - head) / (2 + 4 * nf);
    for (size_t i = 0; i < b.ids.size(); i += per) {
        size_t n = std::min(per, b.ids.size() - i);
        net::Writer w;
        w.u8(b.kind);
        w.u8((uint8_t)nf);
        for (uint8_t f : b.fids)
            w.u8(f);
        w.u16((uint16_t)n);
        for (size_t j = i; j < i + n; j++) {
            w.u16(b.ids[j]);
            for (size_t k = 0; k < nf; k++)
                w.f32(b.vals[j * nf + k]);
        }
        out.push_back(std::move(w));
    }
    return out;
}

// false: a malformed packet (nothing of it is used)
inline bool decode(net::Reader& r, Batch& b)
{
    b = Batch{};
    b.kind = r.u8();
    uint8_t nf = r.u8();
    if (!r.ok() || nf == 0 || nf > kMaxFields)
        return false;
    for (int i = 0; i < nf; i++)
        b.fids.push_back(r.u8());
    uint16_t n = r.u16();
    if (!r.ok() || r.left() != (size_t)n * (2 + 4 * nf))
        return false;
    for (int i = 0; i < n; i++) {
        b.ids.push_back(r.u16());
        for (int k = 0; k < nf; k++)
            b.vals.push_back(r.f32());
    }
    return r.ok();
}

} // namespace fields_wire
