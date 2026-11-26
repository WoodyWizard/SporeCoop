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
constexpr float kNpcHostileThreshold = -1.5f;      // relation score counted as hostile/annoyed NPC nation
constexpr float kNativeSyncEps = 0.05f;            // minimum native cRelationshipManager delta treated as a real change
constexpr float kDiplomacyImproveMinDelta = 1.0f;  // minimum positive native delta to earn real-diplomacy influence
constexpr float kStartingWealthFloor = 3000.0f;    // minimum starting wealth for a taken-over NPC nation
constexpr float kMaxGiftAmount = 500000.0f;
constexpr int32_t kCivMoneyPerInfluenceUnit = 10;  // 50 base price -> 500 Sporebucks
constexpr int32_t kCivDraftPayoutScale = 10;       // Tribe->Civ draft pick: 100 -> 1000, 150 -> 1500, 200 -> 2000
constexpr int32_t kTradeInfluenceGain = 5;
constexpr int32_t kCaptureInfluenceGain = 5;
constexpr int32_t kDiplomacyImproveInfluenceGain = 5;
constexpr int32_t kDiplomacyAllyInfluenceGain = 10;
constexpr uint16_t kMinSpaceHandoffCities = 1;
constexpr uint16_t kMaxSpaceHandoffCities = 3;     // docs/11-rivals.md 11.3.5: Space homeworld city cap (1..3)

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
    int building_slots = 0; // cCity building layout slot count (+0x43C, stride 0x18)
    bool is_small = false;  // cCity +0x519 (mbSmallCity): true = small city (fewer plots / lower cap)
};

