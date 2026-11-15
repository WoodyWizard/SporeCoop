// Payloads of the shared pools (shared.cpp: money in Civ and Space, food in Tribe): MSG_RES and MSG_RES_ADD, the body
// after the message type. Rules: wire/common.h.
#pragma once

#include <utility>

#include "common.h"

namespace wire::shared_res {

constexpr size_t kMaxCauses = 16; // the host's cut

// MSG_RES (host -> guests): u8 resource id, f64 total; then (BUG-0106, older hosts stop before it) u8 n, n x {u8 player,
// u32 the last change of it the host counted}; then (BUG-0696, older hosts stop before it) u8 n, n x {u8 player,
// f64 its part of the change}. Refused as before: a cut head or a non-finite total, a cut ack list (`head_ok` then
// tells the handler the head was fine: it rejects the message at the point it did before, after its ledger steps).
// A cut cause list keeps the causes read before the cut (as before: they were told, the total was taken).
struct Res {
    uint8_t id = 0;
    double v = 0;
    bool has_acks = false;
    std::vector<std::pair<uint8_t, uint32_t>> acks;
    bool has_causes = false; // written only after the acks
    std::vector<std::pair<uint8_t, double>> causes;
    bool head_ok = false; // read: id and a finite total
    void write(net::Writer& w) const
    {
        w.u8(id);
        w.f64(v);
        if (!has_acks)
            return;
        w.u8((uint8_t)acks.size());
        for (auto& [p, seq] : acks) {
            w.u8(p);
            w.u32(seq);
        }
        if (!has_causes)
            return;
        size_t n = causes.size() < kMaxCauses ? causes.size() : kMaxCauses;
        w.u8((uint8_t)n);
        for (size_t i = 0; i < n; i++) {
            w.u8(causes[i].first);
            w.f64(causes[i].second);
        }
    }
    bool read(net::Reader& r)
    {
        id = r.u8();
        v = r.f64();
        acks.clear();
        causes.clear();
        has_acks = has_causes = false;
        head_ok = r.ok() && std::isfinite(v);
        if (!head_ok)
            return false;
        has_acks = r.left() >= 1;
        if (!has_acks)
            return true; // an older host
        uint8_t n = r.u8();
        for (int i = 0; i < n && r.ok(); i++) {
            uint8_t p = r.u8();
            uint32_t q = r.u32();
            if (r.ok())
                acks.push_back({p, q});
        }
        if (!r.ok())
            return false;
        has_causes = r.left() >= 1;
        if (!has_causes)
            return true;
        uint8_t nc = r.u8();
        for (int i = 0; i < nc && r.ok(); i++) {
            uint8_t p = r.u8();
            double c = r.f64();
            if (r.ok())
                causes.push_back({p, c});
        }
        return true; // (a cut cause list: lenient, see above)
    }
};

// MSG_RES_ADD (guest -> host): u8 resource id, f64 the guest's change; then (BUG-0106, older guests stop before it)
// u32 its sequence number. Refused as before: a cut head or a non-finite change; fewer than 4 bytes after the head are
// no sequence number.
struct ResAdd {
    uint8_t id = 0;
    double v = 0;
    bool has_seq = false;
    uint32_t seq = 0;
    void write(net::Writer& w) const
    {
        w.u8(id);
        w.f64(v);
        if (has_seq)
            w.u32(seq);
    }
    bool read(net::Reader& r)
    {
        id = r.u8();
        v = r.f64();
        has_seq = false;
        seq = 0;
        if (!r.ok() || !std::isfinite(v))
            return false;
        if (r.left() >= 4) {
            seq = r.u32();
            has_seq = r.ok();
        }
        return true;
    }
};

} // namespace wire::shared_res
