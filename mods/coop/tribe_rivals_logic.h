// Portable Tribe Rivals logic (WP-R4 of 0.8.0, BUG-1548, docs/11-rivals.md 11.3.3, 11.4-11.9).
// No Windows or SporeKit dependencies so native Linux ASan/UBSan unit tests cover all rules and wire payloads.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "net/buffer.h"
#include "rivals_logic.h"

namespace tribe_rivals {

constexpr uint8_t kNoOwner = 0xFE;
constexpr int32_t kInfluenceMusicGain = 15;
constexpr int32_t kInfluenceGiftGain = 10;
constexpr int32_t kDefaultRaidFoodSteal = 15;
constexpr float kMainHutFloorRatio = 0.10f;
constexpr float kMainHutMinHp = 1.0f;
constexpr int kMinToolKind = 1;
constexpr int kMaxToolKind = 9;
constexpr uint8_t kMaxWireTribes = 32;

// ---------------- R2: Base distance site picker & BUG-1580 persisted assignment ----------------
struct TribeSite {
    uint32_t pol = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool is_host = false;
    bool conquered = false;
    bool taken = false;
    bool has_live_hut = true;
    float hut_hp = 100.0f;
};

inline bool is_eligible_rival_tribe(const TribeSite& s)
{
    return !s.is_host && !s.conquered && !s.taken && s.has_live_hut && std::isfinite(s.hut_hp) && s.hut_hp > 0.0f;
}

inline float dist3(float ax, float ay, float az, float bx, float by, float bz)
{
    float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Matches a persisted guest tribe assignment by political ID first, then by nearest eligible site within max_dist.
// Strictly rejects conquered, dead-hut, host, or already-taken tribes (BUG-1580).
inline int match_persisted_tribe(const std::vector<TribeSite>& sites, uint32_t stored_pol, float x, float y, float z,
                                 float max_dist = 45.0f)
{
    if (stored_pol != 0) {
        for (int i = 0; i < (int)sites.size(); i++) {
            if (sites[i].pol == stored_pol && is_eligible_rival_tribe(sites[i]))
                return i;
        }
    }
    int best = -1;
    float bd = max_dist;
    for (int i = 0; i < (int)sites.size(); i++) {
        if (!is_eligible_rival_tribe(sites[i]))
            continue;
        float d = dist3(sites[i].x, sites[i].y, sites[i].z, x, y, z);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

// Picks an available NPC cTribe site at the requested base distance from the host's village.
// Returns index into `sites`, or -1 if no eligible NPC tribe exists.
inline int pick_rival_tribe(const std::vector<TribeSite>& sites, float host_x, float host_y, float host_z,
                            rivals::BaseDistance dist)
{
    struct Cand {
        int idx;
        float d;
        uint32_t pol;
    };
    std::vector<Cand> cands;
    cands.reserve(sites.size());
    for (int i = 0; i < (int)sites.size(); i++) {
        const auto& s = sites[i];
        if (!is_eligible_rival_tribe(s))
            continue;
        cands.push_back({i, dist3(s.x, s.y, s.z, host_x, host_y, host_z), s.pol});
    }
    if (cands.empty())
        return -1;
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (std::fabs(a.d - b.d) > 1e-3f)
            return a.d < b.d;
        return a.pol < b.pol;
    });
    if (dist == rivals::BaseDistance::Near)
        return cands.front().idx;
    if (dist == rivals::BaseDistance::Far)
        return cands.back().idx;
    return cands[cands.size() / 2].idx;
}

// ---------------- R4: Per-player NPC tribe relations ----------------
enum class NpcRel : uint8_t {
    Hostile = 0,
    Annoyed = 1,
    Neutral = 2,
    Friendly = 3,
    Allied = 4,
    Conquered = 5,
};

inline const char* npc_rel_name(NpcRel r)
{
    switch (r) {
    case NpcRel::Hostile: return "Hostile";
    case NpcRel::Annoyed: return "Annoyed";
    case NpcRel::Neutral: return "Neutral";
    case NpcRel::Friendly: return "Friendly";
    case NpcRel::Allied: return "Allied";
    case NpcRel::Conquered: return "Conquered";
    default: return "Neutral";
    }
}

inline bool is_subjugated(NpcRel r) { return r == NpcRel::Allied || r == NpcRel::Conquered; }

// Maps discrete NpcRel (0..4) to a representative game relationship float using the live
// cRelationshipManager thresholds (mgr+0x10..+0x1C: t0 <= hostile, t1 <= annoyed, t2 <= neutral, t3 <= friendly).
inline float rel_target_float(NpcRel r, float t0, float t1, float t2, float t3)
{
    float span = std::max(0.5f, (t3 - t0) * 0.25f);
    switch (r) {
    case NpcRel::Hostile: return t0 - span;
    case NpcRel::Annoyed: return (t0 + t1) * 0.5f;
    case NpcRel::Neutral: return (t1 + t2) * 0.5f;
    case NpcRel::Friendly: return (t2 + t3) * 0.5f;
    case NpcRel::Allied: return t3 + span;
    case NpcRel::Conquered: return t0 - span;
    default: return (t1 + t2) * 0.5f;
    }
}

inline NpcRel rel_from_game_level(int level, bool conquered)
{
    if (conquered)
        return NpcRel::Conquered;
    if (level <= 0)
        return NpcRel::Hostile;
    if (level == 1)
        return NpcRel::Annoyed;
    if (level == 2)
        return NpcRel::Neutral;
    if (level == 3)
        return NpcRel::Friendly;
    return NpcRel::Allied;
}

struct NpcRelations {
    // player_id (0..3) -> (npc_pol -> NpcRel)
    std::map<uint32_t, NpcRel> per_player[rivals::kMaxPlayers];

    void clear()
    {
        for (int i = 0; i < rivals::kMaxPlayers; i++)
            per_player[i].clear();
    }

    void forget_npc(uint32_t npc_pol)
    {
        for (int i = 0; i < rivals::kMaxPlayers; i++)
            per_player[i].erase(npc_pol);
    }

    NpcRel get(uint8_t pid, uint32_t npc_pol) const
    {
        if (!rivals::valid_player(pid))
            return NpcRel::Neutral;
        auto it = per_player[pid].find(npc_pol);
        return it == per_player[pid].end() ? NpcRel::Neutral : it->second;
    }

    void set(uint8_t pid, uint32_t npc_pol, NpcRel r)
    {
        if (!rivals::valid_player(pid) || npc_pol == 0)
            return;
        per_player[pid][npc_pol] = r;
    }

    // Influence: TurnAgainst -> moves rival's relation 1 step toward Hostile.
    // Returns false if already Hostile or Conquered (refund influence).
    bool step_down(uint8_t rival_pid, uint32_t npc_pol)
    {
        if (!rivals::valid_player(rival_pid) || npc_pol == 0)
            return false;
        NpcRel cur = get(rival_pid, npc_pol);
        if (cur == NpcRel::Hostile || cur == NpcRel::Conquered)
            return false;
        set(rival_pid, npc_pol, (NpcRel)((uint8_t)cur - 1));
        return true;
    }

    // Influence: SmoothOver -> moves actor's relation 1 step toward Allied.
    // Returns false if already Allied or Conquered (refund influence).
    bool step_up(uint8_t actor_pid, uint32_t npc_pol)
    {
        if (!rivals::valid_player(actor_pid) || npc_pol == 0)
            return false;
        NpcRel cur = get(actor_pid, npc_pol);
        if (cur == NpcRel::Allied || cur == NpcRel::Conquered)
            return false;
        set(actor_pid, npc_pol, (NpcRel)((uint8_t)cur + 1));
        return true;
    }

    void mark_conquered(uint8_t conqueror_pid, uint32_t npc_pol)
    {
        if (!rivals::valid_player(conqueror_pid) || npc_pol == 0)
            return;
        set(conqueror_pid, npc_pol, NpcRel::Conquered);
    }
};

// ---------------- 11.3.3 & 11.5: Stage end (>50% NPC tribes) ----------------
struct SubjugationEval {
    uint8_t subjugated[rivals::kMaxPlayers] = {};
    uint8_t allied[rivals::kMaxPlayers] = {};
    uint8_t conquered[rivals::kMaxPlayers] = {};
    int total_npc = 0;
    int free_npc = 0;
    int win_needed = 1;
    uint8_t winner = 255; // 255 = no winner yet
};

inline SubjugationEval evaluate_stage_end(const std::vector<uint32_t>& npc_pols,
                                          const bool present[rivals::kMaxPlayers], const NpcRelations& rels)
{
    SubjugationEval ev;
    ev.total_npc = (int)npc_pols.size();
    if (ev.total_npc <= 0)
        return ev;
    ev.win_needed = ev.total_npc / 2 + 1;
    ev.free_npc = 0;
    for (uint32_t npc : npc_pols) {
        bool any_sub = false;
        for (int p = 0; p < rivals::kMaxPlayers; p++) {
            if (!present[p])
                continue;
            NpcRel r = rels.get((uint8_t)p, npc);
            if (r == NpcRel::Allied) {
                ev.allied[p]++;
                ev.subjugated[p]++;
                any_sub = true;
            } else if (r == NpcRel::Conquered) {
                ev.conquered[p]++;
                ev.subjugated[p]++;
                any_sub = true;
            }
        }
        if (!any_sub)
            ev.free_npc++;
    }
    // First check strictly >50% subjugated
    uint8_t best_p = 255;
    int best_cnt = 0;
    for (int p = 0; p < rivals::kMaxPlayers; p++) {
        if (!present[p])
            continue;
        if (ev.subjugated[p] >= ev.win_needed && ev.subjugated[p] > best_cnt) {
            best_cnt = ev.subjugated[p];
            best_p = (uint8_t)p;
        }
    }
    if (best_p != 255) {
        ev.winner = best_p;
        return ev;
    }
    // Tie-breaker when no free NPC tribe remains: player with most subjugated wins
    if (ev.free_npc == 0) {
        for (int p = 0; p < rivals::kMaxPlayers; p++) {
            if (!present[p])
                continue;
            if (ev.subjugated[p] > best_cnt) {
                best_cnt = ev.subjugated[p];
                best_p = (uint8_t)p;
            }
        }
        if (best_cnt > 0)
            ev.winner = best_p;
    }
    return ev;
}

// ---------------- R7: War PvP, Main Village Inviolability & BUG-1571 Wipe Recovery / Immunity ----------------
constexpr double kDefaultWipeRespawnDelay = 15.0;      // s: respawn delay after full tribe wipe
constexpr double kDefaultWipeImmunityDuration = 300.0; // s: 5-minute PvP shield after wipe / respawn
constexpr int32_t kWipeRecoveryMinFood = 15;           // food floor on respawn so babies can be bought right away
constexpr int32_t kRaidFoodFloor = 10;                 // war raids leave >= 10 food so defender cannot softlock
constexpr int kWipeRespawnAdults = 2;                  // chieftain + 1 villager on wipe recovery

struct PlayerWipeState {
    bool wiping = false;
    double wipe_at = 0.0;
    double immune_until = 0.0;
    uint32_t wipes_count = 0;
};

struct WipeRecoveryDecision {
    bool started_wipe = false;
    bool do_respawn = false;
    bool cancelled_wipe = false;
    bool immunity_changed = false;
    bool immune_now = false;
    double immune_remaining = 0.0;
};

inline bool is_player_immune(const PlayerWipeState& st, double now)
{
    return st.immune_until > 0.0 && now < st.immune_until;
}

inline double player_immune_remaining(const PlayerWipeState& st, double now)
{
    return is_player_immune(st, now) ? (st.immune_until - now) : 0.0;
}

// Clear a player's own PvP shield early if they intentionally attack a rival player while shielded.
inline bool break_immunity_on_attack(PlayerWipeState& st, double now)
{
    if (!is_player_immune(st, now))
        return false;
    st.immune_until = 0.0;
    return true;
}

// Pure host tick decision for a single player's tribe wipe recovery & 5-minute PvP immunity (BUG-1571).
inline WipeRecoveryDecision step_wipe_recovery(PlayerWipeState& st, bool fix_on, bool rivals_active, bool tribe_assigned,
                                               int live_members, double now,
                                               double respawn_delay = kDefaultWipeRespawnDelay,
                                               double immune_duration = kDefaultWipeImmunityDuration)
{
    WipeRecoveryDecision d;
    if (!fix_on || !rivals_active || !tribe_assigned)
        return d;
    bool was_immune = is_player_immune(st, now);
    if (st.immune_until > 0.0 && !was_immune) {
        st.immune_until = 0.0;
        d.immunity_changed = true;
    }
    if (live_members <= 0) {
        if (!st.wiping) {
            st.wiping = true;
            st.wipe_at = now;
            st.wipes_count++;
            if (immune_duration > 0.0) {
                double want = now + immune_duration;
                if (want > st.immune_until) {
                    st.immune_until = want;
                    d.immunity_changed = true;
                }
            }
            d.started_wipe = true;
        } else if (now - st.wipe_at >= std::max(0.0, respawn_delay)) {
            st.wiping = false;
            if (immune_duration > 0.0) {
                double want = now + immune_duration;
                if (want > st.immune_until) {
                    st.immune_until = want;
                    d.immunity_changed = true;
                }
            }
            d.do_respawn = true;
        }
    } else if (st.wiping) {
        // Player recovered on their own (e.g., bought a baby or knocked-out chieftain stood up)
        st.wiping = false;
        d.cancelled_wipe = true;
    }
    d.immune_now = is_player_immune(st, now);
    d.immune_remaining = player_immune_remaining(st, now);
    return d;
}

inline int32_t raid_steal_amount(int32_t avail_food, int32_t want_amount, bool vic_immune, bool wipe_fix)
{
    if (vic_immune || want_amount <= 0 || avail_food <= 0)
        return 0;
    int32_t stealable = wipe_fix ? std::max(0, avail_food - kRaidFoodFloor) : avail_food;
    return std::min(want_amount, stealable);
}

enum class DamageDecision : uint8_t {
    Passthrough = 0, // not rivals, or NPC target, or self
    Block = 1,       // player-vs-player while Neutral, Allied, or victim has wipe PvP immunity
    AllowPvp = 2,    // player-vs-player at War (member or extra tool hut)
    ClampMainHut = 3 // main village hut of any player tribe: clamp HP above floor, never conquer
};

inline DamageDecision decide_damage(bool rivals_active, uint8_t att_owner, uint8_t vic_owner, bool at_war,
                                    bool is_main_hut, bool vic_immune = false)
{
    if (!rivals_active)
        return DamageDecision::Passthrough;
    if (!rivals::valid_player(vic_owner))
        return DamageDecision::Passthrough;
    if (rivals::valid_player(att_owner)) {
        if (att_owner == vic_owner)
            return DamageDecision::Passthrough;
        if (!at_war || vic_immune)
            return DamageDecision::Block;
        return is_main_hut ? DamageDecision::ClampMainHut : DamageDecision::AllowPvp;
    }
    // Non-player attacker (NPC tribe / wild animal) hitting a player's main hut in Rivals:
    // still inviolable so nobody is knocked out of the game.
    return is_main_hut ? DamageDecision::ClampMainHut : DamageDecision::Passthrough;
}

inline float clamp_main_hut_damage(float dmg, float cur_hp, float max_hp, float floor_ratio = kMainHutFloorRatio)
{
    if (dmg <= 0.0f)
        return 0.0f;
    float floor_hp = std::max(kMainHutMinHp, max_hp > 0.0f ? max_hp * floor_ratio : kMainHutMinHp);
    if (cur_hp <= floor_hp)
        return 0.0f;
    if (cur_hp - dmg < floor_hp)
        return cur_hp - floor_hp;
    return dmg;
}

inline bool can_conquer_tribe(bool rivals_active, uint8_t tribe_owner)
{
    if (rivals_active && rivals::valid_player(tribe_owner))
        return false; // R7: a player's main village cannot be conquered
    return true;
}

// ---------------- BUG-1554 & BUG-1571: Host order routing (War PvP, Immunity vs Together friendly fire) ----------------
enum class OrderDecision : uint8_t {
    AllowNative = 0,     // pass through to cCreatureCitizen::DoAction
    RunFriendlyFire = 1, // Together mode: delegate to ff_try_order for same-tribe cross-player hits
    BlockHostile = 2,    // Tribe Rivals: hostile order against a rival player while not at War or victim is immune
};

// BUG-1573: Clamp a freshly taken-over NPC tribe's starting food to match the host's food
// (vanilla NPC villages spawn with 100-150 food, giving guests an unfair 130+ food head start).
inline int32_t starting_rival_food(int32_t npc_food, int32_t host_food, bool fix_on)
{
    if (!fix_on)
        return npc_food < 25 ? 25 : npc_food;
    int32_t want = host_food > 0 ? std::max<int32_t>(15, host_food) : 25;
    return want;
}

inline bool is_hostile_citizen_action(int action)
{
    // CitizenAction 7 = attack citizen/creature, 8 = steal/raid food (0x21), 15 = raid hut/tool/village
    return action == 7 || action == 8 || action == 15;
}

inline OrderDecision decide_host_order(bool rivals_active, int action, uint8_t sender_pid, uint8_t target_owner_pid,
                                       bool at_war, bool vic_immune = false)
{
    if (!rivals_active)
        return OrderDecision::RunFriendlyFire;
    if (is_hostile_citizen_action(action) && rivals::valid_player(sender_pid) &&
        rivals::valid_player(target_owner_pid) && sender_pid != target_owner_pid) {
        return (at_war && !vic_immune) ? OrderDecision::AllowNative : OrderDecision::BlockHostile;
    }
    return OrderDecision::AllowNative;
}

// ---------------- BUG-1554: Guest tool/hut purchase routing & food cost ----------------
inline int32_t tool_buy_cost(int tool_type, bool replacing)
{
    if (replacing)
        return 0; // moving an existing tool hut is free
    switch (tool_type) {
    case 1: return 15; // Stone Axe Hut
    case 2: return 20; // Throwing Spears Hut
    case 3: return 25; // Torch Hut
    case 4: return 15; // Wooden Horn Hut
    case 5: return 20; // Maracas Hut
    case 6: return 25; // Didgeridoo Hut
    case 7: return 15; // Fishing Spear Hut
    case 8: return 15; // Gathering Cane Hut
    case 9: return 20; // Healing Staff Hut
    case 10: return 0; // Fire Pit
    default: return -1;
    }
}

struct ToolBuyPlan {
    bool ok = false;
    uint8_t target_owner = 0;
    int32_t food_cost = 0;
    const char* err = "";
};

inline ToolBuyPlan plan_tool_buy(bool rivals_active, uint8_t from_pid, int tool_type, float dist_from_target_hut,
                                 bool replacing, int32_t target_tribe_food, bool target_tribe_found)
{
    if (tool_type < kMinToolKind || tool_type > 10)
        return {false, 0, 0, "invalid tool type"};
    if (!target_tribe_found)
        return {false, 0, 0, "target tribe not found"};
    if (!std::isfinite(dist_from_target_hut) || dist_from_target_hut > 150.0f)
        return {false, 0, 0, "tool too far from target tribe"};
    if (!rivals_active)
        return {true, 0, 0, ""}; // Together mode: host player_tribe(), food synced via shared ledger
    if (!rivals::valid_player(from_pid))
        return {false, 0, 0, "invalid sender"};
    int32_t cost = (from_pid == 0) ? 0 : tool_buy_cost(tool_type, replacing);
    if (cost < 0)
        return {false, from_pid, 0, "invalid tool cost"};
    if (cost > 0 && target_tribe_food < cost)
        return {false, from_pid, cost, "not enough food"};
    return {true, from_pid, cost, ""};
}

// ---------------- BUG-1554: wsync isolation policy ----------------
struct WsyncPolicy {
    bool apply_tribe_level = true;
    bool apply_tribe_goal = true;
    bool push_guest_goal_target = true;
    bool include_host_tribe_food = false;
    bool apply_creature_nest = true;
};

inline WsyncPolicy wsync_policy(bool tribe_rivals_active, bool creature_rivals_active)
{
    WsyncPolicy p;
    p.apply_tribe_level = !tribe_rivals_active;
    p.apply_tribe_goal = !tribe_rivals_active;
    p.push_guest_goal_target = !tribe_rivals_active;
    p.include_host_tribe_food = tribe_rivals_active;
    p.apply_creature_nest = !creature_rivals_active;
    return p;
}

inline bool should_sync_tribe_food(bool tribe_rivals_active, bool is_local_player_tribe,
                                   bool is_guest_own_rivals_tribe)
{
    if (!tribe_rivals_active)
        return !is_local_player_tribe;
    return !is_guest_own_rivals_tribe;
}

// ---------------- BUG-1554 & BUG-1580: PvP native diplomacy & Creature -> Tribe draft payout ----------------
// BUG-1580: Map PvP Ally to NpcRel::Friendly (level 3), NEVER NpcRel::Allied (level 4):
// TribeToTribeMission_Update (0xC2F750) completes an NPC ally mission as soon as GetRelationshipLevel == 4,
// which granted a totem piece and ended Tribe stage early when two players allied!
inline NpcRel pvp_npc_rel_for(rivals::Relation r)
{
    if (r == rivals::Relation::War)
        return NpcRel::Hostile;
    if (r == rivals::Relation::Ally)
        return NpcRel::Friendly;
    return NpcRel::Neutral;
}

inline bool should_block_rival_mission(bool rivals_active, uint8_t tribe_owner)
{
    return rivals_active && rivals::valid_player(tribe_owner);
}

// ---------------- BUG-1580: Pop cap, o_Form growth, species sync, buy influence, sell tool, KO state ----------------
constexpr int kMaxTribePopHardCap = 12;

// Vanilla Tribe hut population cap by upgrade tier (cTribe +0x38): 0 -> 6, 1 -> 9, >=2 -> 12 (hard-capped at 12).
inline int tribe_pop_cap(uint32_t hut_tier)
{
    if (hut_tier == 0)
        return 6;
    if (hut_tier == 1)
        return 9;
    return kMaxTribePopHardCap;
}

struct BabySpawnCheck {
    bool allowed = false;
    int pop_cap = 6;
    const char* reason = "";
};

inline BabySpawnCheck can_spawn_tribe_baby(int cur_members, uint32_t hut_tier, int32_t food, int32_t cost = 10)
{
    int cap = tribe_pop_cap(hut_tier);
    if (cur_members >= cap)
        return {false, cap, "pop_cap"};
    if (cost > 0 && food < cost)
        return {false, cap, "no_food"};
    return {true, cap, ""};
}

// tribe::o_Form returns bool in AL (upper EAX bits may be stale non-zero garbage): confirm real tribe-list growth.
inline bool npc_tribe_formed(size_t tribes_before, size_t tribes_after)
{
    return tribes_after > tribes_before;
}

inline bool should_auto_form_npc(bool has_guests, uint32_t auto_count, double now, double last_at, bool need_guest,
                                 int free_npcs, bool stage_won, bool race_ended)
{
    if (!has_guests || auto_count >= 6 || (now - last_at) < 2.0)
        return false;
    if (need_guest)
        return true;
    return !stage_won && !race_ended && free_npcs < 2;
}

struct SpeciesSyncPlan {
    bool swap_slot = false;
    bool swap_members = false;
    bool mark_members_done = false;
};

// Decides per-tribe species slot + adult member swap (Host and Guest, BUG-1580):
// - Re-swaps whenever `want_key` changes (rejoin/outfitter).
// - Keeps retrying member swap until `!cinematic_playing` AND all live adult members match `want_key`.
inline SpeciesSyncPlan decide_species_swap(uint32_t want_key, uint32_t cur_slot_key, bool members_swapped,
                                           bool cinematic_playing, bool all_adults_match)
{
    SpeciesSyncPlan p;
    if (!want_key)
        return p;
    bool key_changed = (cur_slot_key != want_key);
    if (key_changed) {
        p.swap_slot = true;
        members_swapped = false;
    }
    if (!members_swapped || !all_adults_match) {
        if (!cinematic_playing) {
            p.swap_members = !all_adults_match || key_changed;
            p.mark_members_done = true;
        }
    }
    return p;
}

// Host influence purchase validation: must return false when Tribe Rivals is inactive or caller is not host (BUG-1580).
inline bool can_buy_influence(bool rivals_active, bool is_host_role, bool has_tribe, int32_t food, int32_t price)
{
    return rivals_active && is_host_role && has_tribe && price >= 0 && food >= price;
}

inline int32_t tool_sell_refund(int tool_type)
{
    if (tool_type < kMinToolKind || tool_type > kMaxToolKind)
        return 0;
    int32_t c = tool_buy_cost(tool_type, false);
    return c > 0 ? c : 0;
}

// A knocked-out chieftain has +0xB58 bit 0x800 set and/or BT node 0x05CA7FA7 even while HP > 0 (0.3f floor).
inline bool is_citizen_knocked_out(bool is_dead, float hp, uint32_t flags_b58, uint32_t bt_node)
{
    return is_dead || hp <= 0.0f || (flags_b58 & 0x800u) != 0 || bt_node == 0x05CA7FA7u;
}

constexpr int32_t kMaxDraftFoodPayout = 2000;

inline int32_t creature_draft_payout_food(int32_t raw_payout)
{
    if (raw_payout <= 0)
        return 0;
    return std::min(raw_payout, kMaxDraftFoodPayout);
}

inline int32_t deliver_creature_payout_for(rivals::Draft& draft, bool rivals_active, bool tribe_assigned, uint8_t pid)
{
    if (!rivals_active || draft.active || !tribe_assigned || !rivals::valid_player(pid))
        return 0;
    int32_t raw = draft.take_pending_payout_for(rivals::Stage::Creature, pid);
    return creature_draft_payout_food(raw);
}

// ---------------- BUG-1554: F9 Influence UI NPC tribe list ----------------
struct NpcUiSource {
    uint32_t npc_pol = 0;
    NpcRel rel = NpcRel::Neutral;
    bool conquered = false;
    std::string name;
};

struct NpcUiSpec {
    uint32_t npc_id = 0;
    std::string label;
    bool allied = false;
    bool hostile = false;
    bool can_turn = false;
    bool can_protect = false;
    bool can_smooth = false;
};

inline std::vector<NpcUiSpec> build_npc_ui_list(const std::vector<NpcUiSource>& npcs)
{
    std::vector<NpcUiSpec> out;
    out.reserve(npcs.size());
    int idx = 0;
    for (const auto& src : npcs) {
        if (src.npc_pol == 0 || src.conquered || src.rel == NpcRel::Conquered)
            continue;
        idx++;
        NpcUiSpec e;
        e.npc_id = src.npc_pol;
        e.allied = (src.rel == NpcRel::Allied);
        e.hostile = (src.rel == NpcRel::Hostile || src.rel == NpcRel::Annoyed);
        e.can_turn = true;
        e.can_protect = e.allied;
        e.can_smooth = e.hostile;
        char buf[128];
        if (!src.name.empty()) {
            snprintf(buf, sizeof buf, "%s (%s)", src.name.c_str(), npc_rel_name(src.rel));
        } else {
            snprintf(buf, sizeof buf, "NPC Tribe #%d (%s)", idx, npc_rel_name(src.rel));
        }
        e.label = buf;
        out.push_back(std::move(e));
    }
    return out;
}

// ---------------- 11.3.3: Alliance food / tool gifts ----------------
enum class GiftKind : uint8_t {
    Food = 1,
    Tool = 2,
};

struct GiftCheck {
    bool ok = false;
    const char* err = "";
};

inline GiftCheck validate_gift(bool rivals_active, bool allied, uint8_t from_pid, uint8_t to_pid, GiftKind kind,
                               int32_t value, int32_t sender_food, uint32_t sender_tools_mask)
{
    if (!rivals_active)
        return {false, "tribe rivals is not active"};
    if (!rivals::valid_player(from_pid) || !rivals::valid_player(to_pid) || from_pid == to_pid)
        return {false, "invalid player"};
    if (!allied)
        return {false, "players must be allied to send gifts"};
    if (kind == GiftKind::Food) {
        if (value <= 0 || value > 10000)
            return {false, "invalid food amount"};
        if (sender_food < value)
            return {false, "not enough food"};
        return {true, ""};
    }
    if (kind == GiftKind::Tool) {
        if (value < kMinToolKind || value > kMaxToolKind)
            return {false, "invalid tool type"};
        if ((sender_tools_mask & (1u << value)) == 0)
            return {false, "sender does not own that tool"};
        return {true, ""};
    }
    return {false, "unknown gift kind"};
}

// ---------------- 11.6 & R6: Tribe Draft items ----------------
inline const char* tribe_tool_name(int kind)
{
    switch (kind) {
    case 1: return "Stone Axe Hut";
    case 2: return "Throwing Spears Hut";
    case 3: return "Torch Hut";
    case 4: return "Wooden Horn Hut";
    case 5: return "Maracas Hut";
    case 6: return "Didgeridoo Hut";
    case 7: return "Fishing Spear Hut";
    case 8: return "Gathering Cane Hut";
    case 9: return "Healing Staff Hut";
    default: return "Tribe Tool";
    }
}

inline std::vector<rivals::Draft::Item> build_tribe_draft_items(uint32_t winner_tools_mask)
{
    // Always include at least the base starting tools (1=Axe, 5=Maracas) if mask is empty.
    uint32_t mask = winner_tools_mask & 0x3FEu; // bits 1..9
    if (mask == 0)
        mask = (1u << 1) | (1u << 5);
    std::vector<rivals::Draft::Item> out;
    for (int k = kMinToolKind; k <= kMaxToolKind; k++) {
        if (mask & (1u << k))
            out.push_back({(uint32_t)k, (uint8_t)k, tribe_tool_name(k)});
    }
    return out;
}

// ---------------- Wire payloads (MSG_RIVALS_FACTION = 81, Stage::Tribe) ----------------
struct AssignEntry {
    uint8_t player_id = 0;
    uint32_t tribe_pol = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    uint32_t sp_inst = 0, sp_type = 0, sp_group = 0;
};

inline void write_assign(net::Writer& w, uint32_t rev, const std::vector<AssignEntry>& list)
{
    w.u32(rev);
    uint8_t n = (uint8_t)std::min<size_t>(list.size(), rivals::kMaxPlayers);
    w.u8(n);
    for (uint8_t i = 0; i < n; i++) {
        const auto& e = list[i];
        w.u8(e.player_id);
        w.u32(e.tribe_pol);
        w.f32(e.x);
        w.f32(e.y);
        w.f32(e.z);
        w.u32(e.sp_inst);
        w.u32(e.sp_type);
        w.u32(e.sp_group);
    }
}

inline bool read_assign(net::Reader& r, uint32_t& rev_out, std::vector<AssignEntry>& out)
{
    out.clear();
    rev_out = r.u32();
    uint8_t n = r.u8();
    if (!r.ok() || n > rivals::kMaxPlayers)
        return false;
    out.reserve(n);
    for (uint8_t i = 0; i < n; i++) {
        AssignEntry e;
        e.player_id = r.u8();
        e.tribe_pol = r.u32();
        e.x = r.f32();
        e.y = r.f32();
        e.z = r.f32();
        e.sp_inst = r.u32();
        e.sp_type = r.u32();
        e.sp_group = r.u32();
        if (!r.ok() || !rivals::valid_player(e.player_id))
            return false;
        out.push_back(e);
    }
    return true;
}

struct NpcRelEntry {
    uint32_t npc_pol = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    NpcRel rel = NpcRel::Neutral;
    bool is_protected = false;
};

inline void write_npc_rels(net::Writer& w, uint8_t target_pid, const std::vector<NpcRelEntry>& list)
{
    w.u8(target_pid);
    uint8_t n = (uint8_t)std::min<size_t>(list.size(), kMaxWireTribes);
    w.u8(n);
    for (uint8_t i = 0; i < n; i++) {
        const auto& e = list[i];
        w.u32(e.npc_pol);
        w.f32(e.x);
        w.f32(e.y);
        w.f32(e.z);
        w.u8((uint8_t)e.rel);
        w.u8(e.is_protected ? 1 : 0);
    }
}

inline bool read_npc_rels(net::Reader& r, uint8_t& target_pid_out, std::vector<NpcRelEntry>& out)
{
    out.clear();
    target_pid_out = r.u8();
    uint8_t n = r.u8();
    if (!r.ok() || !rivals::valid_player(target_pid_out) || n > kMaxWireTribes)
        return false;
    out.reserve(n);
    for (uint8_t i = 0; i < n; i++) {
        NpcRelEntry e;
        e.npc_pol = r.u32();
        e.x = r.f32();
        e.y = r.f32();
        e.z = r.f32();
        uint8_t rv = r.u8();
        uint8_t pr = r.u8();
        if (!r.ok() || rv > (uint8_t)NpcRel::Conquered)
            return false;
        e.rel = (NpcRel)rv;
        e.is_protected = pr != 0;
        out.push_back(e);
    }
    return true;
}

struct GiftMsg {
    uint8_t from_pid = 0;
    uint8_t to_pid = 0;
    GiftKind kind = GiftKind::Food;
    int32_t value = 0;
};

inline void write_gift(net::Writer& w, const GiftMsg& m)
{
    w.u8(m.from_pid);
    w.u8(m.to_pid);
    w.u8((uint8_t)m.kind);
    w.u32((uint32_t)m.value);
}

inline bool read_gift(net::Reader& r, GiftMsg& out)
{
    out.from_pid = r.u8();
    out.to_pid = r.u8();
    uint8_t k = r.u8();
    out.value = (int32_t)r.u32();
    if (!r.ok() || !rivals::valid_player(out.from_pid) || !rivals::valid_player(out.to_pid))
        return false;
    if (k != (uint8_t)GiftKind::Food && k != (uint8_t)GiftKind::Tool)
        return false;
    out.kind = (GiftKind)k;
    return true;
}

enum class PvpHitKind : uint8_t {
    Member = 1,
    MainHut = 2,
    ToolHut = 3,
    RaidFood = 4,
};

struct PvpHitMsg {
    uint8_t attacker_pid = 0;
    uint8_t victim_pid = 0;
    PvpHitKind kind = PvpHitKind::Member;
    uint16_t target_id = 0;
    float amount = 0.0f;
};

inline void write_pvp_hit(net::Writer& w, const PvpHitMsg& m)
{
    w.u8(m.attacker_pid);
    w.u8(m.victim_pid);
    w.u8((uint8_t)m.kind);
    w.u16(m.target_id);
    w.f32(m.amount);
}

inline bool read_pvp_hit(net::Reader& r, PvpHitMsg& out)
{
    out.attacker_pid = r.u8();
    out.victim_pid = r.u8();
    uint8_t k = r.u8();
    out.target_id = r.u16();
    out.amount = r.f32();
    if (!r.ok() || !rivals::valid_player(out.attacker_pid) || !rivals::valid_player(out.victim_pid))
        return false;
    if (k < (uint8_t)PvpHitKind::Member || k > (uint8_t)PvpHitKind::RaidFood)
        return false;
    if (!std::isfinite(out.amount))
        return false;
    out.kind = (PvpHitKind)k;
    return true;
}

enum class DraftApplyKind : uint8_t {
    ToolUnlock = 1,
    Payout = 2,
};

struct DraftApplyMsg {
    uint8_t target_pid = 0;
    DraftApplyKind kind = DraftApplyKind::ToolUnlock;
    uint32_t value = 0;
};

inline void write_draft_apply(net::Writer& w, const DraftApplyMsg& m)
{
    w.u8(m.target_pid);
    w.u8((uint8_t)m.kind);
    w.u32(m.value);
}

inline bool read_draft_apply(net::Reader& r, DraftApplyMsg& out)
{
    out.target_pid = r.u8();
    uint8_t k = r.u8();
    out.value = r.u32();
    if (!r.ok() || !rivals::valid_player(out.target_pid))
        return false;
    if (k != (uint8_t)DraftApplyKind::ToolUnlock && k != (uint8_t)DraftApplyKind::Payout)
        return false;
    out.kind = (DraftApplyKind)k;
    return true;
}

// SK_STAGE_CUSTOM (16): own-tribe food sync, guest food/tool operations, immunity & chieftain respawn sync
enum class CustomOp : uint8_t {
    FoodTotal = 1,    // host -> guest: authoritative own tribe food total
    FoodDelta = 2,    // guest -> host: own tribe food spend/earn delta
    BuyMember = 3,    // guest -> host: buy baby in own tribe from own food
    BuyTool = 4,      // guest -> host: unlock/build tool in own tribe from own food
    Immunity = 5,     // host -> all (BUG-1571): player_id, value = remaining shield seconds (0 = none), tools_mask bit 0 = wiping
    SellTool = 6,     // guest -> host (BUG-1580): sell tool hut `value` (1..9) from own tribe and refund food
    ChiefRespawn = 7, // host -> guest (BUG-1580): player_id, value = remaining KO seconds (0 = recovered), tools_mask bit 0 = ko
};

struct FoodSyncMsg {
    CustomOp op = CustomOp::FoodTotal;
    uint8_t player_id = 0;
    int32_t value = 0;
    uint32_t tools_mask = 0;
};

inline void write_food_sync(net::Writer& w, const FoodSyncMsg& m)
{
    w.u8((uint8_t)m.op);
    w.u8(m.player_id);
    w.u32((uint32_t)m.value);
    w.u32(m.tools_mask);
}

inline bool read_food_sync(net::Reader& r, FoodSyncMsg& out)
{
    uint8_t op = r.u8();
    out.player_id = r.u8();
    out.value = (int32_t)r.u32();
    out.tools_mask = r.u32();
    if (!r.ok() || !rivals::valid_player(out.player_id))
        return false;
    if (op < (uint8_t)CustomOp::FoodTotal || op > (uint8_t)CustomOp::ChiefRespawn)
        return false;
    out.op = (CustomOp)op;
    return true;
}

// ---------------- BUG-1564: Creature -> Tribe pack pet handoff ----------------
constexpr size_t kMaxPackPets = 3;

// Deduplicates species instance IDs (DomesticateAnimal enforces max 1 pet per species per tribe),
// drops zero IDs, preserves order, and clamps to max_pets (3).
inline std::vector<uint32_t> normalize_pack_pet_species(const std::vector<uint32_t>& raw,
                                                        size_t max_pets = kMaxPackPets)
{
    std::vector<uint32_t> out;
    out.reserve(std::min(raw.size(), max_pets));
    for (uint32_t sp : raw) {
        if (sp == 0)
            continue;
        if (std::find(out.begin(), out.end(), sp) != out.end())
            continue;
        out.push_back(sp);
        if (out.size() >= max_pets)
            break;
    }
    return out;
}

struct PackPetPlan {
    std::vector<uint32_t> species[rivals::kMaxPlayers];
    bool spawned[rivals::kMaxPlayers] = {};

    void clear_all()
    {
        for (int p = 0; p < rivals::kMaxPlayers; p++) {
            species[p].clear();
            spawned[p] = false;
        }
    }

    void reset_spawned()
    {
        for (int p = 0; p < rivals::kMaxPlayers; p++)
            spawned[p] = false;
    }
};

inline void record_pack_pets(PackPetPlan& plan, uint8_t pid, const std::vector<uint32_t>& raw)
{
    if (!rivals::valid_player(pid))
        return;
    plan.species[pid] = normalize_pack_pet_species(raw, kMaxPackPets);
}

// Returns the species instance IDs to spawn as domesticated pets for player `pid` in Tribe Rivals.
// Idempotent: marks `plan.spawned[pid] = true` once evaluated on a ready tribe and skips any species
// already domesticated at that tribe, clamping total pets to kMaxPackPets (3).
inline std::vector<uint32_t> take_pack_pets_to_spawn(PackPetPlan& plan, bool fix_on, bool rivals_active, uint8_t pid,
                                                     bool tribe_ready,
                                                     const std::vector<uint32_t>& existing_pet_species)
{
    std::vector<uint32_t> out;
    if (!fix_on || !rivals_active || !rivals::valid_player(pid) || !tribe_ready || plan.spawned[pid])
        return out;
    plan.spawned[pid] = true;
    size_t have = existing_pet_species.size();
    if (have >= kMaxPackPets)
        return out;
    size_t room = kMaxPackPets - have;
    for (uint32_t sp : plan.species[pid]) {
        if (sp == 0)
            continue;
        if (std::find(existing_pet_species.begin(), existing_pet_species.end(), sp) != existing_pet_species.end())
            continue;
        if (std::find(out.begin(), out.end(), sp) != out.end())
            continue;
        out.push_back(sp);
        if (out.size() >= room)
            break;
    }
    return out;
}

} // namespace tribe_rivals
