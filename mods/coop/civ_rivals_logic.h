// Portable Civilization Rivals mode logic (WP-R5 of 0.8.0, BUG-1549, docs/11-rivals.md 11.3.4, 11.4-11.9).
// No Windows or SporeKit dependencies so Linux ASan/UBSan unit tests cover every rule and codec.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "net/buffer.h"
#include "rivals_logic.h"

namespace civ_rivals_logic {

constexpr int kMaxPlayers = rivals::kMaxPlayers;
constexpr float kCapitalMatchRadius = 35.0f;       // position match radius for a player's capital city
constexpr float kCapitalSwProtectRadius = 120.0f;  // targeted superweapon exclusion radius around any rival capital
constexpr float kNpcRelMin = -10.0f;
constexpr float kNpcRelMax = 10.0f;
constexpr float kNpcRelStep = 2.5f;                // TurnAgainst (-2.5) and SmoothOver (+2.5) shift
constexpr float kNpcAllyThreshold = 3.0f;          // relation score counted as allied NPC nation
constexpr float kStartingWealthFloor = 3000.0f;    // minimum starting wealth for a taken-over NPC nation
constexpr float kMaxGiftAmount = 500000.0f;
constexpr int32_t kCivMoneyPerInfluenceUnit = 10;  // 50 base price -> 500 Sporebucks
constexpr int32_t kTradeInfluenceGain = 5;
constexpr int32_t kCaptureInfluenceGain = 5;

enum class GiftKind : uint8_t {
    Money = 1,
    Spice = 2,
};

enum class CombatGate : uint8_t {
    Pass = 0,  // not a cross-player interaction (NPC or self)
    Allow = 1, // players at War: damage/attack allowed
    Block = 2, // Neutral/Allied players or capital superweapon protection: block
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline float dist3(const Vec3& a, const Vec3& b)
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---------------- R2: Base distance nation selection ----------------
struct BaseCandidate {
    uint32_t pol_id = 0;
    Vec3 cap_pos{};
    int city_count = 0;
};

// Picks a free NPC civilization matching `dist` (Near / Middle / Far) from `host_cap`.
// Returns 0 if no free candidate is available.
inline uint32_t select_base_nation(const Vec3& host_cap, const std::vector<BaseCandidate>& candidates,
                                   rivals::BaseDistance dist, const std::vector<uint32_t>& already_taken = {})
{
    struct Ranked {
        uint32_t pol_id;
        float d;
    };
    std::vector<Ranked> pool;
    for (const auto& c : candidates) {
        if (!c.pol_id || c.city_count <= 0)
            continue;
        if (std::find(already_taken.begin(), already_taken.end(), c.pol_id) != already_taken.end())
            continue;
        pool.push_back({c.pol_id, dist3(host_cap, c.cap_pos)});
    }
    if (pool.empty())
        return 0;
    std::sort(pool.begin(), pool.end(), [](const Ranked& a, const Ranked& b) {
        if (std::fabs(a.d - b.d) > 1e-3f)
            return a.d < b.d;
        return a.pol_id < b.pol_id;
    });
    size_t idx = 0;
    if (dist == rivals::BaseDistance::Near)
        idx = 0;
    else if (dist == rivals::BaseDistance::Far)
        idx = pool.size() - 1;
    else
        idx = pool.size() / 2;
    return pool[idx].pol_id;
}

// ---------------- R1 / R2: Nation assignment table ----------------
struct PlayerNation {
    uint8_t player_id = 0;
    uint32_t pol_id = 0;
    Vec3 cap_pos{};
    float wealth = 0.0f;
    bool assigned = false;
};

inline bool is_global_superweapon(int sw_type)
{
    // Spore Civ global superweapons (docs/re/civ.md section F): 2, 5, 8
    return sw_type == 2 || sw_type == 5 || sw_type == 8;
}

inline bool is_capital_city_pos(const Vec3& city_pos, const std::array<PlayerNation, kMaxPlayers>& nations,
                                uint8_t* owner_out = nullptr)
{
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!nations[i].assigned)
            continue;
        if (dist3(city_pos, nations[i].cap_pos) <= kCapitalMatchRadius) {
            if (owner_out)
                *owner_out = nations[i].player_id;
            return true;
        }
    }
    return false;
}

