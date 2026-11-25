// Portable Creature Rivals logic (WP-R3 of 0.8.0, BUG-1547, docs/11-rivals.md 11.3.2, 11.4-11.9).
// No Windows or SporeKit dependencies so native Linux ASan/UBSan unit tests cover every rule and codec.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "net/buffer.h"
#include "rivals_logic.h"

namespace creature_rivals_logic {

constexpr int kMaxBrainLevel = 4; // Spore Creature stage max brain level (Tribe threshold, SporeCoop::g_BrainLevel)
constexpr int32_t kInfPerImpress = 10;
constexpr int32_t kInfPerAlly = 25;
constexpr float kMinNestSeparation = 25.0f; // m: avoid overlapping host home nest when farther nests exist
constexpr size_t kMaxSpeciesRecords = 512;
constexpr size_t kMaxReportedParts = 64;

// Species relationship table values (cSpeciesRelationshipManager::GetRelationship, 0x00BA40E0)
constexpr int32_t kRelNone = 0;       // unset (archetype default)
constexpr int32_t kRelHostile = 1;    // hostile
constexpr int32_t kRelUnfriendly = 2; // cautious / unfriendly
constexpr int32_t kRelNeutral = 3;    // neutral / impressed some
constexpr int32_t kRelFriendly = 4;   // friendly / impressed more
constexpr int32_t kRelExtinct = 5;    // extinct (shared across all players)
constexpr int32_t kRelAllied = 6;     // allied (and own species)

// Subkinds inside MSG_RIVALS_FACTION (81) for rivals::Stage::Creature
constexpr uint8_t kSkAssign = 1;      // wire::rivals_faction::SK_ASSIGN
constexpr uint8_t kSkNpcRels = 2;     // wire::rivals_faction::SK_NPC_RELS
constexpr uint8_t kSkPvpHit = 4;      // wire::rivals_faction::SK_PVP_HIT
constexpr uint8_t kSkProgress = 6;    // wire::rivals_faction::SK_PROGRESS
constexpr uint8_t kSkDraftApply = 7;  // wire::rivals_faction::SK_DRAFT_APPLY
constexpr uint8_t kSkChargeDna = 16;  // wire::rivals_faction::SK_STAGE_CUSTOM

// Draft apply operations
constexpr uint8_t kDraftOpUnlockPart = 1;
constexpr uint8_t kDraftOpPayoutDna = 2;

inline float dist3(float x1, float y1, float z1, float x2, float y2, float z2)
{
    float dx = x1 - x2, dy = y1 - y2, dz = z1 - z2;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline bool finite3(float x, float y, float z)
{
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

// ---------------- R2: Nest & Family Herd Selection ----------------
struct NestCandidate {
    uint32_t id = 0; // unique local handle / serial
    float x = 0, y = 0, z = 0;
    uint32_t species_inst = 0;
    bool is_avatar_herd = false;
    bool has_nest = false;
    bool is_epic = false;
    bool is_extinct = false;
    int members = 0;
};

// Deterministically pick an eligible planet nest at the requested BaseDistance from host_nest.
// Returns candidate.id, or 0 if none eligible.
inline uint32_t select_nest_for_guest(const NestCandidate& host_nest, const std::vector<NestCandidate>& candidates,
                                      const std::vector<uint32_t>& already_taken_ids, rivals::BaseDistance pref)
{
    std::set<uint32_t> taken(already_taken_ids.begin(), already_taken_ids.end());
    struct Entry {
        uint32_t id;
        float d;
    };
    std::vector<Entry> pool;
    std::vector<Entry> fallback;
    for (const auto& c : candidates) {
        if (c.id == 0 || !c.has_nest || c.is_avatar_herd || c.is_epic || c.is_extinct || taken.count(c.id))
            continue;
        if (!finite3(c.x, c.y, c.z))
            continue;
        float d = dist3(host_nest.x, host_nest.y, host_nest.z, c.x, c.y, c.z);
        if (d >= kMinNestSeparation)
            pool.push_back({c.id, d});
        else
            fallback.push_back({c.id, d});
    }
    if (pool.empty())
        pool = std::move(fallback);
    if (pool.empty())
        return 0;
    std::sort(pool.begin(), pool.end(), [](const Entry& a, const Entry& b) {
        if (std::fabs(a.d - b.d) > 1e-3f)
            return a.d < b.d;
        return a.id < b.id;
    });
    size_t n = pool.size();
    size_t idx = 0;
    if (pref == rivals::BaseDistance::Near) {
        idx = 0;
    } else if (pref == rivals::BaseDistance::Middle) {
        idx = n / 2;
    } else { // Far
        idx = n - 1;
    }
    return pool[idx].id;
}

// ---------------- R4: Per-Player Wild Species Relations ----------------
struct SpeciesEntry {
    uint32_t key[3] = {0, 0, 0};
    int32_t relation = kRelNone;
    uint32_t x = 0;
    uint32_t impressed = 0;
    uint32_t killed = 0;

    bool operator==(const SpeciesEntry& o) const
    {
        return key[0] == o.key[0] && key[1] == o.key[1] && key[2] == o.key[2] && relation == o.relation && x == o.x &&
               impressed == o.impressed && killed == o.killed;
    }
};

// Step relation one notch toward hostile (TurnAgainst). Returns false if already hostile or extinct.
inline bool step_toward_hostile(int32_t rel, int32_t& out)
{
    switch (rel) {
    case kRelAllied:
        out = kRelFriendly;
        return true;
    case kRelFriendly:
        out = kRelNeutral;
        return true;
    case kRelNeutral:
        out = kRelUnfriendly;
        return true;
    case kRelUnfriendly:
        out = kRelHostile;
        return true;
    case kRelNone:
        out = kRelUnfriendly;
        return true;
    default: // kRelHostile (1) or kRelExtinct (5)
        out = rel;
        return false;
    }
}

// Step relation one notch toward allied (SmoothOver). Returns false if already allied or extinct.
inline bool step_toward_friendly(int32_t rel, int32_t& out)
{
    switch (rel) {
    case kRelHostile:
        out = kRelUnfriendly;
        return true;
    case kRelUnfriendly:
        out = kRelNeutral;
        return true;
    case kRelNeutral:
        out = kRelFriendly;
        return true;
    case kRelFriendly:
        out = kRelAllied;
        return true;
    case kRelNone:
        out = kRelNeutral;
        return true;
    default: // kRelAllied (6) or kRelExtinct (5)
        out = rel;
        return false;
    }
}

struct ImpressOutcome {
    bool changed = false;
    bool became_allied = false;
};

struct KillOutcome {
    bool changed = false;
    bool became_extinct = false;
    bool blocked_home = false;
};

struct PerPlayerSpeciesTables {
    std::map<uint32_t, SpeciesEntry> by_player[rivals::kMaxPlayers];
    std::set<uint32_t> extinct_species;
    std::map<uint32_t, uint32_t> shared_kills;
    uint32_t home_species[rivals::kMaxPlayers] = {};

    void reset()
    {
        for (int i = 0; i < rivals::kMaxPlayers; i++) {
            by_player[i].clear();
            home_species[i] = 0;
        }
        extinct_species.clear();
        shared_kills.clear();
    }

    void set_home_species(uint8_t player, uint32_t species_inst)
    {
        if (rivals::valid_player(player))
            home_species[player] = species_inst;
    }

    bool is_home_species(uint32_t species_inst) const
    {
        if (species_inst == 0)
            return false;
        for (int i = 0; i < rivals::kMaxPlayers; i++)
            if (home_species[i] == species_inst)
                return true;
        return false;
    }

    bool is_extinct(uint32_t species_inst) const { return extinct_species.count(species_inst) != 0; }

    int32_t get_relation(uint8_t player, uint32_t species_inst) const
    {
        if (!rivals::valid_player(player) || species_inst == 0)
            return kRelNone;
        if (extinct_species.count(species_inst))
            return kRelExtinct;
        auto it = by_player[player].find(species_inst);
        return it != by_player[player].end() ? it->second.relation : kRelNone;
    }

    SpeciesEntry& ensure_entry(uint8_t player, const uint32_t key[3])
    {
        auto& tbl = by_player[player];
        auto it = tbl.find(key[0]);
        if (it == tbl.end()) {
            SpeciesEntry e{};
            e.key[0] = key[0];
            e.key[1] = key[1];
            e.key[2] = key[2];
            if (extinct_species.count(key[0]))
                e.relation = kRelExtinct;
            it = tbl.emplace(key[0], e).first;
        } else if (it->second.key[1] == 0 && key[1] != 0) {
            it->second.key[1] = key[1];
            it->second.key[2] = key[2];
        }
        return it->second;
    }

    void mark_extinct(const uint32_t key[3])
    {
        if (key[0] == 0 || is_home_species(key[0]))
            return;
        extinct_species.insert(key[0]);
        for (int p = 0; p < rivals::kMaxPlayers; p++) {
            auto& e = ensure_entry((uint8_t)p, key);
            e.relation = kRelExtinct;
        }
    }

    // Sync host's native table entries into player 0 (propagating shared extinctions to everyone).
    void sync_from_host_native(const std::vector<SpeciesEntry>& native)
    {
        for (const auto& r : native) {
            if (r.key[0] == 0 || is_home_species(r.key[0]))
                continue;
            if (r.relation == kRelExtinct) {
                mark_extinct(r.key);
                by_player[0][r.key[0]].killed = std::max(by_player[0][r.key[0]].killed, r.killed);
                continue;
            }
            if (extinct_species.count(r.key[0])) {
                auto& e = ensure_entry(0, r.key);
                e.relation = kRelExtinct;
                continue;
            }
            by_player[0][r.key[0]] = r;
        }
    }

    ImpressOutcome record_impress(uint8_t player, const uint32_t key[3], uint32_t ally_need = 3,
                                  uint32_t friendly_need = 2)
    {
        ImpressOutcome out{};
        if (!rivals::valid_player(player) || key[0] == 0 || is_extinct(key[0]) || is_home_species(key[0]))
            return out;
        auto& e = ensure_entry(player, key);
        int32_t prev = e.relation;
        e.impressed++;
        if (ally_need > 0 && e.impressed >= ally_need)
            e.relation = kRelAllied;
        else if (friendly_need > 0 && e.impressed >= friendly_need && e.relation < kRelFriendly)
            e.relation = kRelFriendly;
        else if (e.relation < kRelNeutral)
            e.relation = kRelNeutral;
        out.changed = true;
        out.became_allied = (prev != kRelAllied && e.relation == kRelAllied);
        return out;
    }

    KillOutcome record_kill(uint8_t player, const uint32_t key[3], uint32_t extinct_need = 3)
    {
        KillOutcome out{};
        if (!rivals::valid_player(player) || key[0] == 0)
            return out;
        if (is_home_species(key[0])) {
            out.blocked_home = true;
            return out;
        }
        if (is_extinct(key[0]))
            return out;
        auto& e = ensure_entry(player, key);
        e.killed++;
        e.impressed = 0;
        e.relation = kRelHostile;
        out.changed = true;
        uint32_t& tot = shared_kills[key[0]];
        tot++;
        if (extinct_need > 0 && tot >= extinct_need) {
            mark_extinct(key);
            out.became_extinct = true;
        }
        return out;
    }

    bool apply_turn_against(uint8_t rival_id, const uint32_t key[3])
    {
        if (!rivals::valid_player(rival_id) || key[0] == 0 || is_extinct(key[0]) || is_home_species(key[0]))
            return false;
        auto& e = ensure_entry(rival_id, key);
        int32_t nxt = e.relation;
        if (!step_toward_hostile(e.relation, nxt))
            return false;
        e.relation = nxt;
        if (nxt < kRelFriendly)
            e.impressed = 0;
        return true;
    }

    bool apply_smooth_over(uint8_t actor_id, const uint32_t key[3])
    {
        if (!rivals::valid_player(actor_id) || key[0] == 0 || is_extinct(key[0]) || is_home_species(key[0]))
            return false;
        auto& e = ensure_entry(actor_id, key);
        int32_t nxt = e.relation;
        if (!step_toward_friendly(e.relation, nxt))
            return false;
        e.relation = nxt;
        return true;
    }

    void count_stats(uint8_t player, uint16_t& allies_out, uint16_t& extinct_out) const
    {
        allies_out = 0;
        extinct_out = (uint16_t)std::min<size_t>(extinct_species.size(), 65535);
        if (!rivals::valid_player(player))
            return;
        for (const auto& [inst, e] : by_player[player]) {
            if (!is_home_species(inst) && e.relation == kRelAllied)
                allies_out++;
        }
    }

    std::vector<SpeciesEntry> entries_for(uint8_t player) const
    {
        std::vector<SpeciesEntry> out;
        if (!rivals::valid_player(player))
            return out;
        out.reserve(by_player[player].size());
        for (const auto& [inst, e] : by_player[player]) {
            if (out.size() >= kMaxSpeciesRecords)
                break;
            if (!is_home_species(inst))
                out.push_back(e);
        }
        return out;
    }
};

// ---------------- War PvP & R7 Home Nest Protection Policy ----------------
enum class TargetKind : uint8_t {
    WildNpc = 0,
    PlayerAvatar = 1,
    PlayerFamily = 2,
};

inline bool allow_creature_damage(bool rivals_active, bool friendly_fire_setting, uint8_t attacker_player,
                                  uint8_t target_player, TargetKind kind, bool at_war)
{
    if (kind == TargetKind::WildNpc)
        return true;
    if (!rivals_active) {
        if (kind == TargetKind::PlayerAvatar)
            return attacker_player != target_player && friendly_fire_setting;
        // Together mode: own family cannot be hit by own party
        return false;
    }
    if (attacker_player == target_player)
        return false;
    return at_war;
}

inline bool block_extinction(bool rivals_active, bool is_player_home_species)
{
    return rivals_active && is_player_home_species;
}

inline bool reached_tribe_threshold(int brain_level)
{
    return brain_level >= kMaxBrainLevel;
}

// ---------------- R6: Creature Part Draft Builder ----------------
using PartKey = std::pair<uint32_t, uint32_t>;

inline void build_creature_draft_items(const std::vector<PartKey>& winner_parts,
                                       const std::set<PartKey>& shared_baseline,
                                       std::vector<rivals::Draft::Item>& out_items,
                                       std::vector<PartKey>& out_keys)
{
    out_items.clear();
    out_keys.clear();
    std::vector<PartKey> sorted = winner_parts;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    // Prefer parts the winner has that are not in the shared baseline first
    std::vector<PartKey> picked;
    for (const auto& k : sorted) {
        if (!shared_baseline.count(k)) {
            picked.push_back(k);
            if ((int)picked.size() >= rivals::kMaxDraftItems)
                break;
        }
    }
    if ((int)picked.size() < rivals::kMaxDraftItems) {
        for (const auto& k : sorted) {
            if (shared_baseline.count(k)) {
                picked.push_back(k);
                if ((int)picked.size() >= rivals::kMaxDraftItems)
                    break;
            }
        }
    }
    for (size_t i = 0; i < picked.size(); i++) {
        char lbl[40];
        snprintf(lbl, sizeof lbl, "Creature Part %08X", picked[i].first);
        rivals::Draft::Item it{};
        it.id = (uint32_t)(i + 1);
        it.kind = 1; // Creature part
        it.label = lbl;
        out_items.push_back(std::move(it));
        out_keys.push_back(picked[i]);
    }
}

// ---------------- Wire Payloads (MSG_RIVALS_FACTION, Stage::Creature) ----------------
struct AssignEntry {
    uint8_t player_id = 0;
    float nest_x = 0, nest_y = 0, nest_z = 0;
    uint32_t species_key[3] = {0, 0, 0};
    uint8_t flags = 0; // bit 0 = assigned, bit 1 = species swapped
};

inline void write_assign(net::Writer& w, const std::vector<AssignEntry>& entries)
{
    uint8_t n = (uint8_t)std::min<size_t>(entries.size(), rivals::kMaxPlayers);
    w.u8(n);
    for (uint8_t i = 0; i < n; i++) {
        const auto& e = entries[i];
        w.u8(e.player_id);
        w.f32(e.nest_x);
        w.f32(e.nest_y);
        w.f32(e.nest_z);
        w.u32(e.species_key[0]);
        w.u32(e.species_key[1]);
        w.u32(e.species_key[2]);
        w.u8(e.flags);
    }
}

inline bool read_assign(net::Reader& r, std::vector<AssignEntry>& out)
{
    out.clear();
    uint8_t n = r.u8();
    if (!r.ok() || n > rivals::kMaxPlayers)
        return false;
    for (uint8_t i = 0; i < n; i++) {
        AssignEntry e{};
        e.player_id = r.u8();
        e.nest_x = r.f32();
        e.nest_y = r.f32();
        e.nest_z = r.f32();
        e.species_key[0] = r.u32();
        e.species_key[1] = r.u32();
        e.species_key[2] = r.u32();
        e.flags = r.u8();
        if (!r.ok() || !rivals::valid_player(e.player_id) || !finite3(e.nest_x, e.nest_y, e.nest_z))
            return false;
        out.push_back(e);
    }
    return true;
}

inline void write_npc_rels(net::Writer& w, const std::vector<SpeciesEntry>& entries)
{
    uint16_t n = (uint16_t)std::min<size_t>(entries.size(), kMaxSpeciesRecords);
    w.u16(n);
    for (uint16_t i = 0; i < n; i++) {
        const auto& e = entries[i];
        w.u32(e.key[0]);
        w.u32(e.key[1]);
        w.u32(e.key[2]);
        w.u32((uint32_t)e.relation);
        w.u32(e.x);
        w.u32(e.impressed);
        w.u32(e.killed);
    }
}

inline bool read_npc_rels(net::Reader& r, std::vector<SpeciesEntry>& out)
{
    out.clear();
    uint16_t n = r.u16();
    if (!r.ok() || n > kMaxSpeciesRecords)
        return false;
    out.reserve(n);
    for (uint16_t i = 0; i < n; i++) {
        SpeciesEntry e{};
        e.key[0] = r.u32();
        e.key[1] = r.u32();
        e.key[2] = r.u32();
        e.relation = (int32_t)r.u32();
        e.x = r.u32();
        e.impressed = r.u32();
        e.killed = r.u32();
        if (!r.ok())
            return false;
        out.push_back(e);
    }
    return true;
}

struct ProgressMsg {
    uint8_t brain_level = 0;
    float dna = 0;
    float evo_bar = 0;
    uint16_t parts_total = 0;
    std::vector<PartKey> parts;
};

inline void write_progress(net::Writer& w, const ProgressMsg& m)
{
    w.u8(m.brain_level);
    w.f32(m.dna);
    w.f32(m.evo_bar);
    w.u16(m.parts_total);
    uint8_t n = (uint8_t)std::min<size_t>(m.parts.size(), kMaxReportedParts);
    w.u8(n);
    for (uint8_t i = 0; i < n; i++) {
        w.u32(m.parts[i].first);
        w.u32(m.parts[i].second);
    }
}

inline bool read_progress(net::Reader& r, ProgressMsg& out)
{
    out = ProgressMsg{};
    out.brain_level = r.u8();
    out.dna = r.f32();
    out.evo_bar = r.f32();
    out.parts_total = r.u16();
    uint8_t n = r.u8();
    if (!r.ok() || n > kMaxReportedParts || !std::isfinite(out.dna) || !std::isfinite(out.evo_bar))
        return false;
    out.parts.reserve(n);
    for (uint8_t i = 0; i < n; i++) {
        uint32_t lo = r.u32(), hi = r.u32();
        if (!r.ok())
            return false;
        out.parts.push_back({lo, hi});
    }
    return true;
}

struct DraftApplyMsg {
    uint8_t op = 0;
    uint32_t lo = 0;
    uint32_t hi = 0;
    int32_t amount = 0;
};

inline void write_draft_apply(net::Writer& w, const DraftApplyMsg& m)
{
    w.u8(m.op);
    w.u32(m.lo);
    w.u32(m.hi);
    w.u32((uint32_t)m.amount);
}

inline bool read_draft_apply(net::Reader& r, DraftApplyMsg& out)
{
    out.op = r.u8();
    out.lo = r.u32();
    out.hi = r.u32();
    out.amount = (int32_t)r.u32();
    return r.ok() && (out.op == kDraftOpUnlockPart || out.op == kDraftOpPayoutDna);
}

struct PvpHitMsg {
    uint8_t victim_player = 0;
    uint8_t attacker_player = 0;
    float damage = 0;
    int32_t dmg_type = 2;
};

inline void write_pvp_hit(net::Writer& w, const PvpHitMsg& m)
{
    w.u8(m.victim_player);
    w.u8(m.attacker_player);
    w.f32(m.damage);
    w.u32((uint32_t)m.dmg_type);
}

inline bool read_pvp_hit(net::Reader& r, PvpHitMsg& out)
{
    out.victim_player = r.u8();
    out.attacker_player = r.u8();
    out.damage = r.f32();
    out.dmg_type = (int32_t)r.u32();
    return r.ok() && rivals::valid_player(out.victim_player) && rivals::valid_player(out.attacker_player) &&
           std::isfinite(out.damage) && out.damage > 0.0f && out.damage <= 1000.0f;
}

} // namespace creature_rivals_logic
