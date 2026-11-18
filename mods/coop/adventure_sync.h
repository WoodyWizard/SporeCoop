// Portable, bounded Adventure messages. Native pointers never cross the network.
#pragma once
#include <array>
#include <vector>
#include "net/buffer.h"

namespace adventure_sync {
constexpr uint32_t kMaxCredits = 512;
constexpr uint32_t kMaxRequired = 255;
// Hello / Welcome / Propose / Commit: shared objective credits (host journal).
// Death (BUG-9089, 44): a scenario object of this id died n times in the sender's game.
// Outcome (BUG-9089, 44): the host's mission ended (state 5 success, 6 failure).
// Hit (BUG-9114): guest -> host, the guest's captain hit a host-driven scenario creature (actor key).
enum Kind : uint8_t { Hello, Welcome, Propose, Commit, Death, Outcome, Hit };
// One credit of one goal. A credit for an object (a defeated building or creature) is that object's,
// once, whoever's game saw it (object = the scenario marker id, see adventure.cpp object_id); a credit
// without an object is the goal's count-th one (count = the native count before it).
struct Goal {
    uint32_t act = 0, slot = 0, kind = 0, target = 0, secondary = 0, required = 0;
    uint32_t object = 0, count = 0;
    bool operator==(const Goal&) const = default;
    bool same_credit(const Goal& o) const {
        return act == o.act && slot == o.slot && kind == o.kind && target == o.target && secondary == o.secondary &&
               required == o.required && (object ? object == o.object : !o.object && count == o.count);
    }
    bool supported() const {
        return act < 16 && slot < 8 && kind >= 1 && kind <= 15 && target < 0x100000 &&
               (secondary == UINT32_MAX || secondary < 0x100000) && required >= 1 && required <= kMaxRequired &&
               count < required;
    }
    void write(net::Writer& w) const {
        w.u32(act); w.u32(slot); w.u32(kind); w.u32(target); w.u32(secondary); w.u32(required);
        w.u32(object); w.u32(count);
    }
    void read(net::Reader& r) {
        act=r.u32(); slot=r.u32(); kind=r.u32(); target=r.u32(); secondary=r.u32(); required=r.u32();
        object=r.u32(); count=r.u32();
    }
};
struct Packet {
    Kind kind = Hello;
    std::array<uint32_t,3> mission{};
    uint32_t run = 0, visitor = 0, sequence = 0;
    Goal goal;
    uint32_t object = 0, deaths = 0; // Death
    uint32_t state = 0;              // Outcome
    uint32_t key = 0; float damage = 0; uint32_t damage_type = 0; // Hit: the host's actor key (+ object id)
    void write(net::Writer& w) const {
        w.u8(uint8_t(kind)); for (auto v:mission) w.u32(v);
        w.u32(run); w.u32(visitor); w.u32(sequence);
        if (kind == Propose || kind == Commit) goal.write(w);
        if (kind == Death) { w.u32(object); w.u32(deaths); }
        if (kind == Outcome) w.u32(state);
        if (kind == Hit) { w.u32(object); w.u32(key); w.f32(damage); w.u32(damage_type); }
    }
    bool read(net::Reader& r) {
        kind=Kind(r.u8()); for (auto& v:mission) v=r.u32();
        run=r.u32(); visitor=r.u32(); sequence=r.u32();
        if (kind == Propose || kind == Commit) goal.read(r);
        if (kind == Death) { object=r.u32(); deaths=r.u32(); }
        if (kind == Outcome) state=r.u32();
        if (kind == Hit) { object=r.u32(); key=r.u32(); damage=r.f32(); damage_type=r.u32(); }
        return r.ok() && !r.left() && kind <= Hit && visitor &&
            sequence <= kMaxCredits && (mission[0] || mission[1] || mission[2]) &&
            (kind == Hello || run) &&
            ((kind != Propose && kind != Commit) || goal.supported()) &&
            (kind != Commit || sequence) &&
            (kind != Death || (object && deaths >= 1 && deaths <= 64)) &&
            (kind != Outcome || state == 5 || state == 6) &&
            (kind != Hit || (object && key && damage > 0 && damage < 1e6f && damage_type < 256));
    }
};

// BUG-9114: the host's scenario creatures (enemies, allies, characters) as its game simulates them.
// key: the host's object (unique there); id: the scenario object id shared by every game (object_id).
constexpr uint32_t kMaxActorsPerBatch = 20; // 20 x 57 bytes + header < net::kMaxPacket
constexpr uint8_t kActorDead = 1;
struct Actor {
    uint32_t key = 0, id = 0;
    float pos[3]{}, rot[4]{}, vel[3]{};
    float health = 0, max_health = 0;
    uint8_t flags = 0;
    void write(net::Writer& w) const {
        w.u32(key); w.u32(id);
        for (float v:pos) w.f32(v);
        for (float v:rot) w.f32(v);
        for (float v:vel) w.f32(v);
        w.f32(health); w.f32(max_health); w.u8(flags);
    }
    void read(net::Reader& r) {
        key=r.u32(); id=r.u32();
        for (float& v:pos) v=r.f32();
        for (float& v:rot) v=r.f32();
        for (float& v:vel) v=r.f32();
        health=r.f32(); max_health=r.f32(); flags=r.u8();
    }
    bool sane() const {
        auto finite=[](float v){ return v==v && v>-1e7f && v<1e7f; };
        for (float v:pos) if (!finite(v)) return false;
        for (float v:rot) if (!finite(v) || v<-1.01f || v>1.01f) return false;
        for (float v:vel) if (!finite(v)) return false;
        return key && id && finite(health) && finite(max_health) && flags <= kActorDead;
    }
};
struct ActorBatch {
    std::array<uint32_t,3> mission{};
    uint32_t run = 0, time_ms = 0;
    std::vector<Actor> actors;
    void write(net::Writer& w) const {
        for (auto v:mission) w.u32(v);
        w.u32(run); w.u32(time_ms); w.u8(uint8_t(actors.size()));
        for (const auto& a:actors) a.write(w);
    }
    bool read(net::Reader& r) {
        for (auto& v:mission) v=r.u32();
        run=r.u32(); time_ms=r.u32();
        uint8_t n=r.u8();
        if (!r.ok() || n>kMaxActorsPerBatch) return false;
        actors.resize(n);
        for (auto& a:actors) a.read(r);
        if (!r.ok() || r.left() || !run || !(mission[0] || mission[1] || mission[2])) return false;
        for (const auto& a:actors) if (!a.sane()) return false;
        return true;
    }
};
}