// ---------------- R7: Combat, capture & superweapon protection ----------------
// Cross-player damage gate for vehicles and buildings (dmg_type 0xC / 0xD are superweapon damage types).
inline CombatGate pvp_damage_gate(uint8_t attacker_pid, uint8_t victim_pid, bool at_war, bool victim_is_capital,
                                  int dmg_type = 0)
{
    if (victim_is_capital && (dmg_type == 0xC || dmg_type == 0xD))
        return CombatGate::Block; // superweapon damage never hurts any player's capital
    if (attacker_pid >= kMaxPlayers || victim_pid >= kMaxPlayers || attacker_pid == victim_pid)
        return CombatGate::Pass;
    return at_war ? CombatGate::Allow : CombatGate::Block;
}

// City capture / conversion / buy gate:
// - Any player's capital is strictly inviolable (cannot be captured, converted, or bought by anyone).
// - Ordinary rival cities can only be captured/converted/bought while at War.
inline bool city_capture_blocked(uint8_t attacker_pid, uint8_t defender_pid, bool at_war, bool is_capital)
{
    if (is_capital)
        return true;
    if (attacker_pid < kMaxPlayers && defender_pid < kMaxPlayers && attacker_pid != defender_pid)
        return !at_war;
    return false;
}

// Superweapon targeting gate:
// - Global superweapons (2, 5, 8) hit every foreign city including rival capitals, so block when any rival has a capital.
// - Targeted superweapons targeting within `kCapitalSwProtectRadius` of any rival player's capital are blocked.
inline bool superweapon_blocked_by_capital(uint8_t attacker_pid, int sw_type, bool has_target, const Vec3& target,
                                           const std::array<PlayerNation, kMaxPlayers>& nations,
                                           uint8_t* protected_owner = nullptr)
{
    if (is_global_superweapon(sw_type)) {
        for (int i = 0; i < kMaxPlayers; i++) {
            if (nations[i].assigned && nations[i].player_id != attacker_pid) {
                if (protected_owner)
                    *protected_owner = nations[i].player_id;
                return true;
            }
        }
        return false;
    }
    if (!has_target)
        return false;
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!nations[i].assigned || nations[i].player_id == attacker_pid)
            continue;
        if (dist3(target, nations[i].cap_pos) <= kCapitalSwProtectRadius) {
            if (protected_owner)
                *protected_owner = nations[i].player_id;
            return true;
        }
    }
    return false;
}

// ---------------- R4: Per-player NPC nation relations & influence ----------------
inline float clamp_rel(float v)
{
    if (!std::isfinite(v))
        return 0.0f;
    return std::max(kNpcRelMin, std::min(kNpcRelMax, v));
}

inline int rel_category(float score)
{
    // Matches Spore's 0..4 diplomacy categories (0 Hostile .. 4 Allied)
    if (score <= -5.0f)
        return 0;
    if (score <= -1.5f)
        return 1;
    if (score < 1.5f)
        return 2;
    if (score < 5.0f)
        return 3;
    return 4;
}

struct NpcRelationTable {
    // npc_pol -> per-player relation score [-10, +10]
    std::map<uint32_t, std::array<float, kMaxPlayers>> scores;

    void clear() { scores.clear(); }

    void ensure_npc(uint32_t npc_pol, float initial = 0.0f)
    {
        if (!npc_pol || scores.count(npc_pol))
            return;
        float v = clamp_rel(initial);
        scores[npc_pol] = {v, v, v, v};
    }

    void remove_npc(uint32_t npc_pol) { scores.erase(npc_pol); }

    bool has_npc(uint32_t npc_pol) const { return scores.count(npc_pol) != 0; }

    float get(uint32_t npc_pol, uint8_t pid) const
    {
        if (pid >= kMaxPlayers)
            return 0.0f;
        auto it = scores.find(npc_pol);
        return it == scores.end() ? 0.0f : it->second[pid];
    }

