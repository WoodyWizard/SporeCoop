// Portable logic for Cell stage Rivals mode (WP-R2 of 0.8.0, BUG-1546, docs/11-rivals.md 11.3.1, 11.5, 11.6).
// No Windows or SporeKit dependencies so native Linux ASan/UBSan unit tests cover every rule and wire codec.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "net/buffer.h"
#include "rivals_logic.h"

namespace cell_rivals_logic {

constexpr int32_t kCellFullFood = 1000;
constexpr int32_t kMaxFood = 2000;
constexpr int32_t kMaxDna = 100000;
constexpr int kCellParts = 13;
constexpr int kPvpMeatChunks = 3;
constexpr float kMeatDropRadius = 2.0f;
constexpr float kMeatDropSpeed = 2.5f;

// Stage-custom MSG_RIVALS_FACTION (81) subkinds for Stage::Cell (wire/rivals_faction.h SK_STAGE_CUSTOM = 16).
constexpr uint8_t kSubPvpKill = 16;      // victim guest -> host: rival cell killed at War, drop physical meat
constexpr uint8_t kSubFoodRollback = 17; // host -> guest: chunk already eaten first by someone else, refund food

// Operations carried by wire::rivals_faction::SK_DRAFT_APPLY (7) for Stage::Cell.
constexpr uint8_t kDraftOpUnlockPart = 1; // u32 part_idx (0..12)
constexpr uint8_t kDraftOpPayout = 2;     // u32 creature DNA payout amount

enum PartKind : uint8_t {
    kPartMouth = 1,
    kPartMovement = 2,
    kPartWeapon = 3,
    kPartSensory = 4,
};

struct PartDesc {
    uint8_t index;
    PartKind kind;
    const char* name;
};

// The 13 Cell part slots at cCellSerializableData +0x34 (docs/re/cell.md#cell-parts-shared, docs/re/creations.md).
inline const PartDesc& part_desc(int index)
{
    static constexpr PartDesc kParts[kCellParts] = {
        {0, kPartMouth, "Filter Mouth"},
        {1, kPartMouth, "Jaw"},
        {2, kPartMouth, "Proboscis"},
        {3, kPartMovement, "Flagella"},
        {4, kPartMovement, "Jet"},
        {5, kPartMovement, "Cilia"},
        {6, kPartWeapon, "Spike"},
        {7, kPartWeapon, "Poison"},
        {8, kPartWeapon, "Electric"},
        {9, kPartSensory, "Eyespot"},
        {10, kPartSensory, "Button Eye"},
        {11, kPartSensory, "Beady Eye"},
        {12, kPartSensory, "Cell Part #12"},
    };
    if (index < 0 || index >= kCellParts)
        return kParts[0];
    return kParts[index];
}

// ---- Per-player bar, DNA and parts ----
inline float bar_fraction(int32_t food)
{
    if (food <= 0)
        return 0.0f;
    if (food >= kCellFullFood)
        return 1.0f;
    return (float)food / (float)kCellFullFood;
}

inline int bar_percent(int32_t food)
{
    if (food <= 0)
        return 0;
    if (food >= kCellFullFood)
        return 100;
    return (int)((int64_t)food * 100 / kCellFullFood);
}

inline bool bar_is_full(int32_t food) { return food >= kCellFullFood; }

inline uint16_t mask_from_states(const int* states, int count = kCellParts)
{
    if (!states || count <= 0)
        return 0;
    int n = std::min(count, kCellParts);
    uint16_t m = 0;
    for (int i = 0; i < n; i++)
        if (states[i] != 0)
            m |= (uint16_t)(1u << i);
    return m;
}

inline uint16_t popcount16(uint16_t mask)
{
    uint16_t n = 0;
    for (uint16_t v = mask; v; v &= (uint16_t)(v - 1))
        n++;
    return n;
}

// Rollback delta applied when a guest's bite arrived after someone else already ate the physical chunk.
// Returns a negative adjustment clamped so local food never drops below 0 (0 if nothing to roll back).
inline int32_t clamp_food_rollback(int32_t cur_food, int32_t delta)
{
    if (delta >= 0 || cur_food <= 0)
        return 0;
    int32_t neg = std::max(delta, -cur_food);
    return neg < 0 ? neg : 0;
}

// ---- Policy gates (11.3.1) ----
inline bool can_pvp_damage(bool cell_rivals_active, bool war_between_pair, bool friendly_fire_on)
{
    if (cell_rivals_active)
        return war_between_pair;
    return friendly_fire_on;
}

inline bool ko_allowed(bool cell_rivals_active, bool setting_ko_on)
{
    return !cell_rivals_active && setting_ko_on;
}

inline bool regen_allowed(bool cell_rivals_active, bool setting_regen_on)
{
    return !cell_rivals_active && setting_regen_on;
}

inline bool parts_auto_share_allowed(bool cell_rivals_active, bool setting_parts_shared_cell)
{
    return !cell_rivals_active && setting_parts_shared_cell;
}

inline bool pool_food_shared(bool cell_rivals_active, bool own_progress_active)
{
    return !cell_rivals_active && !own_progress_active;
}

inline bool level_clip_allowed(bool cell_rivals_active, bool own_progress_active)
{
    return !cell_rivals_active && own_progress_active;
}

inline bool influence_allowed_in_cell(bool cell_rivals_active)
{
    (void)cell_rivals_active;
    return false; // 11.3.1: no NPC diplomacy or influence in the Cell stage
}

// ---- Host-authoritative War PvP routing ----
enum class WarHitAction : uint8_t {
    Refuse = 0,
    HurtHost = 1,
    ForwardGuest = 2,
};

inline WarHitAction route_war_pvp_hit(bool cell_rivals_active, bool at_war, uint8_t from, uint8_t attacker,
                                      uint8_t victim, bool victim_present)
{
    if (!cell_rivals_active || !at_war)
        return WarHitAction::Refuse;
    if (from != attacker)
        return WarHitAction::Refuse;
    if (!rivals::valid_player(attacker) || !rivals::valid_player(victim) || attacker == victim)
        return WarHitAction::Refuse;
    if (victim == 0)
        return WarHitAction::HurtHost;
    return victim_present ? WarHitAction::ForwardGuest : WarHitAction::Refuse;
}

// ---- Meat drop placement on rival kill ----
struct MeatSpot {
    float dx = 0.0f;
    float dy = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
};

inline void meat_drop_spots(int count, float radius, MeatSpot* out, float speed = kMeatDropSpeed)
{
    if (!out || count <= 0)
        return;
    if (!std::isfinite(radius) || radius < 0.25f || radius > 20.0f)
        radius = kMeatDropRadius;
    if (!std::isfinite(speed) || speed < 0.0f || speed > 20.0f)
        speed = kMeatDropSpeed;
    constexpr float kTwoPi = 6.2831853071795864769f;
    for (int i = 0; i < count; i++) {
        float ang = kTwoPi * (float)i / (float)count;
        float ca = std::cos(ang);
        float sa = std::sin(ang);
        out[i] = {radius * ca, radius * sa, speed * ca, speed * sa};
    }
}

inline int meat_drop_level(int victim_level)
{
    return std::clamp(victim_level - 1, 0, 15);
}

inline float convert_scale_coord(float v, float from_scale, float to_scale)
{
    if (!std::isfinite(v))
        return 0.0f;
    if (!std::isfinite(from_scale) || !std::isfinite(to_scale) || from_scale <= 1e-6f || to_scale <= 1e-6f)
        return v;
    float r = v * (from_scale / to_scale);
    return std::isfinite(r) ? r : v;
}

// ---- Cell part draft (11.6) ----
// Draft item IDs are 1..13 (part index + 1) so item ID 0 is never used.
inline uint32_t part_index_to_item_id(int part_idx)
{
    return (part_idx >= 0 && part_idx < kCellParts) ? (uint32_t)(part_idx + 1) : 0u;
}

inline int item_id_to_part_index(uint32_t item_id)
{
    return (item_id >= 1u && item_id <= (uint32_t)kCellParts) ? (int)(item_id - 1u) : -1;
}

// Builds the Cell part draft list from the winner's 13-bit unlocked Cell part mask.
// Unlockable parts (indices 2..11) are listed first, followed by remaining unlocked slots (0, 1, 12).
// `common_mask` optionally excludes parts already owned by every picker.
inline std::vector<rivals::Draft::Item> build_draft_items(uint16_t winner_mask, uint16_t common_mask = 0)
{
    std::vector<rivals::Draft::Item> items;
    uint16_t offer = (uint16_t)(winner_mask & (uint16_t)(~common_mask));
    if (offer == 0)
        offer = winner_mask; // fall back to all winner parts if pickers share everything
    static constexpr int kOrder[kCellParts] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 1, 12};
    for (int i = 0; i < kCellParts; i++) {
        int idx = kOrder[i];
        if ((offer & (uint16_t)(1u << idx)) == 0)
            continue;
        const PartDesc& pd = part_desc(idx);
        rivals::Draft::Item it;
        it.id = part_index_to_item_id(idx);
        it.kind = (uint8_t)pd.kind;
        it.label = pd.name;
        items.push_back(std::move(it));
    }
    return items;
}

