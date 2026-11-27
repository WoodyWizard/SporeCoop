// Payloads of MSG_ADVENTURE 79 (creature 1.4, WP-A of 0.7.99, BUG-1474 / BUG-0793): shared Galactic Adventures run
// journal (goals, act changes, host-decided outcome, object deaths) and host-authoritative adventure creatures (actor
// batches, guest hit forwarding). The body after MSG_ADVENTURE is u8 kind, then the kind's struct. Rules: wire/common.h.
#pragma once

#include "common.h"

namespace wire::adventure {

// creature family minor that introduces MSG_ADVENTURE (79)
constexpr uint16_t kCreatureMinor = 4;

constexpr uint8_t kMaxActorsPerBatch = 20;
constexpr uint8_t kMaxAct = 63;
constexpr uint8_t kMaxGoalSlot = 15;
constexpr uint32_t kMaxGoalCount = 100000;
constexpr uint8_t kMaxDeaths = 64;
constexpr float kMaxHitDamage = 1000000.f;
constexpr float kMaxHealth = 10000000.f;
constexpr uint8_t kActorFlagDead = 1;

enum Kind : uint8_t {
    K_HELLO = 0,   // guest -> host (R): announce presence in adventure + last applied journal seq
    K_WELCOME = 1, // host -> guest (R): current run id, act, play state, latest seq (journal entries follow)
    K_PROPOSE = 2, // guest -> host (R): forward a local non-defeat goal credit to host
    K_COMMIT = 3,  // host -> guests (R): authoritative journaled goal credit (seq)
    K_ACT = 4,     // host -> guests (R): authoritative act change (seq, act)
    K_OUTCOME = 5, // host -> guests (R): authoritative win (5) or loss (6) outcome (seq, state)
    K_DEATH = 6,   // any -> host / host -> guests (R): mirrored non-creature scenario object death count
    K_HIT = 7,      // guest -> host (R): guest captain hit on a paired host adventure creature
    K_ACTORS = 8,   // host -> guests (U): batch of host adventure creature states (<= 20)
    K_HOST_ADV = 9, // host -> guests (R): host adventure invite/title for Quick Play join (BUG-1607)
};

constexpr size_t kMaxTitleLen = 96;

inline bool finite4(const float* v)
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]) && std::isfinite(v[3]);
}

// Common run header carried by every MSG_ADVENTURE payload
struct RunHeader {
    Key3 mission;
    uint32_t run_id = 0;

    void write(net::Writer& w) const
    {
        put_key(w, mission);
        w.u32(run_id);
    }
    bool read(net::Reader& r)
    {
        mission = get_key(r);
        run_id = r.u32();
        return r.ok();
    }
};

struct GoalData {
    uint32_t act = 0;
    uint32_t slot = 0;
    uint32_t kind = 0;
    uint32_t target = 0;
    uint32_t secondary = 0;
    uint32_t required = 0;
    uint32_t object_id = 0;
    uint32_t count = 0;

    void write(net::Writer& w) const
    {
        w.u32(act);
        w.u32(slot);
        w.u32(kind);
        w.u32(target);
        w.u32(secondary);
        w.u32(required);
        w.u32(object_id);
        w.u32(count);
    }
    bool read(net::Reader& r)
    {
        act = r.u32();
        slot = r.u32();
        kind = r.u32();
        target = r.u32();
        secondary = r.u32();
        required = r.u32();
        object_id = r.u32();
        count = r.u32();
        return r.ok() && act <= kMaxAct && slot <= kMaxGoalSlot && required <= kMaxGoalCount && count <= kMaxGoalCount;
    }
};

// K_HELLO (guest -> host)
struct Hello {
    RunHeader hdr;
    uint32_t visitor_id = 0;
    uint32_t last_seq = 0;

    void write(net::Writer& w) const
    {
        hdr.write(w);
        w.u32(visitor_id);
        w.u32(last_seq);
    }
    bool read(net::Reader& r)
    {
        if (!hdr.read(r))
            return false;
        visitor_id = r.u32();
        last_seq = r.u32();
        return r.ok();
    }
};