    bool set(uint32_t npc_pol, uint8_t pid, float value)
    {
        if (!npc_pol || pid >= kMaxPlayers)
            return false;
        auto& row = scores[npc_pol];
        row[pid] = clamp_rel(value);
        return true;
    }

    bool apply_turn_against(uint32_t npc_pol, uint8_t rival_pid, float step = kNpcRelStep, float* new_val = nullptr)
    {
        if (rival_pid >= kMaxPlayers)
            return false;
        auto it = scores.find(npc_pol);
        if (it == scores.end())
            return false;
        it->second[rival_pid] = clamp_rel(it->second[rival_pid] - std::fabs(step));
        if (new_val)
            *new_val = it->second[rival_pid];
        return true;
    }

    bool apply_smooth_over(uint32_t npc_pol, uint8_t actor_pid, float step = kNpcRelStep, float* new_val = nullptr)
    {
        if (actor_pid >= kMaxPlayers)
            return false;
        auto it = scores.find(npc_pol);
        if (it == scores.end())
            return false;
        it->second[actor_pid] = clamp_rel(it->second[actor_pid] + std::fabs(step));
        if (new_val)
            *new_val = it->second[actor_pid];
        return true;
    }

    uint16_t allied_count(uint8_t pid) const
    {
        if (pid >= kMaxPlayers)
            return 0;
        uint16_t n = 0;
        for (const auto& [pol, arr] : scores) {
            (void)pol;
            if (arr[pid] >= kNpcAllyThreshold)
                n++;
        }
        return n;
    }
};

inline float influence_buy_civ_cost(int32_t base_price)
{
    if (base_price <= 0)
        return 0.0f;
    return (float)(base_price * kCivMoneyPerInfluenceUnit);
}

// ---------------- Alliance money/spice gift validation ----------------
inline bool can_send_gift(uint8_t from, uint8_t to, bool is_allied, GiftKind kind, float amount, float sender_wealth)
{
    if (from >= kMaxPlayers || to >= kMaxPlayers || from == to)
        return false;
    if (!is_allied)
        return false;
    if (kind != GiftKind::Money && kind != GiftKind::Spice)
        return false;
    if (!std::isfinite(amount) || amount <= 0.0f || amount > kMaxGiftAmount)
        return false;
    if (!std::isfinite(sender_wealth) || sender_wealth < amount)
        return false;
    return true;
}

// ---------------- 11.3.4 / 11.5: Stage end (>50% NPC cities) ----------------
struct CityCounts {
    int total_cities[kMaxPlayers] = {};     // all cities owned by player's nation (capital + conquered)
    int non_cap_cities[kMaxPlayers] = {};   // conquered/subjugated non-capital cities owned by player
    float wealth[kMaxPlayers] = {};         // tie-breaker when no free NPC city remains
    bool present[kMaxPlayers] = {};
    int free_npc_cities = 0;                // cities currently owned by non-player NPC nations
    int total_contestable = 0;              // free_npc_cities + sum(non_cap_cities)
};

struct StageWinEval {
    bool has_winner = false;
    uint8_t winner = 255;
    bool all_npc_taken = false;
    bool qualified[kMaxPlayers] = {}; // every player that meets the >50% condition
};

inline int cities_needed_to_win(int total_contestable)
{
    return total_contestable > 0 ? (total_contestable / 2) + 1 : 1;
}

