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

// ---------------- R2: Base distance site picker ----------------
struct TribeSite {
    uint32_t pol = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool is_host = false;
    bool conquered = false;
    bool taken = false;
};

inline float dist3(float ax, float ay, float az, float bx, float by, float bz)
{
    float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
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
        if (s.is_host || s.conquered || s.taken)
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

// ---------------- R7: War PvP & Main Village Inviolability ----------------
enum class DamageDecision : uint8_t {
    Passthrough = 0, // not rivals, or NPC target, or self
    Block = 1,       // player-vs-player while Neutral or Allied
    AllowPvp = 2,    // player-vs-player at War (member or extra tool hut)
    ClampMainHut = 3 // main village hut of any player tribe: clamp HP above floor, never conquer
};

inline DamageDecision decide_damage(bool rivals_active, uint8_t att_owner, uint8_t vic_owner, bool at_war,
                                    bool is_main_hut)
{
    if (!rivals_active)
        return DamageDecision::Passthrough;
    if (!rivals::valid_player(vic_owner))
        return DamageDecision::Passthrough;
    if (rivals::valid_player(att_owner)) {
        if (att_owner == vic_owner)
            return DamageDecision::Passthrough;
        if (!at_war)
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

// SK_STAGE_CUSTOM (16): own-tribe food sync & guest food operations
enum class CustomOp : uint8_t {
    FoodTotal = 1,   // host -> guest: authoritative own tribe food total
    FoodDelta = 2,   // guest -> host: own tribe food spend/earn delta
    BuyMember = 3,   // guest -> host: buy baby in own tribe from own food
    BuyTool = 4,     // guest -> host: unlock/build tool in own tribe from own food
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
    if (op < (uint8_t)CustomOp::FoodTotal || op > (uint8_t)CustomOp::BuyTool)
        return false;
    out.op = (CustomOp)op;
    return true;
}

} // namespace tribe_rivals