// ---- Wire codecs (carried inside MSG_RIVALS_FACTION = 81, Stage::Cell) ----
struct ProgressMsg {
    int32_t food = 0;
    int32_t dna = 0;
    uint16_t parts_mask = 0;
};

inline void write_progress(net::Writer& w, const ProgressMsg& m)
{
    w.u32((uint32_t)std::clamp(m.food, 0, kMaxFood));
    w.u32((uint32_t)std::clamp(m.dna, 0, kMaxDna));
    w.u16((uint16_t)(m.parts_mask & ((1u << kCellParts) - 1u)));
}

inline bool read_progress(net::Reader& r, ProgressMsg& out)
{
    int32_t food = (int32_t)r.u32();
    int32_t dna = (int32_t)r.u32();
    uint16_t mask = r.u16();
    if (!r.ok() || food < 0 || food > kMaxFood || dna < 0 || dna > kMaxDna)
        return false;
    out.food = food;
    out.dna = dna;
    out.parts_mask = (uint16_t)(mask & ((1u << kCellParts) - 1u));
    return true;
}

struct PvpHitMsg {
    uint8_t attacker = 0;
    uint8_t victim = 0;
    uint8_t dmg = 1;
    uint8_t type = 4;
};

inline void write_pvp_hit(net::Writer& w, const PvpHitMsg& m)
{
    w.u8(m.attacker);
    w.u8(m.victim);
    w.u8(m.dmg == 0 ? 1 : m.dmg);
    w.u8(m.type);
}