inline StageWinEval evaluate_stage_win(const CityCounts& c)
{
    StageWinEval out;
    if (c.total_contestable <= 0)
        return out;
    int need = cities_needed_to_win(c.total_contestable);
    int best_non_cap = -1;
    int best_total = -1;
    float best_wealth = -1.0f;
    uint8_t best_pid = 255;

    for (int i = 0; i < kMaxPlayers; i++) {
        if (!c.present[i])
            continue;
        if (c.non_cap_cities[i] >= need) {
            out.qualified[i] = true;
            if (!out.has_winner || c.non_cap_cities[i] > c.non_cap_cities[out.winner]) {
                out.has_winner = true;
                out.winner = (uint8_t)i;
            }
        }
        bool better = false;
        if (best_pid == 255)
            better = true;
        else if (c.non_cap_cities[i] != best_non_cap)
            better = c.non_cap_cities[i] > best_non_cap;
        else if (c.total_cities[i] != best_total)
            better = c.total_cities[i] > best_total;
        else if (c.wealth[i] > best_wealth + 0.5f)
            better = true;
        if (better) {
            best_pid = (uint8_t)i;
            best_non_cap = c.non_cap_cities[i];
            best_total = c.total_cities[i];
            best_wealth = c.wealth[i];
        }
    }

    if (!out.has_winner && c.free_npc_cities == 0 && best_pid != 255) {
        out.has_winner = true;
        out.winner = best_pid;
        out.all_npc_taken = true;
        out.qualified[best_pid] = true;
    }
    return out;
}

// ---------------- 11.6: Civ Draft items (vehicle & building designs) ----------------
struct DraftDesignSpec {
    uint32_t design_id;
    uint8_t kind; // 1 = vehicle, 2 = building
    const char* label;
};

inline const std::vector<DraftDesignSpec>& civ_draft_specs()
{
    static const std::vector<DraftDesignSpec> kSpecs = {
        {0x7D433FADu, 1, "Military Land Vehicle"},
        {0x9AD7D4AAu, 1, "Religious Land Vehicle"},
        {0xF670AA43u, 1, "Economic Land Vehicle"},
        {0x441CD3E6u, 1, "Military Sea Vehicle"},
        {0x449C040Fu, 1, "Religious Sea Vehicle"},
        {0x1A4E0708u, 1, "Economic Sea Vehicle"},
        {0x8F963DCBu, 1, "Military Air Vehicle"},
        {0x1F2A25B6u, 1, "Religious Air Vehicle"},
        {0x2A5147A9u, 1, "Economic Air Vehicle"},
        {0x4E3F7777u, 2, "House Building"},
        {0x47C10953u, 2, "Factory Building"},
        {0x72C49181u, 2, "Entertainment Building"},
        {0x99E92F05u, 2, "City Hall Building"},
    };
    return kSpecs;
}

inline std::vector<rivals::Draft::Item> build_civ_draft_items(const std::vector<uint32_t>& available_design_ids)
{
    std::vector<rivals::Draft::Item> items;
    for (const auto& sp : civ_draft_specs()) {
        if (!available_design_ids.empty() &&
            std::find(available_design_ids.begin(), available_design_ids.end(), sp.design_id) ==
                available_design_ids.end())
            continue;
        items.push_back({sp.design_id, sp.kind, sp.label});
        if ((int)items.size() >= rivals::kMaxDraftItems)
            break;
    }
    if (items.empty()) {
        // Fallback: core starter set so a Civ draft always has copyable items
        items.push_back({0x7D433FADu, 1, "Military Land Vehicle"});
        items.push_back({0x4E3F7777u, 2, "House Building"});
        items.push_back({0x47C10953u, 2, "Factory Building"});
        items.push_back({0x72C49181u, 2, "Entertainment Building"});
    }
    return items;
}

// ---------------- Wire payloads inside MSG_RIVALS_FACTION (Stage::Civ) ----------------
// SK_ASSIGN (1): u8 count, count * {u8 pid, u32 pol_id, f32 cap_x, f32 cap_y, f32 cap_z, f32 wealth}
inline void write_assign(net::Writer& w, const std::array<PlayerNation, kMaxPlayers>& nations)
{
    uint8_t n = 0;
    for (int i = 0; i < kMaxPlayers; i++)
        if (nations[i].assigned)
            n++;
    w.u8(n);
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!nations[i].assigned)
            continue;
        w.u8(nations[i].player_id);
        w.u32(nations[i].pol_id);
        w.f32(nations[i].cap_pos.x);
        w.f32(nations[i].cap_pos.y);
        w.f32(nations[i].cap_pos.z);
        w.f32(nations[i].wealth);
    }
}