// K_WELCOME (host -> guest)
struct Welcome {
    RunHeader hdr;
    uint32_t visitor_id = 0;
    uint32_t act = 0;
    uint32_t state = 0;
    uint32_t seq = 0;

    void write(net::Writer& w) const
    {
        hdr.write(w);
        w.u32(visitor_id);
        w.u32(act);
        w.u32(state);
        w.u32(seq);
    }
    bool read(net::Reader& r)
    {
        if (!hdr.read(r))
            return false;
        visitor_id = r.u32();
        act = r.u32();
        state = r.u32();
        seq = r.u32();
        return r.ok() && hdr.run_id != 0 && act <= kMaxAct && state <= 10;
    }
};

// K_PROPOSE (guest -> host)
struct Propose {
    RunHeader hdr;
    GoalData goal;

    void write(net::Writer& w) const
    {
        hdr.write(w);
        goal.write(w);
    }
    bool read(net::Reader& r) { return hdr.read(r) && goal.read(r) && hdr.run_id != 0; }
};

// K_COMMIT (host -> guests)
struct Commit {
    RunHeader hdr;
    uint32_t seq = 0;
    GoalData goal;

    void write(net::Writer& w) const
    {
        hdr.write(w);
        w.u32(seq);
        goal.write(w);
    }
    bool read(net::Reader& r)
    {
        if (!hdr.read(r))
            return false;
        seq = r.u32();
        return goal.read(r) && hdr.run_id != 0 && seq != 0;
    }
};

// K_ACT (host -> guests)
struct ActChange {
    RunHeader hdr;
    uint32_t seq = 0;
    uint32_t act = 0;

    void write(net::Writer& w) const
    {
        hdr.write(w);
        w.u32(seq);
        w.u32(act);
    }
    bool read(net::Reader& r)
    {
        if (!hdr.read(r))
            return false;
        seq = r.u32();
        act = r.u32();
        return r.ok() && hdr.run_id != 0 && seq != 0 && act <= kMaxAct;
    }
};

// K_OUTCOME (host -> guests): state is 5 (success) or 6 (failure)
struct Outcome {
    RunHeader hdr;
    uint32_t seq = 0;
    uint8_t state = 0;

    void write(net::Writer& w) const
    {
        hdr.write(w);
        w.u32(seq);
        w.u8(state);
    }
    bool read(net::Reader& r)
    {
        if (!hdr.read(r))
            return false;
        seq = r.u32();
        state = r.u8();
        return r.ok() && hdr.run_id != 0 && seq != 0 && (state == 5 || state == 6);
    }
};

// K_DEATH (any -> host / host -> guests): non-creature scenario object deaths (e.g. buildings)
struct Death {
    RunHeader hdr;
    uint32_t seq = 0;
    uint32_t object_id = 0;
    uint8_t deaths = 0;

    void write(net::Writer& w) const
    {
        hdr.write(w);
        w.u32(seq);
        w.u32(object_id);
        w.u8(deaths);
    }
    bool read(net::Reader& r)
    {
        if (!hdr.read(r))
            return false;
        seq = r.u32();
        object_id = r.u32();
        deaths = r.u8();
        return r.ok() && hdr.run_id != 0 && object_id != 0 && deaths >= 1 && deaths <= kMaxDeaths;
    }
};

// K_HIT (guest -> host): forward a guest captain's hit on a paired host adventure creature
struct Hit {
    RunHeader hdr;
    uint32_t marker_hash = 0;
    int32_t class_id = -1;
    uint32_t host_key = 0;
    float damage = 0.f;
    int32_t dmg_type = 0;

    void write(net::Writer& w) const
    {
        hdr.write(w);
        w.u32(marker_hash);
        w.u32((uint32_t)class_id);
        w.u32(host_key);
        w.f32(damage);
        w.u32((uint32_t)dmg_type);
    }
    bool read(net::Reader& r)
    {
        if (!hdr.read(r))
            return false;
        marker_hash = r.u32();
        class_id = (int32_t)r.u32();
        host_key = r.u32();
        damage = r.f32();
        dmg_type = (int32_t)r.u32();
        return r.ok() && hdr.run_id != 0 && host_key != 0 && class_id >= 0 && std::isfinite(damage) && damage > 0.f &&
               damage <= kMaxHitDamage;
    }
};

