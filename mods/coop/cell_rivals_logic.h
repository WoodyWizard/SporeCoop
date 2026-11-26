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

// BUG-1538 / BUG-1552: after rolling back late food, cap spent DNA at DNAForFood(new_food) so the Cell editor budget
// never drops below 0 (preserving negative spent from selling starting parts).
inline int32_t clamp_spent_after_rollback(int32_t cur_spent, int32_t dna_for_food)
{
    if (dna_for_food < 0)
        return cur_spent;
    return std::min(cur_spent, dna_for_food);
}

// BUG-1552: when a guest wins Cell first and both catch-up and draft have completely finished, unlock the host's
// local Cell->Creature evolution bar (1000 food) if the host is still below 1000 so the party is never stranded.
inline bool should_unlock_host_stage_end(bool is_host, bool rivals_active, bool race_end_handled, bool stage_finished,
                                         bool catchup_active, bool draft_active, uint8_t winner, int32_t host_food,
                                         bool already_unlocked)
{
    if (!is_host || !rivals_active || !race_end_handled || !stage_finished || catchup_active || draft_active ||
        already_unlocked)
        return false;
    if (!rivals::valid_player(winner) || winner == 0)
        return false;
    return host_food < kCellFullFood;
}

// BUG-1552: bits 4..6 of MSG_CELL_STATE cell flags carry the player owner ID (0..3) of a player's poison cloud
// (bit 4 = owner present, bits 5..6 = owner ID 0..3). Older guests ignore bits 4..7; older hosts leave bit 4 clear.
constexpr uint8_t kCloudFlagHasOwner = 0x10u;
constexpr uint8_t kCloudFlagOwnerShift = 5u;
constexpr uint8_t kCloudFlagOwnerMask = 0x03u;

inline uint8_t encode_cloud_owner_flags(uint8_t base_flags, uint8_t owner)
{
    if (!rivals::valid_player(owner))
        return base_flags;
    return (uint8_t)(base_flags | kCloudFlagHasOwner | ((owner & kCloudFlagOwnerMask) << kCloudFlagOwnerShift));
}

inline bool decode_cloud_owner_flags(uint8_t flags, uint8_t* out_owner)
{
    if ((flags & kCloudFlagHasOwner) == 0)
        return false;
    if (out_owner)
        *out_owner = (uint8_t)((flags >> kCloudFlagOwnerShift) & kCloudFlagOwnerMask);
    return true;
}

// BUG-1552: 3-4 player poison cloud PvP permission. In Cell Rivals, when the cloud owner is known, damage is allowed
// strictly when victim and cloud owner are at War (never self-damage, never hurting neutral players while at war with a
// third player). Falls back to `can_pvp_any` only when talking to an older host that omitted owner bits.
inline bool allow_poison_cloud_pvp(bool cell_rivals_active, uint8_t victim_id, bool has_owner, uint8_t owner_id,
                                   bool at_war_with_owner, bool can_pvp_any_now, bool together_allow)
{
    if (!cell_rivals_active)
        return together_allow;
    if (has_owner) {
        if (!rivals::valid_player(owner_id) || owner_id == victim_id)
            return false;
        return at_war_with_owner;
    }
    return can_pvp_any_now;
}

// BUG-1552: during Cell Rivals catch-up when host and guest world scales diverge wildly (e.g. host at level 10
// wscale=15000 vs guest at level 1 wscale=0.5, ratio 30,000x), stop mirroring over-scaled host cells and let the guest
// play its own local pool so it can eat food and catch up. Hysteresis [0.25, 4.0] avoids thrashing at the boundary.
constexpr float kScaleDivergeHigh = 8.0f;
constexpr float kScaleDivergeLow = 0.125f;
constexpr float kScaleConvergeHigh = 4.0f;
constexpr float kScaleConvergeLow = 0.25f;

inline bool scale_diverged(float host_scale, float own_scale)
{
    if (!std::isfinite(host_scale) || !std::isfinite(own_scale) || host_scale <= 1e-6f || own_scale <= 1e-6f)
        return false;
    float r = host_scale / own_scale;
    return !std::isfinite(r) || r > kScaleDivergeHigh || r < kScaleDivergeLow;
}

inline bool should_use_local_pool(bool cell_rivals_active, float host_scale, float own_scale, bool currently_local)
{
    if (!cell_rivals_active)
        return false;
    if (!std::isfinite(host_scale) || !std::isfinite(own_scale) || host_scale <= 1e-6f || own_scale <= 1e-6f)
        return currently_local;
    float r = host_scale / own_scale;
    if (!std::isfinite(r))
        return true;
    if (currently_local)
        return r > kScaleConvergeHigh || r < kScaleConvergeLow;
    return r > kScaleDivergeHigh || r < kScaleDivergeLow;
}