inline bool read_assign(net::Reader& r, std::array<PlayerNation, kMaxPlayers>& out)
{
    uint8_t n = r.u8();
    if (!r.ok() || n > kMaxPlayers)
        return false;
    std::array<PlayerNation, kMaxPlayers> tmp{};
    for (int i = 0; i < kMaxPlayers; i++)
        tmp[i].player_id = (uint8_t)i;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t pid = r.u8();
        uint32_t pol = r.u32();
        float x = r.f32(), y = r.f32(), z = r.f32(), wealth = r.f32();
        if (!r.ok() || pid >= kMaxPlayers || !pol || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
            !std::isfinite(wealth))
            return false;
        tmp[pid] = {pid, pol, {x, y, z}, wealth, true};
    }
    out = tmp;
    return true;
}

// SK_NPC_RELS (2): u8 target_pid, u8 count, count * {u32 npc_pol, f32 score}
inline void write_npc_rels(net::Writer& w, uint8_t target_pid, const NpcRelationTable& tbl)
{
    w.u8(target_pid);
    uint8_t n = (uint8_t)std::min<size_t>(tbl.scores.size(), 64);
    w.u8(n);
    uint8_t written = 0;
    for (const auto& [pol, arr] : tbl.scores) {
        if (written >= n)
            break;
        w.u32(pol);
        w.f32(target_pid < kMaxPlayers ? arr[target_pid] : 0.0f);
        written++;
    }
}

inline bool read_npc_rels(net::Reader& r, uint8_t& target_pid, std::vector<std::pair<uint32_t, float>>& rows)
{
    target_pid = r.u8();
    uint8_t n = r.u8();
    if (!r.ok() || target_pid >= kMaxPlayers || n > 64)
        return false;
    rows.clear();
    rows.reserve(n);
    for (uint8_t i = 0; i < n; i++) {
        uint32_t pol = r.u32();
        float score = r.f32();
        if (!r.ok() || !pol || !std::isfinite(score))
            return false;
        rows.push_back({pol, clamp_rel(score)});
    }
    return true;
}

// SK_GIFT (3): u8 target_pid, u8 gift_kind, f32 amount
struct GiftMsg {
    uint8_t target_pid = 0;
    GiftKind kind = GiftKind::Money;
    float amount = 0.0f;
};

inline void write_gift(net::Writer& w, const GiftMsg& g)
{
    w.u8(g.target_pid);
    w.u8((uint8_t)g.kind);
    w.f32(g.amount);
}

inline bool read_gift(net::Reader& r, GiftMsg& out)
{
    uint8_t t = r.u8();
    uint8_t k = r.u8();
    float amt = r.f32();
    if (!r.ok() || t >= kMaxPlayers || (k != (uint8_t)GiftKind::Money && k != (uint8_t)GiftKind::Spice) ||
        !std::isfinite(amt) || amt <= 0.0f || amt > kMaxGiftAmount)
        return false;
    out = {t, (GiftKind)k, amt};
    return true;
}

// SK_DRAFT_APPLY (7): u8 target_pid, u32 design_id, u32 key[3]
struct DraftApplyMsg {
    uint8_t target_pid = 0;
    uint32_t design_id = 0;
    uint32_t key[3] = {0, 0, 0};
};

inline void write_draft_apply(net::Writer& w, const DraftApplyMsg& m)
{
    w.u8(m.target_pid);
    w.u32(m.design_id);
    w.u32(m.key[0]);
    w.u32(m.key[1]);
    w.u32(m.key[2]);
}

inline bool read_draft_apply(net::Reader& r, DraftApplyMsg& out)
{
    uint8_t t = r.u8();
    uint32_t did = r.u32();
    uint32_t k0 = r.u32(), k1 = r.u32(), k2 = r.u32();
    if (!r.ok() || t >= kMaxPlayers || !did)
        return false;
    out = {t, did, {k0, k1, k2}};
    return true;
}

} // namespace civ_rivals_logic