// One host-simulated adventure creature sample in K_ACTORS
struct ActorSample {
    uint32_t host_key = 0;
    uint32_t marker_hash = 0;
    int32_t class_id = -1;
    float pos[3] = {};
    float rot[4] = {0.f, 0.f, 0.f, 1.f};
    float vel[3] = {};
    float hp = 0.f;
    float max_hp = 0.f;
    uint8_t flags = 0;

    void write(net::Writer& w) const
    {
        w.u32(host_key);
        w.u32(marker_hash);
        w.u32((uint32_t)class_id);
        for (float f : pos)
            w.f32(f);
        for (float f : rot)
            w.f32(f);
        for (float f : vel)
            w.f32(f);
        w.f32(hp);
        w.f32(max_hp);
        w.u8(flags);
    }
    bool read(net::Reader& r)
    {
        host_key = r.u32();
        marker_hash = r.u32();
        class_id = (int32_t)r.u32();
        for (float& f : pos)
            f = r.f32();
        for (float& f : rot)
            f = r.f32();
        for (float& f : vel)
            f = r.f32();
        hp = r.f32();
        max_hp = r.f32();
        flags = r.u8();
        return r.ok() && host_key != 0 && class_id >= 0 && finite3(pos) && finite4(rot) && finite3(vel) &&
               std::isfinite(hp) && std::isfinite(max_hp) && hp >= 0.f && max_hp >= 0.f && hp <= kMaxHealth &&
               max_hp <= kMaxHealth && (flags & ~kActorFlagDead) == 0;
    }
};

// K_ACTORS (host -> guests, unreliable): batch of host adventure creatures
struct ActorBatch {
    RunHeader hdr;
    uint32_t time_ms = 0;
    std::vector<ActorSample> actors;

    void write(net::Writer& w) const
    {
        hdr.write(w);
        w.u32(time_ms);
        uint8_t n = (uint8_t)(actors.size() < kMaxActorsPerBatch ? actors.size() : kMaxActorsPerBatch);
        w.u8(n);
        for (uint8_t i = 0; i < n; i++)
            actors[i].write(w);
    }
    bool read(net::Reader& r)
    {
        actors.clear();
        if (!hdr.read(r))
            return false;
        time_ms = r.u32();
        uint8_t n = r.u8();
        if (!r.ok() || hdr.run_id == 0 || n > kMaxActorsPerBatch)
            return false;
        actors.reserve(n);
        for (uint8_t i = 0; i < n; i++) {
            ActorSample s;
            if (!s.read(r)) {
                actors.clear();
                return false;
            }
            actors.push_back(s);
        }
        return true;
    }
};

// K_HOST_ADV (host -> guests, reliable): active Quick Play / Space adventure announcement for guest join & F9 hint (BUG-1607)
struct HostAdventure {
    uint8_t active = 0; // 1 = host playing adventure (Play mode), 0 = host left adventure
    Key3 raw_key;       // host's real DBPF adventure resource key {inst, type, group}
    Key3 mission;       // host's stable content-based mission key (BUG-0963)
    std::string title;  // UTF-8 adventure title (<= kMaxTitleLen bytes)

    void write(net::Writer& w) const
    {
        w.u8(active ? 1u : 0u);
        put_key(w, raw_key);
        put_key(w, mission);
        std::string t = title.size() > kMaxTitleLen ? title.substr(0, kMaxTitleLen) : title;
        w.str8(t);
    }
    bool read(net::Reader& r)
    {
        active = r.u8();
        raw_key = get_key(r);
        mission = get_key(r);
        title = r.str8();
        return r.ok() && active <= 1u && title.size() <= kMaxTitleLen;
    }
};

} // namespace wire::adventure