inline bool read_pvp_hit(net::Reader& r, PvpHitMsg& out)
{
    uint8_t att = r.u8();
    uint8_t vic = r.u8();
    uint8_t dmg = r.u8();
    uint8_t type = r.u8();
    if (!r.ok() || !rivals::valid_player(att) || !rivals::valid_player(vic) || att == vic || dmg == 0)
        return false;
    out = {att, vic, dmg, type};
    return true;
}

struct PvpKillMsg {
    uint8_t killer = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float scale = 1.0f;
    uint8_t level = 1;
};

inline void write_pvp_kill(net::Writer& w, const PvpKillMsg& m)
{
    w.u8(m.killer);
    w.f32(std::isfinite(m.x) ? m.x : 0.0f);
    w.f32(std::isfinite(m.y) ? m.y : 0.0f);
    w.f32(std::isfinite(m.z) ? m.z : 0.0f);
    w.f32(std::isfinite(m.scale) && m.scale > 0.0f ? m.scale : 1.0f);
    w.u8(m.level);
}

inline bool read_pvp_kill(net::Reader& r, PvpKillMsg& out)
{
    PvpKillMsg m;
    m.killer = r.u8();
    m.x = r.f32();
    m.y = r.f32();
    m.z = r.f32();
    m.scale = r.f32();
    m.level = r.u8();
    if (!r.ok() || !rivals::valid_player(m.killer) || !std::isfinite(m.x) || !std::isfinite(m.y) ||
        !std::isfinite(m.z) || !std::isfinite(m.scale) || m.scale <= 0.0f || m.scale > 1e9f || m.level > 16)
        return false;
    out = m;
    return true;
}

struct DraftApplyMsg {
    uint8_t op = 0;
    uint32_t value = 0;
};

inline void write_draft_apply(net::Writer& w, const DraftApplyMsg& m)
{
    w.u8(m.op);
    w.u32(m.value);
}

inline bool read_draft_apply(net::Reader& r, DraftApplyMsg& out)
{
    uint8_t op = r.u8();
    uint32_t val = r.u32();
    if (!r.ok())
        return false;
    if (op == kDraftOpUnlockPart) {
        if (val >= (uint32_t)kCellParts)
            return false;
    } else if (op == kDraftOpPayout) {
        if (val == 0 || val > (uint32_t)kMaxDna)
            return false;
    } else {
        return false;
    }
    out = {op, val};
    return true;
}

struct FoodRollbackMsg {
    int32_t delta = 0; // negative food refund (-999..-1)
};

inline void write_food_rollback(net::Writer& w, int32_t delta)
{
    int32_t clamped = std::clamp(delta, -999, -1);
    w.u32((uint32_t)clamped);
}

inline bool read_food_rollback(net::Reader& r, FoodRollbackMsg& out)
{
    int32_t d = (int32_t)r.u32();
    if (!r.ok() || d >= 0 || d < -999)
        return false;
    out.delta = d;
    return true;
}

} // namespace cell_rivals_logic