// Picks a free NPC civilization matching `dist` (Near / Middle / Far) from `host_cap`.
// When `prefer_large` is true (BUG-1581), prefers non-small / larger-slot cities so guests are not stuck
// in a 4-tank small coastal city when full-sized cities exist. Returns 0 if no free candidate is available.
inline uint32_t select_base_nation(const Vec3& host_cap, const std::vector<BaseCandidate>& candidates,
                                   rivals::BaseDistance dist, const std::vector<uint32_t>& already_taken = {},
                                   bool prefer_large = true)
{
    struct Ranked {
        uint32_t pol_id;
        float d;
        int building_slots;
        bool is_small;
    };
    std::vector<Ranked> pool;
    bool has_large = false;
    int max_slots = 0;
    for (const auto& c : candidates) {
        if (!c.pol_id || c.city_count <= 0)
            continue;
        if (std::find(already_taken.begin(), already_taken.end(), c.pol_id) != already_taken.end())
            continue;
        pool.push_back({c.pol_id, dist3(host_cap, c.cap_pos), c.building_slots, c.is_small});
        if (!c.is_small)
            has_large = true;
        if (c.building_slots > max_slots)
            max_slots = c.building_slots;
    }
    if (pool.empty())
        return 0;
    if (prefer_large) {
        if (has_large) {
            std::vector<Ranked> large_only;
            for (const auto& r : pool)
                if (!r.is_small)
                    large_only.push_back(r);
            if (!large_only.empty())
                pool = std::move(large_only);
        }
        // Recompute max_slots within surviving pool; if slot counts vary meaningfully, prefer top tier.
        int pool_max = 0;
        int pool_min = 0x7fffffff;
        for (const auto& r : pool) {
            if (r.building_slots > pool_max)
                pool_max = r.building_slots;
            if (r.building_slots < pool_min)
                pool_min = r.building_slots;
        }
        if (pool_max > 0 && pool_max - pool_min >= 3) {
            std::vector<Ranked> top_tier;
            for (const auto& r : pool)
                if (r.building_slots >= pool_max - 2)
                    top_tier.push_back(r);
            if (!top_tier.empty())
                pool = std::move(top_tier);
        }
    }
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

// ---------------- BUG-1581: Persisted player_key -> nation memory across disconnect/reconnect ----------------
struct SavedNation {
    uint32_t pol_id = 0;
    Vec3 cap_pos{};
    int8_t cap_specialty = -1;
};

struct NationMemory {
    std::map<std::string, SavedNation> by_key;

    void clear() { by_key.clear(); }

    void remember(const std::string& key, uint32_t pol_id, const Vec3& cap_pos, int8_t cap_specialty)
    {
        if (key.empty() || !pol_id)
            return;
        by_key[key] = {pol_id, cap_pos, cap_specialty};
    }

    bool has(const std::string& key) const { return !key.empty() && by_key.count(key) != 0; }

    // Finds the player's original nation among live non-host candidates (first by exact pol_id,
    // then by capital position within kCapitalMatchRadius if pol_id changed after reload).
    uint32_t find_reclaim(const std::string& key, const std::vector<BaseCandidate>& live_civs,
                          const std::vector<uint32_t>& active_taken) const
    {
        if (key.empty())
            return 0;
        auto it = by_key.find(key);
        if (it == by_key.end() || !it->second.pol_id)
            return 0;
        const SavedNation& sv = it->second;
        auto is_taken = [&](uint32_t pol) {
            return std::find(active_taken.begin(), active_taken.end(), pol) != active_taken.end();
        };
        for (const auto& c : live_civs) {
            if (c.pol_id == sv.pol_id && c.city_count > 0 && !is_taken(c.pol_id))
                return c.pol_id;
        }
        // Fallback: position match (e.g. after a save/reload)
        uint32_t best_pol = 0;
        float best_d = kCapitalMatchRadius;
        for (const auto& c : live_civs) {
            if (!c.pol_id || c.city_count <= 0 || is_taken(c.pol_id))
                continue;
            float d = dist3(c.cap_pos, sv.cap_pos);
            if (d <= best_d) {
                best_d = d;
                best_pol = c.pol_id;
            }
        }
        return best_pol;
    }

    // True if `pol_id` is remembered for a DIFFERENT player key (prevents a newcomer stealing a reconnecting peer's nation).
    bool is_reserved_for_other(uint32_t pol_id, const std::string& requester_key) const
    {
        if (!pol_id)
            return false;
        for (const auto& [k, sv] : by_key)
            if (sv.pol_id == pol_id && k != requester_key)
                return true;
        return false;
    }

    // True if ANY remembered player owns `pol_id` (keeps NPC AI paused during brief disconnects).
    bool is_reserved_pol(uint32_t pol_id) const
    {
        if (!pol_id)
            return false;
        for (const auto& [k, sv] : by_key)
            if (sv.pol_id == pol_id)
                return true;
        return false;
    }

    void adopt(const std::string& from, const std::string& to)
    {
        if (from.empty() || to.empty() || from == to)
            return;
        auto it = by_key.find(from);
        if (it == by_key.end())
            return;
        by_key[to] = it->second;
        by_key.erase(it);
    }
};

// ---------------- BUG-1581: Superweapon tools (+0x470) & timers (+0x490) borrow validation ----------------
constexpr size_t kSwToolsMinBytes = 12u * 4u;    // 12 intrusive_ptr<cSpaceToolData>
constexpr size_t kSwTimersMinBytes = 12u * 32u;  // 12 cGameTime timers (stride 0x20)

inline bool sw_vec_valid(uint32_t begin, uint32_t end, size_t min_bytes)
{
    return begin != 0u && end >= begin && (size_t)(end - begin) >= min_bytes;
}

inline bool sw_borrow_needed(bool same_civ, uint32_t dst_begin, uint32_t dst_end, uint32_t src_begin, uint32_t src_end,
                             size_t min_bytes)
{
    return !same_civ && !sw_vec_valid(dst_begin, dst_end, min_bytes) && sw_vec_valid(src_begin, src_end, min_bytes);
}

// ---------------- BUG-1581: Capital siege softlock guard & vanilla diplomatic gift validation ----------------
constexpr uint32_t kEventCivDiplomaticGift = 0x0526E4FBu; // verified at SporeApp.exe 0x00DCA96C
constexpr float kCapitalHallMinHpRatio = 0.25f;
constexpr int kCapitalMinProtectedBuildings = 2;

enum class CapitalDamageAction : uint8_t {
    Allow = 0,         // normal damage proceeds
    ClampHp = 1,       // player attacker on inviolable capital City Hall: clamp damage so Hall never falls below floor
    BlockAndRepel = 2, // NPC attacker trying to ruin protected capital City Hall or last buildings: block and repel NPC
};

inline CapitalDamageAction capital_siege_gate(bool is_capital, bool is_hall, bool attacker_is_npc, int alive_buildings,
                                              float cur_hp, float max_hp, float dmg)
{
    if (!is_capital)
        return CapitalDamageAction::Allow;
    if (is_hall) {
        if (attacker_is_npc)
            return CapitalDamageAction::BlockAndRepel;
        float floor_hp = (std::isfinite(max_hp) && max_hp > 0.0f) ? (max_hp * kCapitalHallMinHpRatio) : 0.0f;
        if (std::isfinite(cur_hp) && std::isfinite(dmg) && cur_hp - dmg < floor_hp)
            return CapitalDamageAction::ClampHp;
        return CapitalDamageAction::Allow;
    }
    if (attacker_is_npc && alive_buildings <= kCapitalMinProtectedBuildings)
        return CapitalDamageAction::BlockAndRepel;
    return CapitalDamageAction::Allow;
}

inline float clamped_hall_damage(float cur_hp, float max_hp, float dmg)
{
    if (!std::isfinite(cur_hp) || !std::isfinite(max_hp) || !std::isfinite(dmg) || max_hp <= 0.0f || dmg <= 0.0f)
        return 0.0f;
    float floor_hp = max_hp * kCapitalHallMinHpRatio;
    if (cur_hp <= floor_hp)
        return 0.0f;
    return std::min(dmg, cur_hp - floor_hp);
}

inline bool can_apply_diplo_gift(uint8_t from, uint8_t to, float amount, float sender_wealth)
{
    if (from >= kMaxPlayers || to >= kMaxPlayers || from == to)
        return false;
    if (!std::isfinite(amount) || amount <= 0.0f || amount > kMaxGiftAmount)
        return false;
    if (!std::isfinite(sender_wealth) || sender_wealth < amount)
        return false;
    return true;
}

// ---------------- BUG-1581: Atomic guest city planner sync payload (buildings + turrets) ----------------
constexpr uint8_t kCmdPlannerSync = 9;
constexpr uint8_t kCmdSellVehicle = 11;
constexpr uint8_t kSkDiploGift = 17; // MSG_RIVALS_FACTION subkind: vanilla diplomacy Sporebucks gift
constexpr float kDefaultTurretCost = 500.0f;

struct PlannerBuildingItem {
    uint32_t noun = 0;
    Vec3 pos{};
    float price = 0.0f;
};

struct PlannerMoveItem {
    uint32_t noun = 0;
    Vec3 from{};
    Vec3 to{};
};

struct PlannerSyncMsg {
    uint8_t ci = 0;
    std::vector<PlannerBuildingItem> removed;
    std::vector<PlannerMoveItem> moved;
    std::vector<PlannerBuildingItem> added;
    int8_t turret_delta = 0; // net turrets added (>0) or removed (<0)
    std::vector<Vec3> removed_turrets;
    float net_spend = 0.0f;  // positive = player spent Sporebucks, negative = net refund
};

inline bool finite_vec3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

inline void write_planner_sync(net::Writer& w, const PlannerSyncMsg& m)
{
    w.u8(kCmdPlannerSync);
    w.u8(m.ci);
    uint8_t nr = (uint8_t)std::min<size_t>(m.removed.size(), 32);
    w.u8(nr);
    for (uint8_t i = 0; i < nr; i++) {
        w.u32(m.removed[i].noun);
        w.f32(m.removed[i].pos.x);
        w.f32(m.removed[i].pos.y);
        w.f32(m.removed[i].pos.z);
        w.f32(m.removed[i].price);
    }
    uint8_t nm = (uint8_t)std::min<size_t>(m.moved.size(), 32);
    w.u8(nm);
    for (uint8_t i = 0; i < nm; i++) {
        w.u32(m.moved[i].noun);
        w.f32(m.moved[i].from.x);
        w.f32(m.moved[i].from.y);
        w.f32(m.moved[i].from.z);
        w.f32(m.moved[i].to.x);
        w.f32(m.moved[i].to.y);
        w.f32(m.moved[i].to.z);
    }
    uint8_t na = (uint8_t)std::min<size_t>(m.added.size(), 32);
    w.u8(na);
    for (uint8_t i = 0; i < na; i++) {
        w.u32(m.added[i].noun);
        w.f32(m.added[i].pos.x);
        w.f32(m.added[i].pos.y);
        w.f32(m.added[i].pos.z);
        w.f32(m.added[i].price);
    }
    w.u8((uint8_t)m.turret_delta);
    uint8_t nt = (uint8_t)std::min<size_t>(m.removed_turrets.size(), 32);
    w.u8(nt);
    for (uint8_t i = 0; i < nt; i++) {
        w.f32(m.removed_turrets[i].x);
        w.f32(m.removed_turrets[i].y);
        w.f32(m.removed_turrets[i].z);
    }
    w.f32(m.net_spend);
}

inline bool read_planner_sync(net::Reader& r, PlannerSyncMsg& out)
{
    out = PlannerSyncMsg{};
    out.ci = r.u8();
    uint8_t nr = r.u8();
    if (!r.ok() || nr > 32)
        return false;
    out.removed.resize(nr);
    for (uint8_t i = 0; i < nr; i++) {
        out.removed[i].noun = r.u32();
        out.removed[i].pos = {r.f32(), r.f32(), r.f32()};
        out.removed[i].price = r.f32();
        if (!r.ok() || !finite_vec3(out.removed[i].pos) || !std::isfinite(out.removed[i].price))
            return false;
    }
    uint8_t nm = r.u8();
    if (!r.ok() || nm > 32)
        return false;
    out.moved.resize(nm);
    for (uint8_t i = 0; i < nm; i++) {
        out.moved[i].noun = r.u32();
        out.moved[i].from = {r.f32(), r.f32(), r.f32()};
        out.moved[i].to = {r.f32(), r.f32(), r.f32()};
        if (!r.ok() || !finite_vec3(out.moved[i].from) || !finite_vec3(out.moved[i].to))
            return false;
    }
    uint8_t na = r.u8();
    if (!r.ok() || na > 32)
        return false;
    out.added.resize(na);
    for (uint8_t i = 0; i < na; i++) {
        out.added[i].noun = r.u32();
        out.added[i].pos = {r.f32(), r.f32(), r.f32()};
        out.added[i].price = r.f32();
        if (!r.ok() || !finite_vec3(out.added[i].pos) || !std::isfinite(out.added[i].price) ||
            out.added[i].price < 0.0f || out.added[i].price > 100000.0f)
            return false;
    }
    out.turret_delta = (int8_t)r.u8();
    uint8_t nt = r.u8();
    if (!r.ok() || nt > 32 || out.turret_delta < -32 || out.turret_delta > 32)
        return false;
    out.removed_turrets.resize(nt);
    for (uint8_t i = 0; i < nt; i++) {
        out.removed_turrets[i] = {r.f32(), r.f32(), r.f32()};
        if (!r.ok() || !finite_vec3(out.removed_turrets[i]))
            return false;
    }
    out.net_spend = r.f32();
    if (!r.ok() || !std::isfinite(out.net_spend) || out.net_spend < -200000.0f || out.net_spend > 200000.0f)
        return false;
    return true;
}

inline float clamp_planner_net_spend(float reported_spend, float max_charge, float max_refund)
{
    if (!std::isfinite(reported_spend))
        return 0.0f;
    float hi = (std::isfinite(max_charge) && max_charge > 0.0f) ? max_charge : 0.0f;
    float lo = (std::isfinite(max_refund) && max_refund > 0.0f) ? -max_refund : 0.0f;
    return std::max(lo, std::min(hi, reported_spend));
}

inline std::string format_rival_takeover_notice(const std::string& player_name, const char* color_name,
                                                int native_spec, bool reclaimed)
{
    if (reclaimed)
        return player_name + " reclaimed their rival nation";
    const char* spec = (native_spec == 0) ? "Military" : (native_spec == 1) ? "Religious" : (native_spec == 2) ? "Economic" : "";
    bool has_col = color_name && color_name[0] != '\0';
    bool has_spec = spec[0] != '\0';
    if (has_col && has_spec)
        return player_name + " took command of the " + color_name + " " + spec + " nation";
    if (has_spec)
        return player_name + " took command of a " + spec + " rival nation";
    if (has_col)
        return player_name + " took command of the " + color_name + " rival nation";
    return player_name + " took command of a rival nation";
}

// ---------------- R1 / R2: Nation assignment table ----------------
constexpr int kCitySpecMilitary = 0;
constexpr int kCitySpecReligious = 1;
constexpr int kCitySpecEconomic = 2;

inline bool valid_native_specialty(int spec) { return spec >= kCitySpecMilitary && spec <= kCitySpecEconomic; }

// BUG-1563: returns true when a taken-over capital's native specialty differs from the desired specialty
// derived from the player's Tribe consequence card.
inline bool needs_specialty_conversion(int current_native_spec, int desired_native_spec)
{
    return valid_native_specialty(desired_native_spec) && current_native_spec != desired_native_spec;
}

// BUG-1563: land vehicle editor asset id matching a native cCity specialty (0=Mil, 1=Rel, 2=Eco; 0=unknown).
inline uint32_t land_vehicle_editor_for_specialty(int native_spec)
{
    switch (native_spec) {
    case kCitySpecMilitary: return 0x7D433FADu;
    case kCitySpecReligious: return 0x9AD7D4AAu;
    case kCitySpecEconomic: return 0xF670AA43u;
    default: return 0u;
    }
}

// BUG-1563: Spore GOG 2024 GetSuperweaponCost (0x00BF6DF0) cost table for types 0..11.
// Returns -1.0f for invalid types.
inline float superweapon_cost(int sw_type, int foreign_cities)
{
    switch (sw_type) {
    case 0:
    case 6:
    case 11:
        return 4000.0f;
    case 1:
    case 4:
    case 7:
        return 16000.0f;
    case 3:
    case 9:
    case 10:
        return 2000.0f;
    case 2:
    case 5:
    case 8:
        return 12000.0f * (float)std::max(1, foreign_cities);
    default:
        return -1.0f;
    }
}

inline bool can_afford_superweapon(float wealth, float cost)
{
    return std::isfinite(wealth) && std::isfinite(cost) && cost >= 0.0f && wealth >= cost;
}

// BUG-1563: authoritative post-fire wealth for a guest cCivilization (mIsPlayer == 0 on host).
// Clamps mWealth to max(0, before_wealth - cost) and clears NPC budgets so native SpendMoney / budget refill
// can neither double-spend nor refill from domestic/expansion budgets.
inline float finalize_superweapon_wealth(float before_wealth, float cost, float& domestic_budget,
                                         float& expansion_budget)
{
    float base = (std::isfinite(before_wealth) && before_wealth > 0.0f) ? before_wealth : 0.0f;
    float c = (std::isfinite(cost) && cost > 0.0f) ? cost : 0.0f;
    domestic_budget = 0.0f;
    expansion_budget = 0.0f;
    return std::max(0.0f, base - c);
}

struct PlayerNation {
    uint8_t player_id = 0;
    uint32_t pol_id = 0;
    Vec3 cap_pos{};
    float wealth = 0.0f;
    bool assigned = false;
    int8_t cap_specialty = -1; // native cCity specialty (+0x540): 0=Mil, 1=Rel, 2=Eco, -1=unknown
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

enum class NativeSyncAction : uint8_t {
    None = 0,       // native unchanged and Rivals not dirty: do not overwrite native
    ReadInit = 1,   // first observation of (npc, pid): initialized from native
    ReadBack = 2,   // native changed since last_native: read back into authoritative table
    PushNative = 3, // Rivals modified score (dirty): caller must write target_native to cRelationshipManager
};

struct NativeSyncResult {
    NativeSyncAction action = NativeSyncAction::None;
    float target_native = 0.0f;
    float delta = 0.0f;
    int32_t influence_award = 0;
    bool newly_allied = false;
};

struct NpcRelationTable {
    // npc_pol -> per-player relation score [-10, +10]
    std::map<uint32_t, std::array<float, kMaxPlayers>> scores;
    // Per-(npc_pol, pid) native cRelationshipManager tracking (BUG-1555): avoids overwriting real in-game diplomacy.
    std::map<uint32_t, std::array<float, kMaxPlayers>> last_native;
    std::map<uint32_t, std::array<bool, kMaxPlayers>> has_native;
    std::map<uint32_t, std::array<bool, kMaxPlayers>> dirty_native;

    void clear()
    {
        scores.clear();
        last_native.clear();
        has_native.clear();
        dirty_native.clear();
    }

    void ensure_npc(uint32_t npc_pol, float initial = 0.0f)
    {
        if (!npc_pol || scores.count(npc_pol))
            return;
        float v = clamp_rel(initial);
        scores[npc_pol] = {v, v, v, v};
        last_native[npc_pol] = {v, v, v, v};
        has_native[npc_pol] = {false, false, false, false};
        dirty_native[npc_pol] = {false, false, false, false};
    }

    void remove_npc(uint32_t npc_pol)
    {
        scores.erase(npc_pol);
        last_native.erase(npc_pol);
        has_native.erase(npc_pol);
        dirty_native.erase(npc_pol);
    }

    bool has_npc(uint32_t npc_pol) const { return scores.count(npc_pol) != 0; }

    float get(uint32_t npc_pol, uint8_t pid) const
    {
        if (pid >= kMaxPlayers)
            return 0.0f;
        auto it = scores.find(npc_pol);
        return it == scores.end() ? 0.0f : it->second[pid];
    }

    bool is_dirty_native(uint32_t npc_pol, uint8_t pid) const
    {
        if (pid >= kMaxPlayers)
            return false;
        auto it = dirty_native.find(npc_pol);
        return it != dirty_native.end() && it->second[pid];
    }

    bool set(uint32_t npc_pol, uint8_t pid, float value, bool mark_dirty = false)
    {
        if (!npc_pol || pid >= kMaxPlayers)
            return false;
        if (!scores.count(npc_pol))
            ensure_npc(npc_pol, 0.0f);
        float v = clamp_rel(value);
        scores[npc_pol][pid] = v;
        if (mark_dirty) {
            dirty_native[npc_pol][pid] = true;
        } else {
            last_native[npc_pol][pid] = v;
            has_native[npc_pol][pid] = true;
            dirty_native[npc_pol][pid] = false;
        }
        return true;
    }

    // Records the actual native cRelationshipManager value right after pushing a Rivals shift to native.
    void confirm_native_push(uint32_t npc_pol, uint8_t pid, float observed_native)
    {
        if (!npc_pol || pid >= kMaxPlayers || !scores.count(npc_pol))
            return;
        last_native[npc_pol][pid] = clamp_rel(observed_native);
        has_native[npc_pol][pid] = true;
        dirty_native[npc_pol][pid] = false;
    }

    // Bidirectional 1.0 s sync for one (npc_pol, pid) pair:
    //  - If Rivals modified the score (dirty_native), requests PushNative so caller writes target_native.
    //  - Otherwise if native changed since last_native, reads back the real in-game diplomacy change and
    //    awards influence for positive diplomacy improvements / new alliances.
    //  - Otherwise returns None (never overwrites native).
    NativeSyncResult sync_native_slot(uint32_t npc_pol, uint8_t pid, float native_val)
    {
        NativeSyncResult out;
        if (!npc_pol || pid >= kMaxPlayers)
            return out;
        float nv = clamp_rel(native_val);
        if (!scores.count(npc_pol))
            ensure_npc(npc_pol, nv);
        auto& sc = scores[npc_pol];
        auto& ln = last_native[npc_pol];
        auto& hn = has_native[npc_pol];
        auto& dn = dirty_native[npc_pol];

        if (dn[pid]) {
            out.action = NativeSyncAction::PushNative;
            out.target_native = sc[pid];
            return out;
        }
        if (!hn[pid]) {
            sc[pid] = nv;
            ln[pid] = nv;
            hn[pid] = true;
            out.action = NativeSyncAction::ReadInit;
            return out;
        }
        float delta = nv - ln[pid];
        if (std::fabs(delta) >= kNativeSyncEps) {
            float prev = sc[pid];
            sc[pid] = nv;
            ln[pid] = nv;
            out.action = NativeSyncAction::ReadBack;
            out.delta = delta;
            if (prev < kNpcAllyThreshold && nv >= kNpcAllyThreshold) {
                out.newly_allied = true;
                out.influence_award = kDiplomacyAllyInfluenceGain;
            } else if (delta >= kDiplomacyImproveMinDelta) {
                out.influence_award = kDiplomacyImproveInfluenceGain;
            }
        }
        return out;
    }

    bool apply_turn_against(uint32_t npc_pol, uint8_t rival_pid, float step = kNpcRelStep, float* new_val = nullptr)
    {
        if (rival_pid >= kMaxPlayers)
            return false;
        auto it = scores.find(npc_pol);
        if (it == scores.end())
            return false;
        it->second[rival_pid] = clamp_rel(it->second[rival_pid] - std::fabs(step));
        dirty_native[npc_pol][rival_pid] = true;
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
        dirty_native[npc_pol][actor_pid] = true;
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

inline float civ_draft_payout_money(int32_t raw_payout)
{
    if (raw_payout <= 0)
        return 0.0f;
    int64_t scaled = (int64_t)raw_payout * kCivDraftPayoutScale;
    if (scaled > 500000LL)
        scaled = 500000LL;
    return (float)scaled;
}

inline uint16_t clamp_space_handoff_cities(int count)
{
    if (count < (int)kMinSpaceHandoffCities)
        return kMinSpaceHandoffCities;
    if (count > (int)kMaxSpaceHandoffCities)
        return kMaxSpaceHandoffCities;
    return (uint16_t)count;
}

// Direct wealth deduction for a guest cCivilization whose mIsPlayer == 0 on host (BUG-1555):
// native cCivilization::SpendMoney refills NPC domestic/expansion budgets instead of deducting mWealth.
inline bool deduct_direct_wealth(float& wealth, float& domestic_budget, float& expansion_budget, float cost)
{
    if (!std::isfinite(cost) || cost < 0.0f)
        return false;
    if (!std::isfinite(wealth) || wealth < cost)
        return false;
    wealth = std::max(0.0f, wealth - cost);
    domestic_budget = 0.0f;
    expansion_budget = 0.0f;
    return true;
}

inline void spend_direct_wealth(float& wealth, float& domestic_budget, float& expansion_budget, float cost)
{
    if (!std::isfinite(wealth) || wealth < 0.0f)
        wealth = 0.0f;
    if (std::isfinite(cost) && cost > 0.0f)
        wealth = std::max(0.0f, wealth - cost);
    domestic_budget = 0.0f;
    expansion_budget = 0.0f;
}

// ---------------- F9 Influence UI: readable NPC nation labels & action flags (BUG-1555) ----------------
inline const char* classify_civ_color(float r, float g, float b)
{
    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b))
        return "";
    float mx = std::max({r, g, b});
    float mn = std::min({r, g, b});
    float delta = mx - mn;
    if (mx < 0.08f)
        return "Dark";
    if (delta < 0.12f) {
        if (mx > 0.78f)
            return "White";
        return "Gray";
    }
    float hue = 0.0f;
    if (mx == r)
        hue = 60.0f * std::fmod(((g - b) / delta) + 6.0f, 6.0f);
    else if (mx == g)
        hue = 60.0f * (((b - r) / delta) + 2.0f);
    else
        hue = 60.0f * (((r - g) / delta) + 4.0f);

    if (hue < 18.0f || hue >= 340.0f)
        return (mx < 0.55f && mn < 0.2f) ? "Crimson" : "Red";
    if (hue < 45.0f)
        return (mx < 0.55f) ? "Brown" : "Orange";
    if (hue < 72.0f)
        return "Yellow";
    if (hue < 160.0f)
        return "Green";
    if (hue < 200.0f)
        return "Teal";
    if (hue < 255.0f)
        return "Blue";
    if (hue < 300.0f)
        return "Purple";
    return "Pink";
}

inline const char* civ_specialty_name(int specialty)
{
    switch (specialty) {
    case 0: return "Military";
    case 1: return "Religious";
    case 2: return "Economic";
    default: return "";
    }
}

struct CivNpcSnapshot {
    uint32_t pol_id = 0;
    int specialty = -1; // 0=Military, 1=Religious, 2=Economic, -1=unknown
    int cities = 0;
    bool has_color = false;
    float color_r = 0.0f;
    float color_g = 0.0f;
    float color_b = 0.0f;
    float viewer_rel = 0.0f;
    bool any_rival_turnable = false;
};

struct CivNpcUiItem {
    uint32_t npc_id = 0;
    std::string label;
    bool allied = false;
    bool hostile = false;
    bool can_turn = false;
    bool can_protect = false;
    bool can_smooth = false;
};

inline std::string format_civ_npc_label(const CivNpcSnapshot& s, int ordinal)
{
    const char* col = s.has_color ? classify_civ_color(s.color_r, s.color_g, s.color_b) : "";
    const char* spec = civ_specialty_name(s.specialty);
    std::string name;
    if (col[0] && spec[0])
        name = std::string(col) + " " + spec;
    else if (col[0])
        name = std::string(col) + " Nation";
    else if (spec[0])
        name = std::string(spec) + " Nation #" + std::to_string(ordinal);
    else
        name = "Nation #" + std::to_string(ordinal);

    char tail[64];
    float rel = clamp_rel(s.viewer_rel);
    if (s.cities > 0) {
        snprintf(tail, sizeof tail, " (%d %s, %+.1f)", s.cities, s.cities == 1 ? "city" : "cities", rel);
    } else {
        snprintf(tail, sizeof tail, " (%+.1f)", rel);
    }
    return name + tail;
}

inline std::vector<CivNpcUiItem> build_civ_npc_ui_list(std::vector<CivNpcSnapshot> snaps, bool has_rival)
{
    std::sort(snaps.begin(), snaps.end(), [](const CivNpcSnapshot& a, const CivNpcSnapshot& b) {
        if (std::fabs(a.viewer_rel - b.viewer_rel) > 1e-3f)
            return a.viewer_rel > b.viewer_rel;
        return a.pol_id < b.pol_id;
    });
    std::vector<CivNpcUiItem> out;
    out.reserve(snaps.size());
    int ord = 1;
    for (const auto& s : snaps) {
        if (!s.pol_id)
            continue;
        float rel = clamp_rel(s.viewer_rel);
        CivNpcUiItem item;
        item.npc_id = s.pol_id;
        item.label = format_civ_npc_label(s, ord++);
        item.allied = rel >= kNpcAllyThreshold;
        item.hostile = rel <= kNpcHostileThreshold;
        item.can_turn = has_rival && s.any_rival_turnable;
        item.can_protect = item.allied;
        item.can_smooth = rel < (kNpcRelMax - 0.05f);
        out.push_back(std::move(item));
    }
    return out;
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
// SK_ASSIGN (1): u8 count, count * {u8 pid, u32 pol_id, f32 cap_x, f32 cap_y, f32 cap_z, f32 wealth},
// followed by optional count * u8 cap_specialty (0=Mil, 1=Rel, 2=Eco, 0xFF=unknown; BUG-1563).
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
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!nations[i].assigned)
            continue;
        w.u8(valid_native_specialty(nations[i].cap_specialty) ? (uint8_t)nations[i].cap_specialty : 0xFFu);
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
    uint8_t order[kMaxPlayers] = {};
    for (uint8_t i = 0; i < n; i++) {
        uint8_t pid = r.u8();
        uint32_t pol = r.u32();
        float x = r.f32(), y = r.f32(), z = r.f32(), wealth = r.f32();
        if (!r.ok() || pid >= kMaxPlayers || !pol || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
            !std::isfinite(wealth))
            return false;
        tmp[pid] = {pid, pol, {x, y, z}, wealth, true, -1};
        order[i] = pid;
    }
    if (r.left() >= n) {
        for (uint8_t i = 0; i < n; i++) {
            uint8_t sp = r.u8();
            if (!r.ok())
                return false;
            tmp[order[i]].cap_specialty = sp <= (uint8_t)kCitySpecEconomic ? (int8_t)sp : (int8_t)-1;
        }
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

// kSkDiploGift (17): u8 target_pid, f32 amount (BUG-1581: vanilla diplomacy gift relay)
struct DiploGiftMsg {
    uint8_t target_pid = 0;
    float amount = 0.0f;
};

inline void write_diplo_gift(net::Writer& w, const DiploGiftMsg& m)
{
    w.u8(m.target_pid);
    w.f32(m.amount);
}

inline bool read_diplo_gift(net::Reader& r, DiploGiftMsg& out)
{
    uint8_t t = r.u8();
    float amt = r.f32();
    if (!r.ok() || t >= kMaxPlayers || !std::isfinite(amt) || amt <= 0.0f || amt > kMaxGiftAmount)
        return false;
    out = {t, amt};
    return true;
}

// Persisted store codec for NationMemory (world record "civ.rivals_nations" v1, BUG-1581)
inline void write_saved_nations(net::Writer& w, const NationMemory& mem)
{
    uint8_t n = (uint8_t)std::min<size_t>(mem.by_key.size(), 32);
    w.u8(n);
    uint8_t written = 0;
    for (const auto& [k, sv] : mem.by_key) {
        if (written >= n)
            break;
        w.str8(k);
        w.u32(sv.pol_id);
        w.f32(sv.cap_pos.x);
        w.f32(sv.cap_pos.y);
        w.f32(sv.cap_pos.z);
        w.u8(valid_native_specialty(sv.cap_specialty) ? (uint8_t)sv.cap_specialty : 0xFFu);
        written++;
    }
}

inline bool read_saved_nations(net::Reader& r, NationMemory& out)
{
    uint8_t n = r.u8();
    if (!r.ok() || n > 32)
        return false;
    NationMemory tmp;
    for (uint8_t i = 0; i < n; i++) {
        std::string k = r.str8();
        uint32_t pol = r.u32();
        Vec3 pos{r.f32(), r.f32(), r.f32()};
        uint8_t sp = r.u8();
        if (!r.ok() || k.empty() || !pol || !finite_vec3(pos))
            return false;
        int8_t sp8 = sp <= (uint8_t)kCitySpecEconomic ? (int8_t)sp : (int8_t)-1;
        tmp.by_key[k] = {pol, pos, sp8};
    }
    out = std::move(tmp);
    return true;
}

} // namespace civ_rivals_logic
