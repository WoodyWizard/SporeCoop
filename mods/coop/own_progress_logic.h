// Own progress (BUG-9265, the owner's rule of 2026-11-20): in the Cell and Creature stages every player has their OWN
// DNA and their OWN evolution bar, earned by what they do themselves; nobody evolves alone: a player whose bar is full
// waits (and plays on) until every player's own bar is full, then the existing "I'm ready" vote moves everybody on
// together (docs/11-rivals.md's "Together" variant: wait for everyone, no "first wins"). The session setting
// all.own_progress (default on) switches it; off: the party's one shared total (dna_route.h, dna_pace.h).
//
// The rules and the wire / file formats, portable (no game calls) so that `make test` checks them
// (tests/validate_tests.cpp). The game side is own_progress.cpp; docs/01-architecture.md#architecture-own-progress.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "net/buffer.h"

namespace own_progress {

// the stage mode ids (modes.h is not portable)
constexpr uint32_t kCell = 0x1654C00, kCreature = 0x1654C01;
constexpr int kCellFull = 1000;   // Cell: cCellSerializableData +0x1C foodProgression reaches 1000 (docs/re/cell.md 4)
constexpr int kCreatureFull = 4;  // Creature: the avatar's brain level (vt 0xD8, [0x169E370]) "fully grown" (BUG-9265)

// one player's progress in its own game
struct Values {
    uint32_t mode = 0;  // kCell or kCreature (0: none)
    float dna = 0;      // Creature: GetEvolutionPoints (the HUD's DNA)
    float bar = 0;      // Creature: cPlayer +0x10F0 mCurrentGoalProgress (the evolution bar, the total earned)
    int32_t food = 0;   // Cell: cCellSerializableData +0x1C (the bar, 0..1000)
    int32_t spent = 0;  // Cell: +0x2C, the DNA its own cell editors spent (DNA = DNAForFood(food) - spent)
    uint8_t level = 0;  // Creature: the brain level
    bool operator==(const Values&) const = default;
};

inline bool is_stage(uint32_t m) { return m == kCell || m == kCreature; }

inline bool full(const Values& v)
{
    if (v.mode == kCell)
        return v.food >= kCellFull;
    if (v.mode == kCreature)
        return v.level >= kCreatureFull;
    return false;
}

// the bar in percent (Creature: by brain level, the game's bar segments)
inline int percent(const Values& v)
{
    int p = 0;
    if (v.mode == kCell)
        p = (int)((int64_t)v.food * 100 / kCellFull);
    else if (v.mode == kCreature)
        p = v.level * 100 / kCreatureFull;
    return std::clamp(p, 0, 100);
}

// ---- who still fills their bar. A player counts as full only with a report of the stage played now.
struct Player {
    uint8_t id = 0;
    std::string name;
    bool reported = false; // its progress of this stage is known (a guest synced and reported, the host itself)
    uint32_t mode = 0;
    bool full = false;
    int pct = 0;
};

// true when every player (at least two) is full in `mode`; `waiting` names the others ("Bob 64%, Ann ?")
inline bool all_full(const std::vector<Player>& ps, uint32_t mode, std::string* waiting = nullptr)
{
    bool all = ps.size() >= 2;
    std::string w;
    for (const auto& p : ps) {
        bool ok = p.reported && p.mode == mode && p.full;
        if (ok)
            continue;
        all = false;
        if (!w.empty())
            w += ", ";
        w += p.name.empty() ? "player " + std::to_string(p.id) : p.name;
        w += p.reported && p.mode == mode ? " " + std::to_string(p.pct) + "%" : " (not known yet)";
    }
    if (waiting)
        *waiting = w;
    return all;
}

// ---- MSG_PROGRESS (77, session family): u8 kind, payload
enum Kind : uint8_t {
    kAsk = 1,    // guest -> host: u32 mode. A copy of the host's world opened here: my values of this stage, please
    kSet = 2,    // host -> guest: Values (has = 0: none kept, the world's values are yours from now on)
    kReport = 3, // guest -> host: Values, u8 flags (1 full). Every second while it plays the host's stage
    kTable = 4,  // host -> guests: u8 n, n x {u8 player, u32 mode, u8 flags (1 known, 2 full), u8 pct}
    kGain = 5,   // host -> guest: f32 DNA its deed earned on the host's game (a hunt, a friend): the guest's own
};

inline void write_values(net::Writer& w, const Values& v)
{
    w.u32(v.mode);
    w.f32(v.dna);
    w.f32(v.bar);
    w.u32((uint32_t)v.food);
    w.u32((uint32_t)v.spent);
    w.u8(v.level);
}

inline bool sane(const Values& v)
{
    return (v.mode == kCell || v.mode == kCreature) && std::isfinite(v.dna) && std::isfinite(v.bar) && v.dna >= 0 &&
           v.dna < 100000 && v.bar > -100000 && v.bar < 100000 && v.food >= -2000 && v.food <= 2000 && v.spent >= -100000 &&
           v.spent <= 100000 && v.level <= 16;
}

inline bool read_values(net::Reader& r, Values& v)
{
    Values t;
    t.mode = r.u32();
    t.dna = r.f32();
    t.bar = r.f32();
    t.food = (int32_t)r.u32();
    t.spent = (int32_t)r.u32();
    t.level = r.u8();
    if (!r.ok() || !sane(t))
        return false;
    v = t;
    return true;
}

// ---- the side file next to the planet (<planet>.spo.duoprogress, founders::write_side): every player's last values,
// by the player's key (identity.h key(): "id:<identity>", "name:<name>" for a partner without one). OPG1: u32 'OPG1',
// u8 n, n x {str8 key, Values}. The host's own values are in its save; it keeps its own line too (status, tests).
constexpr uint32_t kFileMagic = 0x3147504Fu; // OPG1
constexpr size_t kMaxRecords = 16;

struct Record {
    std::string key;
    Values v;
    bool operator==(const Record&) const = default;
};

inline std::vector<uint8_t> encode_file(const std::vector<Record>& recs)
{
    net::Writer w;
    w.u32(kFileMagic);
    size_t n = std::min(recs.size(), kMaxRecords);
    w.u8((uint8_t)n);
    for (size_t i = 0; i < n; i++) {
        w.str8(recs[i].key);
        write_values(w, recs[i].v);
    }
    return w.data;
}

inline bool decode_file(const std::vector<uint8_t>& bytes, std::vector<Record>& out)
{
    net::Reader r(bytes.data(), bytes.size());
    if (r.u32() != kFileMagic)
        return false;
    unsigned n = r.u8();
    if (!r.ok() || n > kMaxRecords)
        return false;
    std::vector<Record> recs;
    for (unsigned i = 0; i < n; i++) {
        Record rec;
        rec.key = r.str8();
        if (!r.ok() || rec.key.empty() || !read_values(r, rec.v))
            return false;
        recs.push_back(std::move(rec));
    }
    if (r.left())
        return false;
    out = std::move(recs);
    return true;
}

// the record of `key` (inserted when missing)
inline Record& record_of(std::vector<Record>& recs, const std::string& key)
{
    for (auto& r : recs)
        if (r.key == key)
            return r;
    recs.push_back({key, {}});
    return recs.back();
}
inline const Record* find_record(const std::vector<Record>& recs, const std::string& key)
{
    for (auto& r : recs)
        if (r.key == key)
            return &r;
    return nullptr;
}

// what a guest's game is given back after a copy of the host's world opened: the record of the stage played now, or
// nothing (a new player, another stage: the world's values are its own from now on)
inline const Values* restore_for(const std::vector<Record>& recs, const std::string& key, uint32_t mode)
{
    const Record* r = find_record(recs, key);
    return r && r->v.mode == mode && is_stage(mode) ? &r->v : nullptr;
}

// Creature: how a restore reaches the game. The bar (cPlayer +0x10F0) goes up through the game's AddEvolutionPoints (the
// brain upgrades and the migration see it), so a gain of `up` is added first; then the DNA is written (SetEvolutionPoints)
// and a bar that must go down is written directly (`bar_write`, NAN: none).
struct CreatureApply {
    float up = 0;           // through AddEvolutionPoints (raises the DNA and the bar)
    float dna = 0;          // then SetEvolutionPoints
    float bar_write = NAN;  // then cPlayer +0x10F0 (only when the bar must go down)
};
inline CreatureApply creature_apply(float game_bar, const Values& want)
{
    CreatureApply a;
    a.dna = want.dna;
    if (!std::isfinite(game_bar))
        return a;
    if (want.bar > game_bar + 0.5f)
        a.up = want.bar - game_bar;
    else if (want.bar < game_bar - 0.5f)
        a.bar_write = want.bar;
    return a;
}

} // namespace own_progress