// BUG-1568: during Cell Rivals wscale divergence (e.g. host advances to food=1000 wscale=4.7..15000 while a rival
// guest is still at wscale=0.5), cell_drive scales the rival's puppet by peer_scale/own_scale (up to 999 m wide).
// Allowing HandleCollisionPair between our avatar and that over-scaled War rival puppet creates a huge penetration
// contact that blasts our avatar across the map (~660 m/s), where float32 precision loss in ViewBoxes at |x|>=8182.4
// or |y|>=4088.7 overflows 0x00E4E320's 9-int stack buffer (0xC0000005 storm at 0x00E4E384).
constexpr float kPuppetCollisionMaxRatio = 8.0f;
constexpr float kPuppetCollisionMinRatio = 0.125f;
constexpr float kPuppetCollisionMaxAbsSize = 15.0f;
constexpr float kFluidViewMaxWidth = 20.0f;
constexpr float kFluidViewMaxHeight = 15.0f;
constexpr float kFluidInvCols = 0.05000000074505806f; // 1/20 at 0x01483AE8
constexpr float kFluidInvRows = 0.06666667014360428f; // 1/15 at 0x01483AEC
constexpr float kFluidEps = 1.52587890625e-5f;        // 2^-16 at 0x01483AF4

inline bool puppet_scale_collision_unsafe(bool fix_on, bool cell_rivals_active, bool scale_solo, bool puppet_is_host,
                                          float own_size, float puppet_size, float host_scale, float own_scale)
{
    if (!fix_on || !cell_rivals_active)
        return false;
    if (puppet_is_host && (scale_solo || scale_diverged(host_scale, own_scale)))
        return true;
    if (!std::isfinite(own_size) || !std::isfinite(puppet_size) || own_size <= 1e-4f || puppet_size <= 1e-4f)
        return true;
    if (puppet_size > kPuppetCollisionMaxAbsSize)
        return true;
    float r = puppet_size / own_size;
    return !std::isfinite(r) || r > kPuppetCollisionMaxRatio || r < kPuppetCollisionMinRatio;
}

inline float clamp_puppet_size_for_scale(float own_size, float puppet_size)
{
    if (!std::isfinite(own_size) || own_size <= 1e-4f)
        return std::isfinite(puppet_size) ? std::clamp(puppet_size, 0.1f, kPuppetCollisionMaxAbsSize) : 0.55f;
    float hi = std::min(kPuppetCollisionMaxAbsSize, own_size * kPuppetCollisionMaxRatio);
    float lo = std::max(0.05f, own_size * kPuppetCollisionMinRatio);
    if (lo > hi)
        lo = hi;
    if (!std::isfinite(puppet_size))
        return own_size;
    return std::clamp(puppet_size, lo, hi);
}

