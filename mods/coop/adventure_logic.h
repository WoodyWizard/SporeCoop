// Pure portable logic for co-op Galactic Adventures (WP-A of 0.7.99, BUG-1474 / BUG-0793):
//   - E-A: host run journal (goals, act changes, host-decided outcome, object deaths) and guest ordered replay
//   - E-B: (marker, class) actor pairing within 12 m, unpaired 5 s expiry, stream timeout returning AI
//   - per-captain puppet scale (1.0x remote captain scale)
// No Windows or SporeKit dependencies: tested natively in tests/adventure_tests.cpp under ASan + UBSan.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "net/compat.h"
#include "wire/adventure.h"

namespace adventure {

// Pairing and stream timing rules (BUG-1474, docs/re/adventure.md#adventure-npc-proxies)
constexpr float kPairMaxDist = 12.0f;       // m: max distance to pair a host actor with a local scenario creature
constexpr double kStreamWarmupSec = 3.0;    // s: host actor stream must flow this long before removing guest leftovers
constexpr double kStreamTimeoutSec = 5.0;   // s: stream silent longer than this hands NPCTickAI back to guest
constexpr double kUnpairedExpirySec = 5.0;  // s: unpaired guest leftover creature removed after this duration
constexpr float kLegacyPuppetScaleK = 1.15f; // BUG-1254 legacy Creature puppet factor (used only with captain_scale off)

// Goal kinds in Spore Galactic Adventures (docs/re/adventure.md)
constexpr uint32_t kGoalKindDefeat = 3;
constexpr uint32_t kAdventureResourceType = 0x366A930Du;
constexpr uint32_t kAdventureGroup = 0x408A0000u;

// Deterministic FNV-1a 32-bit hash for scenario object signatures across machines
inline uint32_t fnv1a_32(const void* data, size_t len, uint32_t h = 2166136261u)
{
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

inline uint32_t fmix32(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

inline uint32_t rotl32(uint32_t v, unsigned r) { return (v << r) | (v >> (32u - r)); }

inline int32_t quantize_f(float v, float scale)
{
    if (!std::isfinite(v))
        return 0;
    double d = (double)v * (double)scale;
    if (d > 1e8)
        d = 1e8;
    else if (d < -1e8)
        d = -1e8;
    return (int32_t)std::llround(d);
}

// ---- BUG-0963: Stable content-based adventure identity ----
// Two players launching Quick Play copies of the same adventure can receive different local DBPF instance IDs
// (cScenarioResource +0x08). Hashing the immutable Play-mode blueprint (act count, class count, avatar spawn pos,
// and an order-independent mix of live marker placements) yields identical place & mission keys across machines
// without depending on local instance IDs or per-player captain keys.
struct ContentSummary {
    uint32_t num_acts = 0;
    uint32_t num_classes = 0;
    uint32_t live_markers = 0;
    int32_t spawn_q[3] = {};
    uint32_t marker_sum = 0;
    uint32_t marker_xor = 0;

    void set_spawn(float x, float y, float z)
    {
        spawn_q[0] = quantize_f(x, 10.f);
        spawn_q[1] = quantize_f(y, 10.f);
        spawn_q[2] = quantize_f(z, 10.f);
    }

    void add_marker(int32_t class_id, float x, float y, float z, float scale)
    {
        int32_t rec[5] = {class_id, quantize_f(x, 10.f), quantize_f(y, 10.f), quantize_f(z, 10.f),
                          quantize_f(scale, 100.f)};
        uint32_t h = fmix32(fnv1a_32(rec, sizeof(rec)));
        marker_sum += h;
        marker_xor ^= rotl32(h, 13u);
        live_markers++;
    }
};

inline bool content_signature(const ContentSummary& s, uint32_t& out_lo, uint32_t& out_hi)
{
    if (s.num_acts == 0 && s.num_classes == 0 && s.live_markers == 0)
        return false;
    uint32_t words[8] = {
        s.num_acts,
        s.num_classes,
        s.live_markers,
        (uint32_t)s.spawn_q[0],
        (uint32_t)s.spawn_q[1],
        (uint32_t)s.spawn_q[2],
        s.marker_sum,
        s.marker_xor,
    };
    uint32_t a = fmix32(fnv1a_32(words, sizeof(words), 2166136261u));
    uint32_t b = fmix32(fnv1a_32(words, sizeof(words), 0x9E3779B9u));
    out_lo = a | 1u;
    out_hi = b | 1u;
    return true;
}

inline wire::Key3 content_mission_key(uint32_t sig_lo, uint32_t sig_hi)
{
    return wire::Key3{{sig_lo | 1u, kAdventureResourceType, sig_hi | 1u}};
}

inline uint32_t content_place(uint32_t sig_lo) { return sig_lo | 1u; }

// Deterministic marker signature from scenario marker (+0x18, 0x24 bytes: class, pos, rot, scale) and class (+0x1C).
// Both machines load the same adventure resource so marker bytes 0..0x24 are identical; wave-spawned creatures with a
// null marker still produce a deterministic non-zero hash from class_id alone.
inline uint32_t marker_sig(const void* marker_bytes_36, int32_t class_id)
{
    uint32_t h = 2166136261u;
    if (marker_bytes_36)
        h = fnv1a_32(marker_bytes_36, 0x24, h);
    else {
        uint32_t zero = 0;
        h = fnv1a_32(&zero, sizeof(zero), h);
    }
    h = fnv1a_32(&class_id, sizeof(class_id), h);
    return h ? h : 1u;
}

inline bool peer_supports(const std::vector<net::FamilyRev>& fams, bool has)
{
    return net::compat::has_minor(fams, has, (uint8_t)net::Family::creature, 1, wire::adventure::kCreatureMinor);
}

// Per-captain puppet scale in Galactic Adventures (BUG-0793):
// With exact_scale_fix on, renders the partner captain at its true 1.0x scale (PlayerState.body).
// With exact_scale_fix off, falls back to the legacy 1.15x Creature puppet factor (BUG-1254).
inline float captain_puppet_scale(float remote_body, float own_scale, bool exact_scale_fix)
{
    float base = (std::isfinite(remote_body) && remote_body > 0.05f && remote_body < 50.f)
                     ? remote_body
                     : ((std::isfinite(own_scale) && own_scale > 0.05f && own_scale < 50.f) ? own_scale : 1.0f);
    return exact_scale_fix ? base : base * kLegacyPuppetScaleK;
}

// Defeat goal (kind 3) of a named spatial object must pass natively in every game; holding it causes Spore to fail
// the mission ("You can no longer complete this goal").
inline bool should_pass_credit_natively(uint32_t goal_kind, bool has_spatial)
{
    return goal_kind == kGoalKindDefeat && has_spatial;
}

// Guest suppresses its own local failure (SetState 6) while connected to an active shared run without a host outcome.
inline bool should_hold_guest_state(int32_t state, bool connected_in_run, bool host_outcome_received)
{
    return connected_in_run && !host_outcome_received && state == 6;
}

// ---------------------------------------------------------------- E-A: Shared Run Journal

enum class EntryKind : uint8_t {
    Goal = 1,
    Act = 2,
    Outcome = 3,
    Death = 4,
};

struct JournalEntry {
    uint32_t seq = 0;
    EntryKind kind = EntryKind::Goal;
    wire::adventure::GoalData goal;
    uint32_t act = 0;
    uint8_t outcome_state = 0; // 5 success, 6 failure
    uint32_t object_id = 0;
    uint8_t deaths = 0;
};

struct HostJournal {
    wire::Key3 mission;
    uint32_t run_id = 0;
    uint32_t next_seq = 0;
    uint32_t current_act = 0;
    uint8_t outcome = 0; // 0 playing, 5 success, 6 failure
    std::vector<JournalEntry> entries;
    // Per (act, slot) highest credited count recorded in this run (prevents duplicate commits)
    std::map<uint64_t, uint32_t> goal_counts;
    // Per non-creature object_id highest death count recorded
    std::unordered_map<uint32_t, uint8_t> object_deaths;

    static uint64_t goal_key(uint32_t act, uint32_t slot) { return ((uint64_t)act << 32) | slot; }

    void reset()
    {
        mission = {};
        run_id = 0;
        next_seq = 0;
        current_act = 0;
        outcome = 0;
        entries.clear();
        goal_counts.clear();
        object_deaths.clear();
    }

    void start_run(const wire::Key3& m, uint32_t new_run_id, uint32_t initial_act = 0)
    {
        reset();
        mission = m;
        run_id = new_run_id ? new_run_id : 1u;
        current_act = initial_act;
    }

    bool active() const { return run_id != 0; }

    // Records a goal credit; returns pointer to new entry or nullptr if duplicate/already satisfied/sealed.
    const JournalEntry* commit_goal(wire::adventure::GoalData g)
    {
        if (!active() || outcome != 0)
            return nullptr;
        if (g.act > wire::adventure::kMaxAct || g.slot > wire::adventure::kMaxGoalSlot)
            return nullptr;
        uint64_t k = goal_key(g.act, g.slot);
        uint32_t& prev = goal_counts[k];
        if (g.count == 0)
            g.count = prev + 1;
        if (g.count <= prev)
            return nullptr;
        prev = g.count;
        JournalEntry e;
        e.seq = ++next_seq;
        e.kind = EntryKind::Goal;
        e.goal = g;
        entries.push_back(e);
        return &entries.back();
    }

    enum class ActResult { Ignored, Committed, ResetNeeded };

    // Records an act transition. Backwards act means the adventure was replayed (ResetNeeded).
    ActResult commit_act(uint32_t act, const JournalEntry** out_entry = nullptr)
    {
        if (out_entry)
            *out_entry = nullptr;
        if (!active() || act > wire::adventure::kMaxAct)
            return ActResult::Ignored;
        if (act < current_act)
            return ActResult::ResetNeeded;
        if (act == current_act)
            return ActResult::Ignored;
        current_act = act;
        JournalEntry e;
        e.seq = ++next_seq;
        e.kind = EntryKind::Act;
        e.act = act;
        entries.push_back(e);
        if (out_entry)
            *out_entry = &entries.back();
        return ActResult::Committed;
    }

    // Seals the run with host outcome 5 (win) or 6 (loss). Returns new entry or nullptr if already sealed.
    const JournalEntry* commit_outcome(uint8_t state)
    {
        if (!active() || (state != 5 && state != 6) || outcome != 0)
            return nullptr;
        outcome = state;
        JournalEntry e;
        e.seq = ++next_seq;
        e.kind = EntryKind::Outcome;
        e.outcome_state = state;
        entries.push_back(e);
        return &entries.back();
    }

    // Records a monotonic death count for a non-creature scenario object (e.g. building).
    const JournalEntry* commit_death(uint32_t object_id, uint8_t deaths)
    {
        if (!active() || outcome != 0 || object_id == 0 || deaths < 1 || deaths > wire::adventure::kMaxDeaths)
            return nullptr;
        uint8_t& prev = object_deaths[object_id];
        if (deaths <= prev)
            return nullptr;
        prev = deaths;
        JournalEntry e;
        e.seq = ++next_seq;
        e.kind = EntryKind::Death;
        e.object_id = object_id;
        e.deaths = deaths;
        entries.push_back(e);
        return &entries.back();
    }

    std::vector<JournalEntry> entries_after(uint32_t seq) const
    {
        std::vector<JournalEntry> out;
        for (const auto& e : entries)
            if (e.seq > seq)
                out.push_back(e);
        return out;
    }
};

struct GuestJournal {
    wire::Key3 mission;
    uint32_t run_id = 0;
    uint32_t applied_seq = 0;
    uint32_t host_act = 0;
    uint32_t host_state = 0;
    uint8_t outcome_applied = 0;
    std::map<uint32_t, JournalEntry> pending;

    void reset()
    {
        mission = {};
        run_id = 0;
        applied_seq = 0;
        host_act = 0;
        host_state = 0;
        outcome_applied = 0;
        pending.clear();
    }

    bool on_welcome(const wire::Key3& m, uint32_t new_run_id, uint32_t h_act, uint32_t h_state, uint32_t /*h_seq*/)
    {
        if (new_run_id == 0)
            return false;
        bool new_run = (run_id != new_run_id || mission != m);
        if (new_run) {
            reset();
            mission = m;
            run_id = new_run_id;
        }
        host_act = h_act;
        host_state = h_state;
        return new_run;
    }

    bool enqueue(const JournalEntry& e)
    {
        if (run_id == 0 || e.seq == 0 || e.seq <= applied_seq)
            return false;
        if (pending.count(e.seq))
            return false;
        pending[e.seq] = e;
        return true;
    }

    enum class ActionKind : uint8_t {
        None,
        JumpToAct,
        ApplyGoal,
        ApplyDeath,
        ApplyOutcome,
    };

    struct Action {
        ActionKind kind = ActionKind::None;
        uint32_t seq = 0;
        uint32_t target_act = 0;
        JournalEntry entry;
    };

    // Inspects and pops (or requests JumpToAct for) the next ordered entry when the guest is playing (state == 3).
    // Strictly enforces sequence ordering: never skips over missing sequence numbers, and applies Outcome only after
    // all earlier entries have been applied.
    Action next_action(uint32_t local_act, uint32_t local_state)
    {
        if (run_id == 0 || local_state != 3)
            return {};
        // First: if there is an ordered entry ready (seq == applied_seq + 1), process it.
        auto it = pending.find(applied_seq + 1);
        if (it == pending.end()) {
            // If no pending entries, check if late-join welcome act is ahead of local_act (e.g., host jumped with no
            // journal entries yet, or initial act > 0).
            if (pending.empty() && host_act > local_act) {
                Action a;
                a.kind = ActionKind::JumpToAct;
                a.target_act = host_act;
                return a;
            }
            return {};
        }

        const JournalEntry e = it->second;
        switch (e.kind) {
        case EntryKind::Act:
            if (e.act > local_act) {
                // Keep entry in pending until local_act actually reaches e.act
                Action a;
                a.kind = ActionKind::JumpToAct;
                a.seq = e.seq;
                a.target_act = e.act;
                return a;
            }
            // Already at or past e.act: consume it and continue on next call
            applied_seq = e.seq;
            pending.erase(it);
            return next_action(local_act, local_state);

        case EntryKind::Goal:
            if (e.goal.act > local_act) {
                Action a;
                a.kind = ActionKind::JumpToAct;
                a.seq = e.seq;
                a.target_act = e.goal.act;
                return a;
            }
            applied_seq = e.seq;
            pending.erase(it);
            if (e.goal.act < local_act)
                return next_action(local_act, local_state); // past act: already completed
            {
                Action a;
                a.kind = ActionKind::ApplyGoal;
                a.seq = e.seq;
                a.entry = e;
                return a;
            }

        case EntryKind::Death:
            applied_seq = e.seq;
            pending.erase(it);
            {
                Action a;
                a.kind = ActionKind::ApplyDeath;
                a.seq = e.seq;
                a.entry = e;
                return a;
            }

        case EntryKind::Outcome:
            applied_seq = e.seq;
            pending.erase(it);
            if (outcome_applied != 0)
                return {};
            outcome_applied = e.outcome_state;
            {
                Action a;
                a.kind = ActionKind::ApplyOutcome;
                a.seq = e.seq;
                a.entry = e;
                return a;
            }
        }
        return {};
    }
};

// ---------------------------------------------------------------- E-B: Host-Authoritative Actor Pairing

struct LocalCandidate {
    uint32_t local_id = 0; // local object serial (+0x24)
    uint32_t marker_hash = 0;
    int32_t class_id = -1;
    float pos[3] = {};
    bool dead = false;
};

struct ActorPairing {
    std::unordered_map<uint32_t, uint32_t> host_to_local; // host_key -> local_id
    std::unordered_map<uint32_t, uint32_t> local_to_host; // local_id -> host_key
    std::unordered_map<uint32_t, double> unpaired_since;  // local_id -> first time seen unpaired
    double stream_first_rx = -1.0;
    double stream_last_rx = -1.0;

    void reset()
    {
        host_to_local.clear();
        local_to_host.clear();
        unpaired_since.clear();
        stream_first_rx = -1.0;
        stream_last_rx = -1.0;
    }

    void note_stream_rx(double now)
    {
        if (stream_first_rx < 0.0 || (stream_last_rx >= 0.0 && now - stream_last_rx > kStreamTimeoutSec))
            stream_first_rx = now;
        stream_last_rx = now;
    }

    bool stream_active(double now) const
    {
        return stream_last_rx >= 0.0 && (now - stream_last_rx) <= kStreamTimeoutSec;
    }

    bool stream_warmed(double now) const
    {
        return stream_active(now) && stream_first_rx >= 0.0 && (now - stream_first_rx) >= kStreamWarmupSec;
    }

    void unpair_host(uint32_t host_key)
    {
        auto it = host_to_local.find(host_key);
        if (it == host_to_local.end())
            return;
        local_to_host.erase(it->second);
        host_to_local.erase(it);
    }

    void unpair_local(uint32_t local_id)
    {
        auto it = local_to_host.find(local_id);
        if (it == local_to_host.end())
            return;
        host_to_local.erase(it->second);
        local_to_host.erase(it);
    }

    bool is_local_paired(uint32_t local_id) const { return local_to_host.count(local_id) != 0; }

    uint32_t local_for_host(uint32_t host_key) const
    {
        auto it = host_to_local.find(host_key);
        return it != host_to_local.end() ? it->second : 0;
    }

    uint32_t host_for_local(uint32_t local_id) const
    {
        auto it = local_to_host.find(local_id);
        return it != local_to_host.end() ? it->second : 0;
    }

    // Prunes pairings whose local creature is no longer in live_locals.
    void prune_dead_locals(const std::unordered_set<uint32_t>& live_locals)
    {
        for (auto it = local_to_host.begin(); it != local_to_host.end();) {
            if (!live_locals.count(it->first)) {
                host_to_local.erase(it->second);
                it = local_to_host.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = unpaired_since.begin(); it != unpaired_since.end();) {
            if (!live_locals.count(it->first))
                it = unpaired_since.erase(it);
            else
                ++it;
        }
    }

    // Pairs host_key with the nearest unpaired local candidate matching (marker_hash, class_id) within kPairMaxDist (12 m).
    // Returns paired local_id, or 0 if no eligible candidate is within range.
    uint32_t pair(uint32_t host_key, uint32_t marker_hash, int32_t class_id, const float host_pos[3],
                  const std::vector<LocalCandidate>& candidates, float max_dist = kPairMaxDist)
    {
        if (host_key == 0 || class_id < 0)
            return 0;
        if (uint32_t existing = local_for_host(host_key))
            return existing;

        float best_d2 = max_dist * max_dist;
        uint32_t best_id = 0;
        for (const auto& c : candidates) {
            if (c.local_id == 0 || c.dead)
                continue;
            if (c.marker_hash != marker_hash || c.class_id != class_id)
                continue;
            if (local_to_host.count(c.local_id))
                continue;
            float dx = c.pos[0] - host_pos[0];
            float dy = c.pos[1] - host_pos[1];
            float dz = c.pos[2] - host_pos[2];
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 <= best_d2) {
                best_d2 = d2;
                best_id = c.local_id;
            }
        }
        if (best_id != 0) {
            host_to_local[host_key] = best_id;
            local_to_host[best_id] = host_key;
            unpaired_since.erase(best_id);
        }
        return best_id;
    }

    // Updates unpaired timers and returns local_ids that have been unpaired for >= expiry_sec while stream is warmed.
    // When stream stops, clears unpaired timers so nothing is removed after a disconnect/stall.
    std::vector<uint32_t> collect_expired_unpaired(double now, const std::vector<LocalCandidate>& live_candidates,
                                                   double expiry_sec = kUnpairedExpirySec)
    {
        std::vector<uint32_t> expired;
        if (!stream_warmed(now)) {
            if (!stream_active(now))
                unpaired_since.clear();
            return expired;
        }
        std::unordered_set<uint32_t> seen;
        for (const auto& c : live_candidates) {
            if (c.local_id == 0 || c.dead)
                continue;
            seen.insert(c.local_id);
            if (local_to_host.count(c.local_id)) {
                unpaired_since.erase(c.local_id);
                continue;
            }
            auto [it, inserted] = unpaired_since.emplace(c.local_id, now);
            if (!inserted && (now - it->second) >= expiry_sec)
                expired.push_back(c.local_id);
        }
        for (auto it = unpaired_since.begin(); it != unpaired_since.end();) {
            if (!seen.count(it->first))
                it = unpaired_since.erase(it);
            else
                ++it;
        }
        return expired;
    }
};

} // namespace adventure
