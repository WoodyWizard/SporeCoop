// MSG_SPACE payloads of space_contracts.cpp (kinds 20..22, co-op contracts). Rules: wire/common.h.
#pragma once

#include "common.h"

namespace wire::space_contracts {

constexpr uint8_t kTypeArmada = 1, kTypeSurvey = 2; // space_contracts.cpp T_ARMADA, T_SURVEY
constexpr uint8_t kMaxTargets = 16;

struct Target {
    uint32_t star = 0;
    uint8_t done = 0;
    uint32_t planet = 0;
};

// K_STATE 20 (host): u32 id, u8 type, u8 state, u8 reason, u8 needed, u32 money, u32 tool, u32 seconds left,
// u8 n + n x u8 crew, u8 n + n x {u32 star, u8 done, u32 planet} targets, u8 wave, u8 waves, u8 left, u8 up.
// Refused (as before): more than 16 targets, a type above survey, an armada without a target.
struct State {
    uint32_t id = 0;
    uint8_t type = 0, state = 0, reason = 0, needed = 0;
    uint32_t money = 0, tool = 0;
    uint32_t secs = 0; // to the deadline, from now
    std::vector<uint8_t> crew;
    std::vector<Target> targets;
    uint8_t wave = 0, waves = 0, left = 0, up = 0;
    void write(net::Writer& w) const
    {
        w.u32(id);
        w.u8(type);
        w.u8(state);
        w.u8(reason);
        w.u8(needed);
        w.u32(money);
        w.u32(tool);
        w.u32(secs);
        w.u8((uint8_t)crew.size());
        for (uint8_t c : crew)
            w.u8(c);
        w.u8((uint8_t)targets.size());
        for (auto& tg : targets) {
            w.u32(tg.star);
            w.u8(tg.done);
            w.u32(tg.planet);
        }
        w.u8(wave);
        w.u8(waves);
        w.u8(left);
        w.u8(up);
    }
    bool read(net::Reader& r)
    {
        *this = State{};
        id = r.u32();
        type = r.u8();
        state = r.u8();
        reason = r.u8();
        needed = r.u8();
        money = r.u32();
        tool = r.u32();
        secs = r.u32();
        uint8_t n = r.u8();
        for (int i = 0; i < n && r.ok(); i++)
            crew.push_back(r.u8());
        n = r.u8();
        if (n > kMaxTargets)
            return false;
        for (int i = 0; i < n && r.ok(); i++) {
            Target tg;
            tg.star = r.u32();
            tg.done = r.u8();
            tg.planet = r.u32();
            targets.push_back(tg);
        }
        wave = r.u8();
        waves = r.u8();
        left = r.u8();
        up = r.u8();
        // an armada needs its target planet (title, rows, the raid all read targets[0])
        if (type > kTypeSurvey || (type == kTypeArmada && targets.empty()))
            return false;
        return r.ok();
    }
};

// K_ACCEPT 21 (guest): u32 contract id
struct Accept {
    uint32_t id = 0;
    void write(net::Writer& w) const { w.u32(id); }
    bool read(net::Reader& r)
    {
        id = r.u32();
        return r.ok();
    }
};

// K_RESULT 22 (host): u32 id, u8 ok, u8 reason, u32 money, u32 tool, u8 type, u8 n + n x u8 crew
struct Result {
    uint32_t id = 0;
    uint8_t ok = 0, reason = 0;
    uint32_t money = 0, tool = 0;
    uint8_t type = 0;
    std::vector<uint8_t> crew;
    void write(net::Writer& w) const
    {
        w.u32(id);
        w.u8(ok);
        w.u8(reason);
        w.u32(money);
        w.u32(tool);
        w.u8(type);
        w.u8((uint8_t)crew.size());
        for (uint8_t c : crew)
            w.u8(c);
    }
    bool read(net::Reader& r)
    {
        id = r.u32();
        ok = r.u8();
        reason = r.u8();
        money = r.u32();
        tool = r.u32();
        type = r.u8();
        uint8_t n = r.u8();
        crew.clear();
        for (int i = 0; i < n && r.ok(); i++)
            crew.push_back(r.u8());
        return r.ok();
    }
};

} // namespace wire::space_contracts