// BUG-1568: Spore's Cell fluid grid helper 0x00E4E210 computes half_cols = ceil((maxX - minX) * 0.05 - 2^-16) and
// half_rows = ceil((maxY - minY) * (1/15) - 2^-16) into a fixed 9-int (3x3) stack buffer in 0x00E4E320.
// When |cam.x| >= 8182.4 or |cam.y| >= 4088.7, float32 rounding makes (cam+10)-(cam-10) > 20.0 or (cam+7.5)-(cam-7.5) >
// 15.0, pushing half_cols/half_rows to 2 (15 or 25 ints) and smashing 0x00E4E320's stack (0x00E4E384 crash).
// Shrinking max by 1+ ULP so max-min <= 20.0 / 15.0 keeps 0x00E4E210 strictly <= 9 cells at any finite coordinate.
inline bool sanitize_fluid_viewbox(float& min_x, float& max_x, float& min_y, float& max_y)
{
    auto clamp_axis = [](float& lo, float& hi, float max_span, float inv_cells) -> bool {
        if (!std::isfinite(lo) || !std::isfinite(hi) || !(hi > lo))
            return false;
        if ((hi - lo) * inv_cells - kFluidEps <= 1.0f)
            return false;
        float cand = lo + max_span;
        if (!std::isfinite(cand) || cand > hi)
            cand = hi;
        for (int i = 0; i < 8 && ((cand - lo) > max_span || (cand - lo) * inv_cells - kFluidEps > 1.0f) && cand > lo; i++)
            cand = std::nextafter(cand, lo);
        hi = cand;
        return true;
    };
    bool cx = clamp_axis(min_x, max_x, kFluidViewMaxWidth, kFluidInvCols);
    bool cy = clamp_axis(min_y, max_y, kFluidViewMaxHeight, kFluidInvRows);
    return cx || cy;
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

inline bool picker_needs_part(uint16_t picker_mask, int part_idx)
{
    if (part_idx < 0 || part_idx >= kCellParts)
        return false;
    return (picker_mask & (uint16_t)(1u << part_idx)) == 0;
}

// BUG-1552: union of winner parts missing from at least one non-winner picker.
// Returns `winner_mask` only when no picker masks were supplied (`picker_count == 0`); if every picker already owns
// every winner part, returns 0 so no pointless no-op draft is started.
inline uint16_t draft_missing_union_mask(uint16_t winner_mask, const uint16_t* picker_masks, size_t picker_count)
{
    if (!picker_masks || picker_count == 0)
        return winner_mask;
    uint16_t missing_union = 0;
    for (size_t i = 0; i < picker_count; i++)
        missing_union |= (uint16_t)(winner_mask & (uint16_t)(~picker_masks[i]));
    return missing_union;
}

// Builds the Cell part draft list for a 2-4 player race from the winner's unlocked part mask and each non-winner
// picker's owned mask. Only parts missing from at least one picker are offered, ordered with parts lacking on the most
// pickers first (tie-breaking on unlockable slots 2..11 before starting slots 0, 1, 12).
inline std::vector<rivals::Draft::Item> build_draft_items_for_pickers(uint16_t winner_mask,
                                                                      const uint16_t* picker_masks,
                                                                      size_t picker_count)
{
    std::vector<rivals::Draft::Item> items;
    uint16_t offer = draft_missing_union_mask(winner_mask, picker_masks, picker_count);
    if (offer == 0)
        return items;
    static constexpr int kOrder[kCellParts] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 1, 12};
    struct Cand {
        int idx;
        size_t missing_by;
        int order_pos;
    };
    std::vector<Cand> cands;
    cands.reserve(kCellParts);
    for (int i = 0; i < kCellParts; i++) {
        int idx = kOrder[i];
        if ((offer & (uint16_t)(1u << idx)) == 0)
            continue;
        size_t miss = 0;
        if (picker_masks && picker_count > 0) {
            for (size_t p = 0; p < picker_count; p++)
                if (picker_needs_part(picker_masks[p], idx))
                    miss++;
        }
        cands.push_back({idx, miss, i});
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (a.missing_by != b.missing_by)
            return a.missing_by > b.missing_by;
        return a.order_pos < b.order_pos;
    });
    items.reserve(cands.size());
    for (const auto& c : cands) {
        const PartDesc& pd = part_desc(c.idx);
        rivals::Draft::Item it;
        it.id = part_index_to_item_id(c.idx);
        it.kind = (uint8_t)pd.kind;
        it.label = pd.name;
        items.push_back(std::move(it));
    }
    return items;
}

// Builds the Cell part draft list from the winner's 13-bit unlocked Cell part mask.
// Unlockable parts (indices 2..11) are listed first, followed by remaining unlocked slots (0, 1, 12).
// `common_mask` optionally excludes parts already owned by every picker (if all winner parts are excluded, returns empty).
inline std::vector<rivals::Draft::Item> build_draft_items(uint16_t winner_mask, uint16_t common_mask = 0)
{
    if (common_mask == 0)
        return build_draft_items_for_pickers(winner_mask, nullptr, 0);
    return build_draft_items_for_pickers(winner_mask, &common_mask, 1);
}

// BUG-1552: resolves a picker's draft pick against the winner's parts and the picker's own owned parts so a 3-4p draft
// never wastes a pick on a part the picker already owns (substitutes the first winner part the picker still lacks, or
// returns -1 if the picker already owns every winner part).
inline int resolve_draft_pick(uint16_t winner_mask, uint16_t picker_mask, int requested_part)
{
    if (requested_part >= 0 && requested_part < kCellParts &&
        (winner_mask & (uint16_t)(1u << requested_part)) != 0 &&
        picker_needs_part(picker_mask, requested_part)) {
        return requested_part;
    }
    uint16_t missing = (uint16_t)(winner_mask & (uint16_t)(~picker_mask));
    if (missing == 0)
        return -1;
    static constexpr int kOrder[kCellParts] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 1, 12};
    for (int i = 0; i < kCellParts; i++) {
        int idx = kOrder[i];
        if ((missing & (uint16_t)(1u << idx)) != 0)
            return idx;
    }
    return -1;
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
