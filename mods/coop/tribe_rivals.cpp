// Tribe Rivals runtime module (WP-R4 of 0.8.0, BUG-1548, docs/11-rivals.md 11.3.3, 11.4-11.9).
#include "tribe_rivals.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "call_guard.h"
#include "creations.h"
#include "fixes.h"
#include "game.h"
#include "history.h"
#include "i18n.h"
#include "lifecycle.h"
#include "modes.h"
#include "npc.h"
#include "rivals.h"
#include "savestrip.h"
#include "species_line.h"
#include "sporekit_game.hpp"
#include "store.h"
#include "tribe.h"
#include "tribe_internal.h"
#include "tribe_logic.h"
#include "tribe_rivals_logic.h"
#include "wire/rivals_faction.h"
#include "world.h"

using skg::thiscall;
using skg::vcall;

namespace tribe {
bool guest_request_consequence_ability(int ab_type); // BUG-1564: defined in tribe.cpp
}

namespace tribe_rivals {

static const SkHost* H = nullptr;
static SkMod* M = nullptr;
static net::Session* S = nullptr;

static bool g_rivals_tribe_fix = true;
COOP_FIX("rivals.tribe", "BUG-1548", g_rivals_tribe_fix,
         "tribe: Tribe Rivals mode (NPC tribe takeover, guest perspective swap, per-player NPC relations & influence, "
         "war PvP with inviolable main village, alliance gifts, >50% stage end & Tribe draft)");

static bool g_pack_pets_fix = true;
COOP_FIX("rivals.tribe_pack_pets", "BUG-1564", g_pack_pets_fix,
         "tribe: carry over each player's Creature-stage pack species as domesticated village pets in Tribe Rivals");

static bool g_wipe_recovery_fix = true;
COOP_FIX("rivals.tribe_wipe_recovery", "BUG-1571", g_wipe_recovery_fix,
         "tribe: prevent PopDestroyed Game Over in Tribe Rivals, auto-respawn wiped player tribes, keep Add Baby usable, and grant 5-minute PvP immunity");

static bool g_tribe_polish_fix = true;
COOP_FIX("rivals.tribe_polish", "BUG-1573", g_tribe_polish_fix,
         "tribe: block Neutral/Ally/Shield food-mat stealing, clamp guest starting food to match host, rebuild held staff after species swap, and explain rival main-hut 10% floor");

// ---- Game addresses (verified GOG 2024 dc04aee5, docs/re/tribe.md#tribe-rivals-re) ----
static uintptr_t a_SetPlayerTribe = 0;
static uintptr_t a_PlannerUpdate = 0;
static uintptr_t a_HutTD = 0;
static uintptr_t a_ToolTD = 0;
static uintptr_t a_SetConquered = 0;
static uintptr_t a_TribeGoalListener = 0;
static uintptr_t a_SpeciesSlot = 0;
static uintptr_t a_SetSpeciesSlot = 0;
static uintptr_t a_IsToolPurchased = 0;
static uintptr_t a_SetToolPurchased = 0;
static uintptr_t a_AddFood = 0;
static uintptr_t a_FoodFx = 0;
static uintptr_t a_GetCursor = 0;
static uintptr_t a_CursorSetObjects = 0;
static uintptr_t a_MinimapRebuild = 0;
static uintptr_t a_TribeUiGet = 0;
static uintptr_t a_UnlockToolNotify = 0;
static uintptr_t a_ToolSetAt = 0;
static uintptr_t a_RelMgrGet = 0;
static uintptr_t a_RelCalc = 0;
static uintptr_t a_RelCalcAbs = 0;
static uintptr_t a_RelApply = 0;
static uintptr_t a_RelLevel = 0;
static uintptr_t a_SetSpecies = 0;
static uintptr_t a_SetSpeciesProfile = 0;
static uintptr_t a_SetChieftain = 0;
static uintptr_t a_CreateHerd = 0;
static uintptr_t a_HerdEnable = 0;
static uintptr_t a_CreatureCreate = 0;
static uintptr_t a_GetDomHerd = 0;

constexpr uint32_t kTribeNoun = 0x18C6D19u;
constexpr uint32_t kCitizenNoun = 0x18EB4B7u;
constexpr uint32_t kHutNoun = 0x1E4DAAEu;
constexpr uint32_t kToolNoun = 0x18C8F0Cu;
constexpr uint32_t kFoodMatNoun = 0x0629BAFEu; // BUG-1573: cTribe +0x260 mpFoodMat (action 0x21 RaidFood)
constexpr uint32_t kRelEventSocial = 0x0530CF07u;
constexpr uint32_t kRelEventGift = 0x0530CF02u;
constexpr uint32_t kMsgPopDestroyed = 0x05C80207u; // BUG-1571: TribeGame_GoalListener PopDestroyed -> TRG_PRE_PopDestroyed
constexpr uint32_t kMsgHutDestroyed = 0x058BADDDu; // BUG-1571: TribeGame_GoalListener HutDestroyed -> TRG_PRE_HutDestroyed
constexpr uintptr_t kProfKeyOff = 0x504;
constexpr uintptr_t kProfGenOff = 0x53C;
constexpr uintptr_t kSpKeyOff = 0xB28;

template <typename T>
static T& F(void* p, uintptr_t off)
{
    return *(T*)((uint8_t*)p + off);
}

static bool is_host() { return S && S->connected() && S->role() == net::Role::Host; }
static bool is_guest() { return S && S->connected() && S->role() == net::Role::Client; }
static uint8_t my_id() { return S ? S->my_id() : 0; }

bool enabled() { return g_rivals_tribe_fix && rivals::enabled(); }

bool active()
{
    return g_rivals_tribe_fix && rivals::active_in_stage() && game::mode_id() == modes::kTribe && S && S->connected();
}

static uint32_t tribe_pol(void* t)
{
    if (!t || !skg::readable(t, 0x34))
        return 0;
    return F<uint32_t>(t, 0x30);
}

static bool tribe_conquered(void* t)
{
    return t && skg::readable(t, 0x558) && F<uint8_t>(t, 0x556) != 0;
}

static int32_t tribe_food(void* t)
{
    if (!t || !skg::readable(t, 0x230))
        return 0;
    return vcall<int>(t, 0xA4 / 4);
}

static void tribe_add_food(void* t, int32_t delta)
{
    if (!t || !delta || !a_AddFood || !skg::readable(t, 0x230))
        return;
    int32_t cur = tribe_food(t);
    if (delta < 0 && -delta > cur)
        delta = -cur;
    if (!delta)
        return;
    thiscall<void>(a_AddFood, t, (int)delta);
    if (a_FoodFx && t == tribe::player_tribe())
        game::tribe_food_visuals(t, (float)delta);
}

static void tribe_set_food(void* t, int32_t target)
{
    if (!t || target < 0)
        return;
    int32_t cur = tribe_food(t);
    if (target != cur)
        tribe_add_food(t, target - cur);
}

static uint32_t tribe_tools_mask(void* t)
{
    if (!t || !skg::readable(t, 0x19DC))
        return 0;
    uint32_t mask = 0;
    if (a_IsToolPurchased) {
        for (int k = kMinToolKind; k <= kMaxToolKind; k++)
            if (thiscall<bool>(a_IsToolPurchased, t, k))
                mask |= (1u << k);
    } else {
        mask = F<uint32_t>(t, 0x19D8) & 0x3FEu;
    }
    for (void* tool : tribe::tribe_tools(t)) {
        int k = tribe::tool_type(tool);
        if (k >= kMinToolKind && k <= kMaxToolKind)
            mask |= (1u << k);
    }
    return mask;
}

static void unlock_tool_local(int kind, void* t)
{
    if (kind < kMinToolKind || kind > kMaxToolKind)
        return;
    if (t && a_SetToolPurchased && skg::readable(t, 0x19DC))
        thiscall<void>(a_SetToolPurchased, t, kind);
    void* ui = a_TribeUiGet ? skg::cdecl_call<void*>(a_TribeUiGet) : nullptr;
    if (ui && a_ToolSetAt && skg::readable((uint8_t*)ui + 0xA8, 0x18)) {
        int k = kind;
        int* slot = thiscall<int*>(a_ToolSetAt, (uint8_t*)ui + 0xA8, &k);
        if (slot)
            *slot = 1;
        if (a_UnlockToolNotify)
            skg::cdecl_call<void>(a_UnlockToolNotify, kind);
    }
}

static void* rel_mgr()
{
    void* m = a_RelMgrGet ? skg::cdecl_call<void*>(a_RelMgrGet) : nullptr;
    return m && skg::readable(m, 0x24) ? m : nullptr;
}

static void rel_thresholds(void* mgr, float& t0, float& t1, float& t2, float& t3)
{
    t0 = -6.0f;
    t1 = -2.0f;
    t2 = 2.0f;
    t3 = 6.0f;
    if (mgr && skg::readable(mgr, 0x20)) {
        float a = F<float>(mgr, 0x10), b = F<float>(mgr, 0x14), c = F<float>(mgr, 0x18), d = F<float>(mgr, 0x1C);
        if (std::isfinite(a) && std::isfinite(b) && std::isfinite(c) && std::isfinite(d) && a < b && b < c && c < d) {
            t0 = a;
            t1 = b;
            t2 = c;
            t3 = d;
        }
    }
}

static float rel_value(void* mgr, uint32_t polA, uint32_t polB)
{
    if (!mgr || !a_RelCalc || !polA || !polB)
        return 0.0f;
    return thiscall<float>(a_RelCalc, mgr, polA, polB, 0);
}

static float rel_value_abs(void* mgr, uint32_t polA, uint32_t polB)
{
    if (!mgr || !a_RelCalcAbs || !polA || !polB)
        return rel_value(mgr, polA, polB);
    return thiscall<float>(a_RelCalcAbs, mgr, polA, polB, 0);
}

static int rel_level_of(void* mgr, float val)
{
    if (!mgr || !a_RelLevel)
        return 2;
    return thiscall<int>(a_RelLevel, mgr, val);
}

// ---- Runtime state ----
struct OwnedTribe {
    void* ptr = nullptr;
    uint32_t serial = 0;
    uint32_t pol = 0;
    net::Vec3 pos{};
    void* orig_slot = nullptr;
    uint32_t swapped_key[3] = {};
    bool species_swapped = false;
    int32_t last_sent_food = -1;
    uint32_t last_sent_tools = 0;
};

static OwnedTribe g_owned[rivals::kMaxPlayers];
static uint32_t g_assign_rev = 0;
static bool g_was_active = false;

// Guest perspective swap state (R1)
static std::vector<AssignEntry> g_guest_assign;
static void* g_orig_pt = nullptr;
static uint32_t g_orig_pt_serial = 0;
static void* g_my_tribe = nullptr;
static uint32_t g_my_tribe_serial = 0;
static bool g_swapped = false;
static bool g_save_temp_unswapped = false;
static std::vector<NpcRelEntry> g_guest_npc_rels;

// Host R4 per-player NPC relations & stage race state
static NpcRelations g_npc_rels;
static bool g_stage_won_reported = false;
static bool g_race_end_handled = false;
static double g_last_eval_at = 0.0;
static double g_last_guest_pvp_rel = 0.0;
static double g_last_assign_tx = 0.0;
static double g_last_food_tx = 0.0;
static double g_last_rel_tx = 0.0;
static double g_last_auto_form = 0.0;
static uint32_t g_auto_form_count = 0;
static double g_last_raid_at[rivals::kMaxPlayers] = {};

// Save-strip temporary backup for host species slots
struct SavedSlot {
    void* tribe = nullptr;
    uint32_t serial = 0;
    void* cur_slot = nullptr;
    void* orig_slot = nullptr;
};
static std::vector<SavedSlot> g_saved_slots;

// Persistence (store.cpp "tribe.rivals" v1): player stable key -> village position
struct StoredAssign {
    std::string key;
    float x = 0.f, y = 0.f, z = 0.f;
};
static std::vector<StoredAssign> g_stored_assign;
static uint32_t g_store_serial = 0;

// Diagnostics counters
static uint32_t g_planner_skipped = 0;
static uint32_t g_pvp_hits = 0;
static uint32_t g_pvp_blocked = 0;
static uint32_t g_hut_clamped = 0;
static uint32_t g_tool_pvp_hits = 0;
static uint32_t g_conquer_blocked = 0;
static uint32_t g_gifts_done = 0;
static uint32_t g_raids_done = 0;
static uint32_t g_inf_earned = 0;
static uint32_t g_draft_unlocked = 0;
static uint32_t g_draft_payouts_applied = 0;
static uint32_t g_species_swaps = 0;
static uint32_t g_pack_pets_spawned = 0; // BUG-1564
static uint32_t g_last_npc_count = 0;
static int g_last_win_needed = 1;

// BUG-1571: Tribe Rivals wipe recovery, PopDestroyed Game Over guard, and 5-minute PvP immunity
static PlayerWipeState g_wipe[rivals::kMaxPlayers];
static double g_wipe_respawn_delay = kDefaultWipeRespawnDelay;
static double g_wipe_immune_duration = kDefaultWipeImmunityDuration;
static double g_last_wipe_tick = 0.0;
static double g_last_immune_tx = 0.0;
static uint32_t g_pop_gameover_blocked = 0;
static uint32_t g_wipes_detected = 0;
static uint32_t g_wipes_recovered = 0;
static uint32_t g_immune_blocked = 0;
static uint32_t g_immune_broken = 0;
static uint32_t g_foodmat_blocked = 0;
static double g_last_hut_floor_notice = 0.0;

bool wipe_recovery_active() { return active() && g_wipe_recovery_fix; }

bool is_immune(uint8_t pid)
{
    if (!wipe_recovery_active() || !rivals::valid_player(pid))
        return false;
    double now = H ? H->time_seconds() : 0.0;
    return is_player_immune(g_wipe[pid], now);
}

static std::string fmt_mmss(double secs)
{
    int s = std::max(0, (int)std::ceil(secs));
    char b[24];
    snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}

static void host_broadcast_immunity(uint8_t pid, double now)
{
    if (!is_host() || !active() || !rivals::valid_player(pid))
        return;
    double rem = player_immune_remaining(g_wipe[pid], now);
    int32_t rem_s = rem > 0.0 ? (int32_t)std::ceil(rem) : 0;
    uint32_t flags = g_wipe[pid].wiping ? 1u : 0u;
    net::Writer w;
    write_food_sync(w, {CustomOp::Immunity, pid, rem_s, flags});
    rivals::broadcast_faction(rivals::Stage::Tribe, wire::rivals_faction::SK_STAGE_CUSTOM, w);
}

void note_pvp_aggression(uint8_t sender_pid, uint8_t target_pid)
{
    if (!wipe_recovery_active() || !is_host() || !rivals::valid_player(sender_pid) ||
        !rivals::valid_player(target_pid) || sender_pid == target_pid)
        return;
    double now = H ? H->time_seconds() : 0.0;
    if (is_player_immune(g_wipe[target_pid], now))
        return;
    if (break_immunity_on_attack(g_wipe[sender_pid], now)) {
        g_immune_broken++;
        host_broadcast_immunity(sender_pid, now);
        if (H && M)
            H->log(M, SK_INFO, "tribe_rivals: player %u attacked rival %u: PvP immunity ended early (BUG-1571)",
                   (unsigned)sender_pid, (unsigned)target_pid);
        if (tribe::C.notice && S)
            tribe::C.notice(S->name_of(sender_pid) + i18n::tr(" attacked a rival: PvP shield ended early!"));
    }
}

// BUG-1564: Creature -> Tribe pack pet handoff state (species preserved across Creature -> Tribe world change)
static PackPetPlan g_pack_pets;
static double g_last_pack_snap = 0.0;

static bool tribe_alive(void* t, uint32_t serial)
{
    return t && game::alive(game::LiveRef{t, serial, kTribeNoun});
}

static void* find_tribe_by_pol_or_pos(uint32_t pol, float x, float y, float z)
{
    auto ts = tribe::all_tribes();
    if (pol) {
        for (void* t : ts)
            if (tribe_pol(t) == pol)
                return t;
    }
    void* best = nullptr;
    float bd = 45.0f;
    net::Vec3 want{x, y, z};
    for (void* t : ts) {
        float d = net::dist(tribe::tribe_pos(t), want);
        if (d < bd) {
            bd = d;
            best = t;
        }
    }
    return best;
}

static void* orig_pt_live()
{
    if (g_orig_pt && tribe_alive(g_orig_pt, g_orig_pt_serial))
        return g_orig_pt;
    return nullptr;
}

bool perspective_swapped() { return g_swapped; }
void* orig_player_tribe() { return orig_pt_live(); }

uint8_t owner_of_tribe(void* t)
{
    if (!t || !active())
        return kNoOwner;
    if (is_host()) {
        for (int p = 0; p < rivals::kMaxPlayers; p++) {
            if (g_owned[p].ptr == t && tribe_alive(t, g_owned[p].serial))
                return (uint8_t)p;
        }
        if (t == tribe::player_tribe())
            return 0;
        return kNoOwner;
    }
    if (is_guest()) {
        uint32_t tp = tribe_pol(t);
        net::Vec3 pos = tribe::tribe_pos(t);
        for (const auto& e : g_guest_assign) {
            if ((e.tribe_pol && tp == e.tribe_pol) || net::dist(pos, {e.x, e.y, e.z}) < 25.0f)
                return e.player_id;
        }
        if (g_swapped && t == g_my_tribe && tribe_alive(g_my_tribe, g_my_tribe_serial))
            return my_id();
        if (g_swapped && t == orig_pt_live())
            return 0;
    }
    return kNoOwner;
}

void* tribe_of_player(uint8_t pid)
{
    if (!rivals::valid_player(pid) || !active())
        return nullptr;
    if (is_host()) {
        if (g_owned[pid].ptr && tribe_alive(g_owned[pid].ptr, g_owned[pid].serial))
            return g_owned[pid].ptr;
        if (pid == 0)
            return tribe::player_tribe();
        return nullptr;
    }
    if (is_guest()) {
        if (pid == my_id() && g_swapped && tribe_alive(g_my_tribe, g_my_tribe_serial))
            return g_my_tribe;
        for (const auto& e : g_guest_assign) {
            if (e.player_id == pid)
                return find_tribe_by_pol_or_pos(e.tribe_pol, e.x, e.y, e.z);
        }
    }
    return nullptr;
}

bool is_player_owned_tribe(void* t) { return rivals::valid_player(owner_of_tribe(t)); }

static uint8_t owner_of_pol(uint32_t pol)
{
    if (!pol || !active())
        return kNoOwner;
    if (is_host()) {
        for (int p = 0; p < rivals::kMaxPlayers; p++)
            if (g_owned[p].ptr && tribe_alive(g_owned[p].ptr, g_owned[p].serial) && g_owned[p].pol == pol)
                return (uint8_t)p;
        if (void* pt = tribe::player_tribe(); pt && tribe_pol(pt) == pol)
            return 0;
    }
    return kNoOwner;
}

// ---- Hooks ----
using PlannerUpdate_t = void(SK_THISCALL*)(void*, uint32_t, uint32_t);
static PlannerUpdate_t o_PlannerUpdate = nullptr;
static void SK_THISCALL h_PlannerUpdate(void* planner, uint32_t dt_lo, uint32_t dt_hi)
{
    if (active() && planner && skg::readable(planner, 0x78)) {
        void* t = F<void*>(planner, 0x74);
        if (t && is_player_owned_tribe(t)) {
            g_planner_skipped++;
            return; // R2: disable NPC planner AI for player-owned tribes without mutating +0x290
        }
    }
    o_PlannerUpdate(planner, dt_lo, dt_hi);
}

using TakeDamage_t = int(SK_THISCALL*)(void*, float, uint32_t, int, const void*, void*);
static TakeDamage_t o_HutTD = nullptr;
static TakeDamage_t o_ToolTD = nullptr;

static uint8_t attacker_owner_of(void* att_comb, uint32_t att_pol)
{
    if (att_comb && !IsBadReadPtr(att_comb, 4)) {
        void* obj = vcall<void*>(att_comb, 0x0C / 4);
        if (obj && tribe::is_citizen(obj)) {
            void* at = F<void*>(obj, 0x1014);
            uint8_t o = owner_of_tribe(at);
            if (rivals::valid_player(o))
                return o;
        }
    }
    if (att_pol && att_pol != 0xFFFFFFFFu) {
        uint8_t o = owner_of_pol(att_pol);
        if (rivals::valid_player(o))
            return o;
    }
    return kNoOwner;
}

static void host_try_war_food_raid(uint8_t att, uint8_t vic, int32_t want_amount);

static int SK_THISCALL h_HutTD(void* comb, float dmg, uint32_t pol, int type, const void* pos, void* att)
{
    if (active() && is_host() && comb && skg::readable(comb, 0x114)) {
        void* vt = F<void*>(comb, 0x110); // hut+0x230 = mpTribe
        uint8_t vo = owner_of_tribe(vt);
        uint8_t ao = attacker_owner_of(att, pol);
        bool war = rivals::valid_player(ao) && rivals::valid_player(vo) && rivals::at_war(ao, vo);
        bool vic_imm = is_immune(vo);
        auto dec = decide_damage(true, ao, vo, war, true, vic_imm);
        if (dec == DamageDecision::Block) {
            g_pvp_blocked++;
            if (war && vic_imm)
                g_immune_blocked++;
            return 0;
        }
        if (dec == DamageDecision::ClampMainHut) {
            if (war && rivals::valid_player(ao) && rivals::valid_player(vo))
                note_pvp_aggression(ao, vo);
            float cur_hp = F<float>(comb, 0x38);
            float max_hp = vcall<float>(comb, 0x58 / 4);
            if (!std::isfinite(max_hp) || max_hp <= 0.0f)
                max_hp = std::max(cur_hp, 100.0f);
            float clamped = clamp_main_hut_damage(dmg, cur_hp, max_hp);
            if (clamped < dmg - 1e-4f)
                g_hut_clamped++;
            if (war && rivals::valid_player(ao) && rivals::valid_player(vo)) {
                double now = H ? H->time_seconds() : 0.0;
                if (now - g_last_raid_at[ao] >= 3.0) {
                    g_last_raid_at[ao] = now;
                    host_try_war_food_raid(ao, vo, 5);
                }
            }
            if (clamped <= 0.0f) {
                if (g_tribe_polish_fix && war && rivals::valid_player(ao) && rivals::valid_player(vo) && tribe::C.notice) {
                    double now = H ? H->time_seconds() : 0.0;
                    if (now - g_last_hut_floor_notice >= 30.0) {
                        g_last_hut_floor_notice = now;
                        tribe::C.notice(i18n::tr("Rival main hut is protected from destruction (attacks raid food instead)."));
                    }
                }
                return 0;
            }
            dmg = clamped;
        }
    }
    return o_HutTD(comb, dmg, pol, type, pos, att);
}

static int SK_THISCALL h_ToolTD(void* comb, float dmg, uint32_t pol, int type, const void* pos, void* att)
{
    if (active() && is_host() && comb && skg::readable(comb, 0xE0)) {
        void* vt = F<void*>(comb, 0xD8); // tool+0x1F8 = mpTribe
        uint8_t vo = owner_of_tribe(vt);
        uint8_t ao = attacker_owner_of(att, pol);
        bool war = rivals::valid_player(ao) && rivals::valid_player(vo) && rivals::at_war(ao, vo);
        bool vic_imm = is_immune(vo);
        auto dec = decide_damage(true, ao, vo, war, false, vic_imm);
        if (dec == DamageDecision::Block) {
            g_pvp_blocked++;
            if (war && vic_imm)
                g_immune_blocked++;
            return 0;
        }
        if (dec == DamageDecision::AllowPvp) {
            note_pvp_aggression(ao, vo);
            g_tool_pvp_hits++;
        }
    }
    return o_ToolTD(comb, dmg, pol, type, pos, att);
}

// BUG-1571: Block native TRG_PRE_PopDestroyed / TRG_PRE_HutDestroyed Game Over handler in TribeGame_GoalListener
// (0x00CD81F0) while Tribe Rivals is active, on both host and guest, so a full tribe wipe triggers respawn instead
// of hiding the HUD and reloading the save.
using GoalListener_t = bool(SK_THISCALL*)(void*, uint32_t, void*);
static GoalListener_t o_TribeGoalListener = nullptr;
static bool SK_THISCALL h_TribeGoalListener(void* self, uint32_t msg_id, void* data)
{
    if (wipe_recovery_active() && (msg_id == kMsgPopDestroyed || msg_id == kMsgHutDestroyed)) {
        g_pop_gameover_blocked++;
        if (H && M)
            H->log(M, SK_INFO, "tribe_rivals: blocked native Tribe Game Over msg 0x%08X (#%u, BUG-1571)", msg_id,
                   g_pop_gameover_blocked);
        return false;
    }
    return o_TribeGoalListener(self, msg_id, data);
}

using SetConquered_t = void(SK_THISCALL*)(void*, bool);
static SetConquered_t o_SetConquered = nullptr;
static void SK_THISCALL h_SetConquered(void* t, bool conquered)
{
    if (conquered && active()) {
        uint8_t o = owner_of_tribe(t);
        if (!can_conquer_tribe(true, o)) {
            g_conquer_blocked++;
            if (H && M)
                H->log(M, SK_INFO, "tribe_rivals: blocked conquest of player %u main village %p (R7)", (unsigned)o, t);
            return;
        }
        if (is_host() && o == kNoOwner && t) {
            uint32_t np = tribe_pol(t);
            // Attribute conquest to player whose members are closest to the village
            net::Vec3 vp = tribe::tribe_pos(t);
            uint8_t best_p = 0;
            float best_d = 1e9f;
            for (int p = 0; p < rivals::kMaxPlayers; p++) {
                void* pt = tribe_of_player((uint8_t)p);
                if (!pt)
                    continue;
                for (void* m : tribe::members(pt)) {
                    float d = net::dist(game::position(m), vp);
                    if (d < best_d) {
                        best_d = d;
                        best_p = (uint8_t)p;
                    }
                }
            }
            if (np) {
                bool already = g_npc_rels.get(best_p, np) == NpcRel::Conquered;
                g_npc_rels.mark_conquered(best_p, np);
                if (!already && best_p != 0)
                    history::record_tribe_outcome(best_p, false);
            }
        }
    }
    o_SetConquered(t, conquered);
}

using ApplyRel_t = float(SK_THISCALL*)(void*, uint32_t, uint32_t, uint32_t, float);
static ApplyRel_t o_ApplyRel = nullptr;
static bool g_in_internal_rel = false;
CALL_GUARD("tribe_rivals.internal_rel", g_in_internal_rel);

static void apply_rel_internal(void* mgr, uint32_t polA, uint32_t polB, NpcRel want)
{
    if (!mgr || !polA || !polB || !o_ApplyRel)
        return;
    float t0, t1, t2, t3;
    rel_thresholds(mgr, t0, t1, t2, t3);
    float target = rel_target_float(want, t0, t1, t2, t3);
    float cur_abs = rel_value_abs(mgr, polA, polB);
    float cur = rel_value(mgr, polA, polB);
    float base = std::isfinite(cur_abs) ? cur_abs : cur;
    float delta = target - base;
    if (!std::isfinite(delta) || std::fabs(delta) < 0.05f)
        return;
    callguard::Hold hold(g_in_internal_rel);
    (void)o_ApplyRel(mgr, polA, polB, kRelEventSocial, delta);
}

static float SK_THISCALL h_ApplyRel(void* mgr, uint32_t polA, uint32_t polB, uint32_t eventId, float scale)
{
    float res = o_ApplyRel(mgr, polA, polB, eventId, scale);
    if (!g_in_internal_rel && active() && is_host() && scale > 0.0f &&
        (eventId == kRelEventSocial || eventId == kRelEventGift)) {
        uint8_t actor = owner_of_pol(polB);
        uint8_t target_owner = owner_of_pol(polA);
        if (rivals::valid_player(actor) && target_owner == kNoOwner && polA != 0) {
            int32_t gain = (eventId == kRelEventSocial) ? kInfluenceMusicGain : kInfluenceGiftGain;
            rivals::grant_influence(actor, gain, eventId == kRelEventSocial ? "tribe music" : "tribe gift");
            g_inf_earned++;
            int lvl = rel_level_of(mgr, rel_value(mgr, polA, polB));
            NpcRel prev = g_npc_rels.get(actor, polA);
            NpcRel next = rel_from_game_level(lvl, false);
            g_npc_rels.set(actor, polA, next);
            if (actor != 0 && prev != NpcRel::Allied && next == NpcRel::Allied)
                history::record_tribe_outcome(actor, true);
        }
    }
    return res;
}

// ---- War PvP citizen filter & choose action override ----
bool filter_citizen_damage(void* victim, void* attacker)
{
    if (!active() || !is_host() || !victim)
        return true;
    void* vt = F<void*>(victim, 0x1014);
    void* at = attacker && tribe::is_citizen(attacker) ? F<void*>(attacker, 0x1014) : nullptr;
    uint8_t vo = owner_of_tribe(vt);
    uint8_t ao = owner_of_tribe(at);
    bool war = rivals::valid_player(ao) && rivals::valid_player(vo) && rivals::at_war(ao, vo);
    bool vic_imm = is_immune(vo);
    auto dec = decide_damage(true, ao, vo, war, false, vic_imm);
    if (dec == DamageDecision::Block) {
        g_pvp_blocked++;
        if (war && vic_imm)
            g_immune_blocked++;
        return false;
    }
    if (dec == DamageDecision::AllowPvp) {
        note_pvp_aggression(ao, vo);
        g_pvp_hits++;
    }
    return true;
}

int override_choose_action(void* target_obj, int vanilla_action)
{
    if (!active() || !target_obj || IsBadReadPtr(target_obj, 4))
        return 0;
    void* gd = vcall<void*>(target_obj, 0xC / 4, (uint32_t)0x17F243Bu);
    if (!gd || !game::listed(gd, 0))
        return 0;
    uint32_t noun = game::noun_id(gd);
    void* target_tribe = nullptr;
    if (noun == kCitizenNoun)
        target_tribe = F<void*>(gd, 0x1014);
    else if (noun == kHutNoun)
        target_tribe = F<void*>(gd, 0x230);
    else if (noun == kToolNoun)
        target_tribe = F<void*>(gd, 0x1F8);
    else if (g_tribe_polish_fix && noun == kFoodMatNoun) {
        for (void* t : tribe::all_tribes()) {
            if (t && skg::readable(t, 0x264) && F<void*>(t, 0x260) == gd) {
                target_tribe = t;
                break;
            }
        }
    }
    if (!target_tribe)
        return 0;
    uint8_t to = owner_of_tribe(target_tribe);
    uint8_t me = my_id();
    if (!rivals::valid_player(to) || to == me)
        return 0;
    if (!rivals::at_war(me, to) || is_immune(to)) {
        // Block hostile actions (0x0A attack, 0x0C raid, 0x21 raid food) on Neutral / Allied / immune rival tribes
        if (vanilla_action == 0x0A || vanilla_action == 0x0C || vanilla_action == 0x21) {
            if (noun == kFoodMatNoun)
                g_foodmat_blocked++;
            return -1;
        }
        return 0;
    }
    // At War and target not immune: allow attack on rival citizen, raid on rival hut/tool/food mat
    if (noun == kCitizenNoun && !game::is_dead(gd))
        return 0x0A;
    if (noun == kHutNoun || noun == kToolNoun)
        return 0x0C;
    if (g_tribe_polish_fix && noun == kFoodMatNoun)
        return 0x21;
    return 0;
}

// ---- R2: Species swap for taken-over NPC tribe ----
static void* species_profile_of(const creations::Key& k)
{
    if (!k.instance)
        return nullptr;
    creations::Key lk = creations::local_species(k);
    void* p = game::species_by_key({lk.instance, lk.type, lk.group});
    if (!p)
        p = game::species_by_key({k.instance, k.type, k.group});
    return p && skg::readable(p, kProfGenOff + 4) ? p : nullptr;
}

static bool swap_tribe_species(OwnedTribe& ot, void* prof)
{
    if (!ot.ptr || !prof || !tribe_alive(ot.ptr, ot.serial))
        return false;
    if (!a_SpeciesSlot || !a_SetSpeciesSlot)
        return false;
    if (!ot.orig_slot)
        ot.orig_slot = thiscall<void*>(a_SpeciesSlot, ot.ptr, 0);
    thiscall<void>(a_SetSpeciesSlot, ot.ptr, prof, 0);
    uint32_t* pk = &F<uint32_t>(prof, kProfKeyOff);
    ot.swapped_key[0] = pk[0];
    ot.swapped_key[1] = pk[1];
    ot.swapped_key[2] = pk[2];
    ot.species_swapped = true;
    g_species_swaps++;
    // Swap existing adult members & chief to the guest's species
    if (!world::cinematic_playing()) {
        for (void* c : tribe::members(ot.ptr)) {
            if (!c || game::is_dead(c) || F<int>(c, 0xB34) != 1) // skip babies (BUG-1313)
                continue;
            if (F<uint32_t>(c, kSpKeyOff) == pk[0])
                continue;
            if (a_SetSpecies)
                thiscall<bool>(a_SetSpecies, c, prof, 1, true);
            else if (a_SetSpeciesProfile)
                thiscall<void>(a_SetSpeciesProfile, c, prof);
            if (g_tribe_polish_fix)
                tribe::rebuild_member_hand(c); // BUG-1573: re-attach chieftain staff/held tool to new species bones
        }
    }
    // Ensure tribe has a chieftain
    if (a_SetChieftain && skg::readable(ot.ptr, 0x120) && !F<void*>(ot.ptr, 0x11C)) {
        for (void* c : tribe::members(ot.ptr)) {
            if (c && !game::is_dead(c) && F<int>(c, 0xB34) == 1) {
                thiscall<void>(a_SetChieftain, ot.ptr, c);
                break;
            }
        }
    }
    return true;
}

static void restore_host_species_slots()
{
    if (!a_SetSpeciesSlot)
        return;
    for (int p = 1; p < rivals::kMaxPlayers; p++) {
        auto& ot = g_owned[p];
        if (ot.species_swapped && ot.orig_slot && ot.ptr && tribe_alive(ot.ptr, ot.serial)) {
            thiscall<void>(a_SetSpeciesSlot, ot.ptr, ot.orig_slot, 0);
            ot.species_swapped = false;
        }
    }
}

// ---- R1: Guest perspective swap & clean restore ----
static void set_cursor_tribe_list(void* t)
{
    if (!t || !a_GetCursor || !a_CursorSetObjects)
        return;
    void* cur = skg::cdecl_call<void*>(a_GetCursor);
    if (cur && skg::readable(cur, 0x168))
        thiscall<void>(a_CursorSetObjects, cur, (uint8_t*)t + 0x354);
}

static void rebuild_minimap()
{
    if (!a_TribeUiGet || !a_MinimapRebuild)
        return;
    void* ui = skg::cdecl_call<void*>(a_TribeUiGet);
    void* game_ui = ui && skg::readable(ui, 0x244) ? F<void*>(ui, 0x240) : nullptr;
    if (game_ui && skg::readable(game_ui, 0x100))
        thiscall<void>(a_MinimapRebuild, game_ui);
}

static void restore_guest_perspective(const char* why)
{
    if (!g_swapped)
        return;
    void* orig = orig_pt_live();
    void* nm = game::noun_manager();
    if (orig && nm && a_SetPlayerTribe && game::mode_id() == modes::kTribe) {
        thiscall<void>(a_SetPlayerTribe, nm, orig);
        set_cursor_tribe_list(orig);
        rebuild_minimap();
        if (H && M)
            H->log(M, SK_INFO, "tribe_rivals: restored guest perspective to original tribe %p (%s)", orig, why);
    }
    g_swapped = false;
}

static void apply_guest_perspective()
{
    if (!active() || !is_guest() || !a_SetPlayerTribe || g_save_temp_unswapped)
        return;
    uint8_t me = my_id();
    const AssignEntry* mine = nullptr;
    for (const auto& e : g_guest_assign) {
        if (e.player_id == me) {
            mine = &e;
            break;
        }
    }
    if (!mine)
        return;
    if (!orig_pt_live()) {
        void* pt = tribe::player_tribe();
        if (pt) {
            g_orig_pt = pt;
            g_orig_pt_serial = game::object_serial(pt);
        }
    }
    void* target = find_tribe_by_pol_or_pos(mine->tribe_pol, mine->x, mine->y, mine->z);
    if (!target || target == orig_pt_live())
        return;
    g_my_tribe = target;
    g_my_tribe_serial = game::object_serial(target);
    if (tribe::player_tribe() != target) {
        void* nm = game::noun_manager();
        if (nm) {
            thiscall<void>(a_SetPlayerTribe, nm, target);
            set_cursor_tribe_list(target);
            rebuild_minimap();
            g_swapped = true;
            if (H && M)
                H->log(M, SK_INFO, "tribe_rivals: guest perspective swapped to tribe %p (pol %08X, orig %p)", target,
                       tribe_pol(target), g_orig_pt);
        }
    } else {
        g_swapped = true;
    }
}

// ---- Wire send helpers (MSG_RIVALS_FACTION = 81, Stage::Tribe) ----
static void host_broadcast_assign()
{
    if (!is_host() || !active())
        return;
    std::vector<AssignEntry> list;
    for (int p = 0; p < rivals::kMaxPlayers; p++) {
        auto& ot = g_owned[p];
        if (!ot.ptr || !tribe_alive(ot.ptr, ot.serial))
            continue;
        net::Vec3 pos = tribe::tribe_pos(ot.ptr);
        ot.pos = pos;
        AssignEntry e;
        e.player_id = (uint8_t)p;
        e.tribe_pol = ot.pol;
        e.x = pos.x;
        e.y = pos.y;
        e.z = pos.z;
        e.sp_inst = ot.swapped_key[0];
        e.sp_type = ot.swapped_key[1];
        e.sp_group = ot.swapped_key[2];
        list.push_back(e);
    }
    if (list.empty())
        return;
    g_assign_rev++;
    net::Writer w;
    write_assign(w, g_assign_rev, list);
    rivals::broadcast_faction(rivals::Stage::Tribe, wire::rivals_faction::SK_ASSIGN, w);
}

static void host_send_food(uint8_t pid, bool force = false)
{
    if (!is_host() || !active() || pid == 0 || !rivals::valid_player(pid))
        return;
    auto& ot = g_owned[pid];
    if (!ot.ptr || !tribe_alive(ot.ptr, ot.serial))
        return;
    int32_t food = tribe_food(ot.ptr);
    uint32_t tools = tribe_tools_mask(ot.ptr);
    if (!force && food == ot.last_sent_food && tools == ot.last_sent_tools)
        return;
    ot.last_sent_food = food;
    ot.last_sent_tools = tools;
    net::Writer w;
    write_food_sync(w, {CustomOp::FoodTotal, pid, food, tools});
    rivals::send_faction_to(pid, rivals::Stage::Tribe, wire::rivals_faction::SK_STAGE_CUSTOM, w);
}

static void host_sync_npc_rels_to_guests()
{
    if (!is_host() || !active() || !S)
        return;
    std::vector<void*> npcs;
    for (void* t : tribe::all_tribes())
        if (!is_player_owned_tribe(t))
            npcs.push_back(t);
    for (const auto& pl : S->players()) {
        if (pl.id == 0 || !rivals::valid_player(pl.id))
            continue;
        std::vector<NpcRelEntry> list;
        for (void* t : npcs) {
            uint32_t np = tribe_pol(t);
            if (!np)
                continue;
            net::Vec3 p = tribe::tribe_pos(t);
            NpcRel r = g_npc_rels.get(pl.id, np);
            bool prot = rivals::is_npc_protected(pl.id, np);
            list.push_back({np, p.x, p.y, p.z, r, prot});
        }
        net::Writer w;
        write_npc_rels(w, pl.id, list);
        rivals::send_faction_to(pl.id, rivals::Stage::Tribe, wire::rivals_faction::SK_NPC_RELS, w);
    }
}

static void host_try_war_food_raid(uint8_t att, uint8_t vic, int32_t want_amount)
{
    if (!is_host() || !active() || !rivals::at_war(att, vic) || want_amount <= 0)
        return;
    bool vic_imm = is_immune(vic);
    if (vic_imm) {
        g_immune_blocked++;
        return;
    }
    void* at = tribe_of_player(att);
    void* vt = tribe_of_player(vic);
    if (!at || !vt)
        return;
    int32_t avail = tribe_food(vt);
    int32_t steal = raid_steal_amount(avail, want_amount, vic_imm, g_wipe_recovery_fix);
    if (steal <= 0)
        return;
    note_pvp_aggression(att, vic);
    tribe_add_food(vt, -steal);
    tribe_add_food(at, steal);
    g_raids_done++;
    host_send_food(att, true);
    host_send_food(vic, true);
    net::Writer w;
    write_pvp_hit(w, {att, vic, PvpHitKind::RaidFood, 0, (float)steal});
    rivals::broadcast_faction(rivals::Stage::Tribe, wire::rivals_faction::SK_PVP_HIT, w);
    if (tribe::C.notice && S)
        tribe::C.notice(S->name_of(att) + i18n::tr(" raided ") + std::to_string(steal) + i18n::tr(" food from ") +
                        S->name_of(vic));
}

static std::string host_apply_gift(uint8_t from, uint8_t to, GiftKind kind, int32_t value)
{
    if (!is_host() || !active())
        return "not host or tribe rivals inactive";
    void* ft = tribe_of_player(from);
    void* tt = tribe_of_player(to);
    if (!ft || !tt)
        return "player tribe not found";
    int32_t sf = tribe_food(ft);
    uint32_t st = tribe_tools_mask(ft);
    auto chk = validate_gift(true, rivals::allied(from, to), from, to, kind, value, sf, st);
    if (!chk.ok)
        return chk.err;
    if (kind == GiftKind::Food) {
        tribe_add_food(ft, -value);
        tribe_add_food(tt, value);
        host_send_food(from, true);
        host_send_food(to, true);
    } else if (kind == GiftKind::Tool) {
        if (to == 0)
            unlock_tool_local(value, tt);
        else {
            if (a_SetToolPurchased && skg::readable(tt, 0x19DC))
                thiscall<void>(a_SetToolPurchased, tt, value);
            net::Writer w;
            write_draft_apply(w, {to, DraftApplyKind::ToolUnlock, (uint32_t)value});
            rivals::send_faction_to(to, rivals::Stage::Tribe, wire::rivals_faction::SK_DRAFT_APPLY, w);
        }
    }
    g_gifts_done++;
    if (tribe::C.notice && S) {
        std::string msg = S->name_of(from) + " gifted " +
                          (kind == GiftKind::Food ? std::to_string(value) + " food" : std::string(tribe_tool_name(value))) +
                          " to " + S->name_of(to);
        tribe::C.notice(msg);
    }
    return "";
}

static std::string request_gift(uint8_t to, GiftKind kind, int32_t value)
{
    if (!active())
        return "tribe rivals inactive";
    if (is_host())
        return host_apply_gift(0, to, kind, value);
    if (is_guest()) {
        net::Writer w;
        write_gift(w, {my_id(), to, kind, value});
        if (!rivals::send_faction_to(0, rivals::Stage::Tribe, wire::rivals_faction::SK_GIFT, w))
            return "failed to send gift request";
        return "";
    }
    return "not connected";
}

// ---- R3 forced transition helper (unlocks host Tribe -> Civ advance button) ----
static void host_unlock_stage_advance()
{
    uintptr_t get_mm = H ? H->sym("App::IMessageManager::Get") : 0;
    void* nm = game::noun_manager();
    void* p = nm && skg::readable((uint8_t*)nm + 0x74, 4) ? F<void*>(nm, 0x74) : nullptr;
    void* mm = get_mm ? skg::cdecl_call<void*>(get_mm) : nullptr;
    if (!p || !mm || !skg::readable((uint8_t*)p + 0x10F0, 8))
        return;
    float& done = *(float*)((uint8_t*)p + 0x10F0);
    float& need = *(float*)((uint8_t*)p + 0x10F4);
    if (need < 1.0f)
        need = 1.0f;
    done = need;
    vcall<void>(mm, 0x18 / 4, 0x5CC2589u, (void*)nullptr, (void*)nullptr, 0);
    if (H && M)
        H->log(M, SK_INFO, "tribe_rivals: unlocked Tribe->Civ advance button (R3 forced transition)");
}

// ---- Callbacks registered with rivals.cpp ----
static bool on_npc_influence(rivals::Stage stage, rivals::InfluenceAction action, uint8_t actor, uint32_t npc_id,
                             uint8_t rival_id)
{
    if (stage != rivals::Stage::Tribe || !active() || !is_host())
        return false;
    void* npc = find_tribe_by_pol_or_pos(npc_id, 0, 0, 0);
    if (!npc || is_player_owned_tribe(npc))
        return false; // must target a remaining NPC tribe
    uint32_t np = tribe_pol(npc);
    if (!np)
        return false;
    void* mgr = rel_mgr();
    if (action == rivals::InfluenceAction::TurnAgainst) {
        if (!g_npc_rels.step_down(rival_id, np))
            return false;
        void* rt = tribe_of_player(rival_id);
        if (rt && mgr)
            apply_rel_internal(mgr, np, tribe_pol(rt), g_npc_rels.get(rival_id, np));
        host_sync_npc_rels_to_guests();
        return true;
    }
    if (action == rivals::InfluenceAction::Protect) {
        host_sync_npc_rels_to_guests();
        return true;
    }
    if (action == rivals::InfluenceAction::SmoothOver) {
        NpcRel prev = g_npc_rels.get(actor, np);
        if (!g_npc_rels.step_up(actor, np))
            return false;
        if (actor != 0 && prev != NpcRel::Allied && g_npc_rels.get(actor, np) == NpcRel::Allied)
            history::record_tribe_outcome(actor, true);
        void* at = tribe_of_player(actor);
        if (at && mgr)
            apply_rel_internal(mgr, np, tribe_pol(at), g_npc_rels.get(actor, np));
        host_sync_npc_rels_to_guests();
        return true;
    }
    return false;
}

static bool on_buy_influence(uint8_t actor, int32_t price, int32_t /*gain*/)
{
    if (!active() || !is_host())
        return true; // allow fallback outside Tribe Rivals
    void* t = tribe_of_player(actor);
    if (!t)
        return false;
    int32_t food = tribe_food(t);
    if (food < price)
        return false;
    tribe_add_food(t, -price);
    if (actor != 0)
        host_send_food(actor, true);
    return true;
}

static void on_draft_pick(uint8_t actor, uint32_t item_id, uint8_t /*kind*/, const std::string& /*label*/)
{
    if (!active() || !is_host())
        return;
    int tool = (int)item_id;
    if (tool < kMinToolKind || tool > kMaxToolKind)
        return;
    void* t = tribe_of_player(actor);
    if (actor == 0) {
        unlock_tool_local(tool, t);
        g_draft_unlocked++;
    } else {
        if (t && a_SetToolPurchased && skg::readable(t, 0x19DC))
            thiscall<void>(a_SetToolPurchased, t, tool);
        net::Writer w;
        write_draft_apply(w, {actor, DraftApplyKind::ToolUnlock, (uint32_t)tool});
        rivals::send_faction_to(actor, rivals::Stage::Tribe, wire::rivals_faction::SK_DRAFT_APPLY, w);
        g_draft_unlocked++;
    }
}

static void on_stage_race_end(uint8_t winner)
{
    if (!active() || !is_host() || g_race_end_handled)
        return;
    g_race_end_handled = true;
    void* wt = tribe_of_player(winner);
    uint32_t mask = wt ? tribe_tools_mask(wt) : 0u;
    auto items = build_tribe_draft_items(mask);
    rivals::start_draft(winner, items);
    host_unlock_stage_advance();
}

static std::vector<rivals::UiRow> on_extra_ui_rows()
{
    std::vector<rivals::UiRow> rows;
    if (!active() || !S)
        return rows;
    uint8_t me = my_id();
    void* mt = tribe_of_player(me);
    char b[160];
    snprintf(b, sizeof b, "Tribe Rivals: own food=%d, NPCs=%u (need %d)", mt ? tribe_food(mt) : 0, g_last_npc_count,
             g_last_win_needed);
    rows.push_back({b, "", {}, 0});
    if (wipe_recovery_active()) {
        double now = H ? H->time_seconds() : 0.0;
        for (const auto& p : S->players()) {
            if (!rivals::valid_player(p.id))
                continue;
            double rem = player_immune_remaining(g_wipe[p.id], now);
            if (rem > 0.0) {
                std::string label =
                    (p.id == me ? std::string(i18n::tr("  Your PvP shield: "))
                                : ("  " + p.name + i18n::tr(" PvP shield: "))) +
                    fmt_mmss(rem) + (g_wipe[p.id].wiping ? i18n::tr(" (respawning...)") : "");
                rows.push_back({label, "", {}, 0xFF60E0FFu});
            }
        }
    }
    for (const auto& p : S->players()) {
        if (p.id == me || !rivals::valid_player(p.id))
            continue;
        if (rivals::allied(me, p.id)) {
            uint8_t to = p.id;
            rows.push_back({std::string(i18n::tr("  Ally gift to ")) + p.name, i18n::tr("Gift 25 food"),
                            [to] { (void)request_gift(to, GiftKind::Food, 25); }, 0});
        }
    }
    return rows;
}

static std::vector<rivals::NpcUiEntry> on_npc_ui_list(uint8_t viewer)
{
    std::vector<rivals::NpcUiEntry> out;
    if (!active() || !rivals::valid_player(viewer))
        return out;
    std::vector<NpcUiSource> src;
    if (is_host()) {
        for (void* t : tribe::all_tribes()) {
            if (is_player_owned_tribe(t))
                continue;
            uint32_t np = tribe_pol(t);
            if (!np)
                continue;
            src.push_back({np, g_npc_rels.get(viewer, np), tribe_conquered(t), ""});
        }
    } else if (is_guest()) {
        for (const auto& e : g_guest_npc_rels) {
            if (!e.npc_pol)
                continue;
            src.push_back({e.npc_pol, e.rel, e.rel == NpcRel::Conquered, ""});
        }
    }
    auto specs = build_npc_ui_list(src);
    out.reserve(specs.size());
    for (auto& s : specs) {
        rivals::NpcUiEntry ne;
        ne.npc_id = s.npc_id;
        ne.label = std::move(s.label);
        ne.allied = s.allied;
        ne.hostile = s.hostile;
        ne.can_turn = s.can_turn;
        ne.can_protect = s.can_protect;
        ne.can_smooth = s.can_smooth;
        out.push_back(std::move(ne));
    }
    return out;
}

static void on_faction_message(uint8_t from, uint8_t subkind, net::Reader& r)
{
    if (!g_rivals_tribe_fix)
        return;
    using namespace wire::rivals_faction;
    if (subkind == SK_ASSIGN && is_guest() && from == 0) {
        uint32_t rev = 0;
        std::vector<AssignEntry> list;
        if (!read_assign(r, rev, list))
            return;
        g_assign_rev = rev;
        g_guest_assign = std::move(list);
        apply_guest_perspective();
        return;
    }
    if (subkind == SK_NPC_RELS && is_guest() && from == 0) {
        uint8_t target_pid = 0;
        std::vector<NpcRelEntry> list;
        if (!read_npc_rels(r, target_pid, list) || target_pid != my_id())
            return;
        g_guest_npc_rels = std::move(list);
        g_last_npc_count = (uint32_t)g_guest_npc_rels.size();
        g_last_win_needed = g_last_npc_count > 0 ? (int)g_last_npc_count / 2 + 1 : 1;
        void* my_t = tribe_of_player(my_id());
        void* mgr = rel_mgr();
        uint32_t mp = tribe_pol(my_t);
        if (my_t && mgr && mp) {
            for (const auto& e : g_guest_npc_rels) {
                void* nt = find_tribe_by_pol_or_pos(e.npc_pol, e.x, e.y, e.z);
                if (nt && nt != my_t) {
                    uint32_t np = tribe_pol(nt);
                    if (np)
                        apply_rel_internal(mgr, np, mp, e.rel);
                }
            }
        }
        return;
    }
    if (subkind == SK_GIFT && is_host() && from != 0) {
        GiftMsg m;
        if (!read_gift(r, m))
            return;
        host_apply_gift(from, m.to_pid, m.kind, m.value);
        return;
    }
    if (subkind == SK_PVP_HIT) {
        PvpHitMsg m;
        if (!read_pvp_hit(r, m))
            return;
        if (is_host() && from != 0 && m.kind == PvpHitKind::RaidFood) {
            host_try_war_food_raid(from, m.victim_pid, (int32_t)std::lround(m.amount));
        } else if (is_guest() && from == 0 && m.kind == PvpHitKind::RaidFood) {
            g_raids_done++;
        }
        return;
    }
    if (subkind == SK_DRAFT_APPLY && is_guest() && from == 0) {
        DraftApplyMsg m;
        if (!read_draft_apply(r, m) || m.target_pid != my_id())
            return;
        if (m.kind == DraftApplyKind::ToolUnlock) {
            unlock_tool_local((int)m.value, tribe_of_player(my_id()));
            g_draft_unlocked++;
        } else if (m.kind == DraftApplyKind::Payout && m.value > 0) {
            g_draft_payouts_applied++;
            if (tribe::C.notice)
                tribe::C.notice("Creature draft payout: +" + std::to_string(m.value) + " food!");
            if (H && M)
                H->log(M, SK_INFO, "tribe_rivals: received +%u Tribe food Creature draft payout", (unsigned)m.value);
        }
        return;
    }
    if (subkind == SK_STAGE_CUSTOM) {
        FoodSyncMsg m;
        if (!read_food_sync(r, m))
            return;
        if (is_guest() && from == 0 && m.op == CustomOp::FoodTotal && m.player_id == my_id()) {
            void* mt = tribe_of_player(my_id());
            if (mt) {
                tribe_set_food(mt, m.value);
                for (int k = kMinToolKind; k <= kMaxToolKind; k++)
                    if (m.tools_mask & (1u << k))
                        unlock_tool_local(k, mt);
            }
        } else if (is_guest() && from == 0 && m.op == CustomOp::Immunity && rivals::valid_player(m.player_id)) {
            double now = H ? H->time_seconds() : 0.0;
            bool was = is_player_immune(g_wipe[m.player_id], now);
            g_wipe[m.player_id].wiping = (m.tools_mask & 1u) != 0;
            g_wipe[m.player_id].immune_until = m.value > 0 ? (now + (double)m.value) : 0.0;
            if (!was && m.value > 0 && tribe::C.notice && S) {
                std::string who = m.player_id == my_id() ? std::string(i18n::tr("Your tribe"))
                                                         : (S->name_of(m.player_id) + i18n::tr("'s tribe"));
                tribe::C.notice(who + i18n::tr(" has a PvP shield for ") + fmt_mmss((double)m.value));
            }
        } else if (is_host() && from != 0 && m.op == CustomOp::BuyMember) {
            handle_guest_baby(from, 0.f, 0.f, 0.f);
        }
        return;
    }
}

// ---- Host guest baby purchase in own tribe ----
bool handle_guest_baby(uint8_t from, float /*x*/, float /*y*/, float /*z*/)
{
    if (!active() || !is_host() || from == 0 || !rivals::valid_player(from))
        return false;
    void* gt = tribe_of_player(from);
    if (!gt)
        return true; // swallow in rivals mode if guest tribe not assigned yet
    int cost = 10;
    if (tribe_food(gt) < cost)
        return true;
    void* baby = tribe::spawn_member(gt, 0, from);
    if (baby) {
        tribe_add_food(gt, -cost);
        host_send_food(from, true);
        if (H && M)
            H->log(M, SK_INFO, "tribe_rivals: player %u bought baby in own tribe %p (food now %d)", (unsigned)from, gt,
                   tribe_food(gt));
    } else if (H && M) {
        H->log(M, SK_WARN, "tribe_rivals: player %u baby spawn returned null in tribe %p (food kept at %d)",
               (unsigned)from, gt, tribe_food(gt));
    }
    return true;
}

int32_t player_food(uint8_t pid)
{
    void* t = tribe_of_player(pid);
    return t ? tribe_food(t) : 0;
}

bool spend_player_food(uint8_t pid, int32_t cost)
{
    if (!active() || !is_host() || cost <= 0)
        return true;
    void* t = tribe_of_player(pid);
    if (!t || tribe_food(t) < cost)
        return false;
    tribe_add_food(t, -cost);
    if (pid != 0)
        host_send_food(pid, true);
    return true;
}

void sync_player_food(uint8_t pid)
{
    if (!active() || !is_host() || pid == 0)
        return;
    host_send_food(pid, true);
}

// ---- BUG-1554: Native cRelationshipManager PvP War/Ally sync & Creature draft payout delivery ----
static void sync_pvp_native_relations()
{
    void* mgr = rel_mgr();
    if (!mgr || !o_ApplyRel)
        return;
    uint32_t pols[rivals::kMaxPlayers] = {};
    for (int p = 0; p < rivals::kMaxPlayers; p++) {
        void* pt = tribe_of_player((uint8_t)p);
        if (pt)
            pols[p] = tribe_pol(pt);
    }
    for (int i = 0; i < rivals::kMaxPlayers; i++) {
        if (!pols[i])
            continue;
        for (int j = i + 1; j < rivals::kMaxPlayers; j++) {
            if (!pols[j] || pols[i] == pols[j])
                continue;
            NpcRel want = pvp_npc_rel_for(rivals::relation((uint8_t)i, (uint8_t)j));
            apply_rel_internal(mgr, pols[i], pols[j], want);
            apply_rel_internal(mgr, pols[j], pols[i], want);
        }
    }
}

// ---- BUG-1564: Creature -> Tribe pack pet handoff ----
static std::vector<uint32_t> tribe_dom_pet_species(void* t)
{
    std::vector<uint32_t> out;
    if (!t || !skg::readable(t, 0x19D0))
        return out;
    void* dh = a_GetDomHerd ? thiscall<void*>(a_GetDomHerd, t) : F<void*>(t, 0x19CC);
    if (!dh || !skg::readable(dh, 0x48))
        return out;
    void** b = F<void**>(dh, 0x40);
    void** e = F<void**>(dh, 0x44);
    if (!b || e <= b || e - b > 32 || IsBadReadPtr(b, (size_t)((uint8_t*)e - (uint8_t*)b)))
        return out;
    for (void** p = b; p < e; ++p) {
        void* a = *p;
        if (!a || !game::listed(a, 0x18EB45Eu) || game::is_dead(a))
            continue;
        uint32_t sp = game::species_instance(a);
        if (sp != 0)
            out.push_back(sp);
    }
    return out;
}

static void* find_species_profile_for_pet(uint32_t sp_inst)
{
    if (!sp_inst)
        return nullptr;
    void* prof = game::species_by_key({sp_inst, 0x2B978C46u, 0x40626200u});
    if (prof && skg::readable(prof, kProfGenOff + 4))
        return prof;
    struct Q {
        uint32_t want;
        void* found = nullptr;
    } q{sp_inst, nullptr};
    game::for_each_object(
        [](void* a, uint32_t noun, void* u) {
            auto* q = (Q*)u;
            if (noun != 0x18EB45Eu || !game::listed(a, 0x18EB45Eu))
                return true;
            if (game::species_instance(a) == q->want) {
                void* sp = game::species_profile(a);
                if (sp && skg::readable(sp, kProfGenOff + 4)) {
                    q->found = sp;
                    return false;
                }
            }
            return true;
        },
        &q);
    return q.found;
}

static void* find_wild_herd_of_species(void* prof)
{
    if (!prof)
        return nullptr;
    struct Q {
        void* prof;
        void* found = nullptr;
    } q{prof, nullptr};
    game::for_each_object(
        [](void* h, uint32_t noun, void* u) {
            auto* q = (Q*)u;
            if (noun != game::kHerdNoun || !game::listed(h, game::kHerdNoun) || !skg::readable(h, 0x160))
                return true;
            if (F<uint32_t>(h, 0x88) == tribe::kDomesticatedArchetype ||
                F<int32_t>(h, 0x15C) == tribe::kDomesticatedPersonality ||
                F<uint32_t>(h, 0x88) == tribe::kEpicArchetype)
                return true;
            if (game::herd_species(h) == q->prof) {
                q->found = h;
                return false;
            }
            return true;
        },
        &q);
    return q.found;
}

static bool spawn_domesticated_pet_at(void* t, void* prof, int slot_idx)
{
    if (!t || !prof || !a_CreateHerd || !a_HerdEnable || !a_CreatureCreate || !skg::readable(t, 0x370))
        return false;
    if (!F<void*>(t, 0x2D4))
        return false; // cTribe_SetupDomesticatedHerd requires tribe+0x2D4
    net::Vec3 tp = tribe::tribe_pos(t);
    float len = std::sqrt(tp.x * tp.x + tp.y * tp.y + tp.z * tp.z);
    net::Vec3 up = len > 1.f ? net::Vec3{tp.x / len, tp.y / len, tp.z / len} : net::Vec3{0.f, 0.f, 1.f};
    net::Vec3 t1 = std::fabs(up.z) < 0.9f ? net::Vec3{up.y, -up.x, 0.f} : net::Vec3{0.f, up.z, -up.y};
    float l1 = std::sqrt(t1.x * t1.x + t1.y * t1.y + t1.z * t1.z);
    if (l1 > 1e-4f) {
        t1.x /= l1;
        t1.y /= l1;
        t1.z /= l1;
    }
    net::Vec3 t2{up.y * t1.z - up.z * t1.y, up.z * t1.x - up.x * t1.z, up.x * t1.y - up.y * t1.x};
    float ang = (float)slot_idx * 2.0943951f; // 120 deg apart around village
    float rad = 11.0f;
    float ca = std::cos(ang), sa = std::sin(ang);
    float pp[3] = {
        tp.x + (t1.x * ca + t2.x * sa) * rad,
        tp.y + (t1.y * ca + t2.y * sa) * rad,
        tp.z + (t1.z * ca + t2.z * sa) * rad,
    };
    void* nmgr = game::noun_manager();
    if (!nmgr)
        return false;
    bool temp_herd = false;
    void* donor = find_wild_herd_of_species(prof);
    if (!donor) {
        donor = thiscall<void*>(a_CreateHerd, nmgr, pp, prof, 0, false, 0, false);
        if (!donor)
            return false;
        temp_herd = true;
        thiscall<void>(a_HerdEnable, donor, true);
    }
    void* c = skg::cdecl_call<void*>(a_CreatureCreate, pp, prof, 1, donor, false, false);
    if (!c) {
        if (temp_herd)
            game::herd_quiet(donor);
        return false;
    }
    bool dom = tribe::domesticate_animal(c, t);
    if (temp_herd)
        game::herd_quiet(donor);
    if (!dom) {
        game::destroy_creature(c);
        return false;
    }
    return true;
}

static void host_spawn_pack_pets_for(uint8_t pid)
{
    if (!is_host() || !active() || !rivals::valid_player(pid) || world::cinematic_playing())
        return;
    void* t = tribe_of_player(pid);
    if (!t || g_pack_pets.spawned[pid])
        return;
    if (g_pack_pets.species[pid].empty())
        return; // keep unspawned until species exist or set_pack_pets is called
    auto existing = tribe_dom_pet_species(t);
    auto want = take_pack_pets_to_spawn(g_pack_pets, g_pack_pets_fix, true, pid, true, existing);
    if (want.empty())
        return;
    int slot = (int)existing.size();
    uint32_t made = 0;
    for (uint32_t sp_inst : want) {
        void* prof = find_species_profile_for_pet(sp_inst);
        if (!prof) {
            if (H && M)
                H->log(M, SK_INFO, "tribe_rivals: pack pet species %08X for player %u not loaded; skipping (BUG-1564)",
                       sp_inst, (unsigned)pid);
            continue;
        }
        if (spawn_domesticated_pet_at(t, prof, slot++)) {
            g_pack_pets_spawned++;
            made++;
        }
    }
    if (made && H && M)
        H->log(M, SK_INFO, "tribe_rivals: spawned %u domesticated Creature pack pet(s) for player %u at tribe %p (BUG-1564)",
               made, (unsigned)pid, t);
}

static void host_spawn_pack_pets()
{
    if (!g_pack_pets_fix || !is_host() || !active() || !S)
        return;
    for (const auto& pl : S->players())
        if (rivals::valid_player(pl.id))
            host_spawn_pack_pets_for(pl.id);
}

static void host_deliver_creature_payouts()
{
    if (!is_host() || !active() || rivals::draft_state().active || world::cinematic_playing())
        return;
    for (uint8_t pid = 0; pid < rivals::kMaxPlayers; pid++) {
        void* pt = tribe_of_player(pid);
        if (!pt)
            continue;
        int32_t raw = rivals::take_pending_draft_payout_for(rivals::Stage::Creature, pid);
        int32_t food = creature_draft_payout_food(raw);
        if (food <= 0)
            continue;
        tribe_add_food(pt, food);
        g_draft_payouts_applied++;
        if (pid == 0) {
            if (tribe::C.notice)
                tribe::C.notice("Creature draft payout: +" + std::to_string(food) + " food!");
        } else {
            host_send_food(pid, true);
            net::Writer w;
            write_draft_apply(w, {pid, DraftApplyKind::Payout, (uint32_t)food});
            rivals::send_faction_to(pid, rivals::Stage::Tribe, wire::rivals_faction::SK_DRAFT_APPLY, w);
        }
        if (H && M)
            H->log(M, SK_INFO, "tribe_rivals: delivered +%d Tribe food Creature draft payout to player %u (food now %d)",
                   (int)food, (unsigned)pid, tribe_food(pt));
    }
}

// ---- Host tick: R2 takeover, species swap, R4 NPC relations, 11.5 stage end ----
static void host_try_auto_form_npc(double now, const char* reason)
{
    if (now - g_last_auto_form < 2.0 || g_auto_form_count >= 6)
        return;
    if (!tribe::a_NpcCtrl || !tribe::o_Form)
        return;
    void* ctrl = skg::cdecl_call<void*>(tribe::a_NpcCtrl);
    if (!ctrl)
        return;
    tribe::FormSpecies fs;
    if (tribe::form_species_state(ctrl, fs) && fs.past)
        return;
    g_last_auto_form = now;
    void* made = tribe::o_Form(ctrl, 0);
    if (made) {
        g_auto_form_count++;
        if (H && M)
            H->log(M, SK_INFO, "tribe_rivals: auto-formed NPC tribe %p (%s, total_auto=%u, tribes=%u)", made, reason,
                   g_auto_form_count, (unsigned)tribe::all_tribes().size());
    }
}

static void host_ensure_assignments(double now)
{
    void* pt = tribe::player_tribe();
    if (!pt || !S)
        return;
    // Host always owns player_tribe()
    g_owned[0].ptr = pt;
    g_owned[0].serial = game::object_serial(pt);
    g_owned[0].pol = tribe_pol(pt);
    g_owned[0].pos = tribe::tribe_pos(pt);

    auto ts = tribe::all_tribes();
    net::Vec3 hp = g_owned[0].pos;
    bool changed = false;
    bool need_guest_tribe = false;

    for (const auto& pl : S->players()) {
        uint8_t pid = pl.id;
        if (pid == 0 || !rivals::valid_player(pid))
            continue;
        auto& ot = g_owned[pid];
        if (ot.ptr && tribe_alive(ot.ptr, ot.serial) && !tribe_conquered(ot.ptr)) {
            ot.pol = tribe_pol(ot.ptr);
            ot.pos = tribe::tribe_pos(ot.ptr);
            // Apply guest species line if it arrived and hasn't been applied yet
            if (!ot.species_swapped) {
                creations::Key lk = species::key(pid, species::Stage::Tribe);
                if (void* prof = species_profile_of(lk)) {
                    if (swap_tribe_species(ot, prof))
                        changed = true;
                }
            }
            continue;
        }
        // Try persisted assignment by stable player key first
        void* picked = nullptr;
        std::string pkey = store::key_of(pid);
        if (!pkey.empty()) {
            for (const auto& sa : g_stored_assign) {
                if (sa.key == pkey) {
                    void* cand = find_tribe_by_pol_or_pos(0, sa.x, sa.y, sa.z);
                    if (cand && cand != pt && !tribe_conquered(cand) && !is_player_owned_tribe(cand)) {
                        picked = cand;
                        break;
                    }
                }
            }
        }
        if (!picked) {
            std::vector<TribeSite> sites;
            sites.reserve(ts.size());
            for (void* t : ts) {
                net::Vec3 p = tribe::tribe_pos(t);
                sites.push_back({tribe_pol(t), p.x, p.y, p.z, t == pt, tribe_conquered(t), is_player_owned_tribe(t)});
            }
            int idx = pick_rival_tribe(sites, hp.x, hp.y, hp.z, rivals::base_distance());
            if (idx >= 0 && idx < (int)ts.size())
                picked = ts[idx];
        }
        if (!picked) {
            need_guest_tribe = true;
            continue;
        }
        ot = OwnedTribe{};
        ot.ptr = picked;
        ot.serial = game::object_serial(picked);
        ot.pol = tribe_pol(picked);
        ot.pos = tribe::tribe_pos(picked);
        int32_t target_food = starting_rival_food(tribe_food(picked), tribe_food(pt), g_tribe_polish_fix);
        if (target_food != tribe_food(picked))
            tribe_set_food(picked, target_food);
        creations::Key lk = species::key(pid, species::Stage::Tribe);
        if (void* prof = species_profile_of(lk))
            swap_tribe_species(ot, prof);
        // Remove taken-over tribe from NPC relations table
        g_npc_rels.forget_npc(ot.pol);
        changed = true;
        if (H && M)
            H->log(M, SK_INFO, "tribe_rivals: assigned NPC tribe %p (pol %08X) to player %u (dist=%s)", picked, ot.pol,
                   (unsigned)pid, rivals::distance_name(rivals::base_distance()));
    }
    // Fresh Tribe planets start with only 1 NPC tribe; once guests take it over (R7 inviolable), spawn extra tribes
    // so 3-4 players each get a village AND at least 2 non-player NPC tribes exist for stage progression.
    if (need_guest_tribe) {
        host_try_auto_form_npc(now, "guest_tribe");
    } else if (!ts.empty() && !g_stage_won_reported && !g_race_end_handled) {
        int total_npcs = 0;
        for (void* t : ts) {
            if (t && t != pt && !is_player_owned_tribe(t))
                total_npcs++;
        }
        if (total_npcs < 2)
            host_try_auto_form_npc(now, "free_npc_reserve");
    }
    if (changed || now - g_last_assign_tx >= 3.0) {
        g_last_assign_tx = now;
        host_broadcast_assign();
    }
}

static void host_eval_tick(double now)
{
    if (now - g_last_eval_at < 1.0)
        return;
    g_last_eval_at = now;
    if (!S)
        return;
    sync_pvp_native_relations();
    void* mgr = rel_mgr();
    auto ts = tribe::all_tribes();
    std::vector<uint32_t> npc_pols;
    for (void* t : ts) {
        if (is_player_owned_tribe(t))
            continue;
        uint32_t np = tribe_pol(t);
        if (!np)
            continue;
        npc_pols.push_back(np);
        // Sync live game state for host (p=0) if conquered or relation changed natively
        if (tribe_conquered(t) && g_npc_rels.get(0, np) != NpcRel::Conquered) {
            bool someone = false;
            for (int p = 0; p < rivals::kMaxPlayers; p++)
                if (g_npc_rels.get((uint8_t)p, np) == NpcRel::Conquered)
                    someone = true;
            if (!someone)
                g_npc_rels.mark_conquered(0, np);
        } else if (mgr && g_owned[0].pol && g_npc_rels.get(0, np) != NpcRel::Conquered) {
            int lvl = rel_level_of(mgr, rel_value(mgr, np, g_owned[0].pol));
            // Only raise natively if game's own social/gift pushed it to Allied
            if (lvl >= 4 && g_npc_rels.get(0, np) != NpcRel::Allied)
                g_npc_rels.set(0, np, NpcRel::Allied);
        }
    }
    bool present[rivals::kMaxPlayers] = {};
    for (const auto& pl : S->players())
        if (rivals::valid_player(pl.id))
            present[pl.id] = true;

    auto ev = evaluate_stage_end(npc_pols, present, g_npc_rels);
    g_last_npc_count = (uint32_t)ev.total_npc;
    g_last_win_needed = ev.win_needed;

    for (int p = 0; p < rivals::kMaxPlayers; p++) {
        if (!present[p])
            continue;
        void* pt = tribe_of_player((uint8_t)p);
        rivals::PlayerProgress prog;
        prog.player_id = (uint8_t)p;
        prog.present = true;
        prog.primary = (float)ev.subjugated[p];
        prog.secondary = (float)(pt ? tribe_food(pt) : 0);
        prog.stat_a = (uint16_t)(pt ? tribe::members(pt).size() : 0);
        prog.stat_b = (uint16_t)ev.total_npc;
        prog.extra = (float)ev.win_needed;
        rivals::set_player_progress((uint8_t)p, prog);
        if (g_stage_won_reported && ev.subjugated[p] >= ev.win_needed)
            rivals::report_stage_complete((uint8_t)p);
    }
    if (ev.winner != 255 && !g_stage_won_reported) {
        g_stage_won_reported = true;
        rivals::report_stage_complete(ev.winner);
    }
}

// ---- Persistence (store.cpp "tribe.rivals" v1) ----
static void store_load_if_opened()
{
    pstore::Store* w = store::world();
    uint32_t ser = store::serial();
    if (!w || ser == g_store_serial)
        return;
    g_store_serial = ser;
    g_stored_assign.clear();
    const pstore::Record* rec = w->get_world("tribe.rivals");
    if (!rec || rec->ver != 1)
        return;
    pstore::In in(rec->data);
    uint8_t n = in.u8();
    for (uint8_t i = 0; i < n && in.ok; i++) {
        StoredAssign sa;
        sa.key = in.str();
        sa.x = in.f32();
        sa.y = in.f32();
        sa.z = in.f32();
        if (in.ok && !sa.key.empty())
            g_stored_assign.push_back(std::move(sa));
    }
}

static void store_save(pstore::Store& st, int64_t now)
{
    if (!g_rivals_tribe_fix || !g_store_serial || g_store_serial != store::serial() || &st != store::world() ||
        game::mode_id() != modes::kTribe || !is_host() || !active())
        return;
    std::vector<StoredAssign> list;
    std::vector<std::string> owners;
    for (int p = 1; p < rivals::kMaxPlayers; p++) {
        if (!g_owned[p].ptr || !tribe_alive(g_owned[p].ptr, g_owned[p].serial))
            continue;
        std::string k = store::key_of((uint8_t)p);
        if (k.empty())
            continue;
        net::Vec3 pos = tribe::tribe_pos(g_owned[p].ptr);
        list.push_back({k, pos.x, pos.y, pos.z});
        owners.push_back(k);
    }
    if (list.empty())
        return;
    pstore::Out o;
    o.u8((uint8_t)list.size());
    for (const auto& sa : list) {
        o.str(sa.key);
        o.f32(sa.x);
        o.f32(sa.y);
        o.f32(sa.z);
    }
    st.put_world("tribe.rivals", 1, o.d);
    store::mark_owners(st, "tribe.rivals.owner", owners, now);
}

static void store_adopt(const std::string& from, const std::string& to)
{
    for (auto& sa : g_stored_assign)
        if (sa.key == from)
            sa.key = to;
}

static bool g_tr_store_reg = store::add({"tribe.rivals", store_save, store_adopt, nullptr});

// ---- Save-strip hooks (never corrupt vanilla save!) ----
static void save_start()
{
    g_saved_slots.clear();
    if (g_swapped) {
        g_save_temp_unswapped = true;
        restore_guest_perspective("save_start");
    }
    if (a_SpeciesSlot && a_SetSpeciesSlot) {
        for (int p = 1; p < rivals::kMaxPlayers; p++) {
            auto& ot = g_owned[p];
            if (ot.species_swapped && ot.orig_slot && ot.ptr && tribe_alive(ot.ptr, ot.serial)) {
                void* cur = thiscall<void*>(a_SpeciesSlot, ot.ptr, 0);
                g_saved_slots.push_back({ot.ptr, ot.serial, cur, ot.orig_slot});
                thiscall<void>(a_SetSpeciesSlot, ot.ptr, ot.orig_slot, 0);
            }
        }
    }
}

static void save_end()
{
    if (a_SetSpeciesSlot) {
        for (const auto& ss : g_saved_slots) {
            if (ss.cur_slot && tribe_alive(ss.tribe, ss.serial))
                thiscall<void>(a_SetSpeciesSlot, ss.tribe, ss.cur_slot, 0);
        }
    }
    g_saved_slots.clear();
    if (g_save_temp_unswapped) {
        g_save_temp_unswapped = false;
        apply_guest_perspective();
    }
}

static int live_tribe_members(void* t)
{
    if (!t)
        return 0;
    int live = 0;
    for (void* c : tribe::members(t))
        if (c && !game::is_dead(c) && game::health(c) > 0.0f)
            live++;
    return live;
}

static int tribe_up_count(void* t)
{
    if (!t || !skg::readable(t, 0x35C))
        return 0;
    uintptr_t b = F<uintptr_t>(t, 0x354);
    uintptr_t e = F<uintptr_t>(t, 0x358);
    if (!b || e < b || (e - b) > 4096)
        return 0;
    return (int)((e - b) / 4);
}

// cTribe::CreateCitizen (0x00C8E730) passes &zeroVec to Init and only moves age==0 (babies) to tribe_pos();
// age==1 (adults) stay at (0,0,0) (planet core/underwater) and die within ~1 s unless placed on the village surface.
static net::Vec3 village_spawn_spot(void* t, int slot_idx)
{
    net::Vec3 centre = tribe::tribe_pos(t);
    float r = std::sqrt(centre.x * centre.x + centre.y * centre.y + centre.z * centre.z);
    if (r < 1.0f)
        return centre;
    net::Vec3 n{centre.x / r, centre.y / r, centre.z / r};
    net::Vec3 ref = (std::fabs(n.y) < 0.9f) ? net::Vec3{0.f, 1.f, 0.f} : net::Vec3{1.f, 0.f, 0.f};
    net::Vec3 e1{n.y * ref.z - n.z * ref.y, n.z * ref.x - n.x * ref.z, n.x * ref.y - n.y * ref.x};
    float e1l = std::sqrt(e1.x * e1.x + e1.y * e1.y + e1.z * e1.z);
    if (e1l > 1e-4f)
        e1 = {e1.x / e1l, e1.y / e1l, e1.z / e1l};
    net::Vec3 e2{n.y * e1.z - n.z * e1.y, n.z * e1.x - n.x * e1.z, n.x * e1.y - n.y * e1.x};
    float ang = 1.5707963f * (float)(slot_idx & 3) + 0.4f;
    float cs = std::cos(ang) * 6.0f, sn = std::sin(ang) * 6.0f;
    net::Vec3 p{centre.x + e1.x * cs + e2.x * sn, centre.y + e1.y * cs + e2.y * sn, centre.z + e1.z * cs + e2.z * sn};
    float g = game::ground_radius(p);
    float pr = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (g > 1.0f && pr > 1.0f)
        p = {p.x * g / pr, p.y * g / pr, p.z * g / pr};
    return p;
}

static void ensure_baby_button_unlocked()
{
    if (!a_TribeUiGet)
        return;
    void* ui = skg::cdecl_call<void*>(a_TribeUiGet);
    if (ui && skg::readable(ui, 0x3A4) && F<uint8_t>(ui, 0x3A3) == 0)
        F<uint8_t>(ui, 0x3A3) = 1;
}

constexpr uintptr_t kTribeChiefKoTimerOff = 0x2C8; // float: cTribe chieftain knockout countdown (BT_CitizenKnockoutEnter 0xDB1A30)

static void ensure_live_chieftain(void* t)
{
    if (!a_SetChieftain || !t || !skg::readable(t, 0x2CC))
        return;
    void* ch = F<void*>(t, 0x11C);
    // Keep vanilla chieftain knockout intact when the chieftain object still exists (even while recovering).
    if (ch && game::listed(ch, kCitizenNoun))
        return;
    // Chieftain pointer is null/gone: clear any stale knockout timer before promoting a living adult.
    if (F<float>(t, kTribeChiefKoTimerOff) > 0.0f)
        F<float>(t, kTribeChiefKoTimerOff) = 0.0f;
    for (void* c : tribe::members(t)) {
        if (c && !game::is_dead(c) && game::health(c) > 0.0f && F<int>(c, 0xB34) == 1) {
            thiscall<void>(a_SetChieftain, t, c);
            return;
        }
    }
}

// BUG-1571: Detect full player tribe wipes on host, grant 5-minute PvP immunity, respawn chieftain + villager
// after short delay with >= 15 food, and keep Add Baby usable (never Game Over or softlock).
static void host_wipe_recovery_tick(double now)
{
    if (!wipe_recovery_active() || !is_host() || !S || now - g_last_wipe_tick < 0.5)
        return;
    g_last_wipe_tick = now;
    ensure_baby_button_unlocked();
    bool periodic_tx = (now - g_last_immune_tx >= 3.0);
    if (periodic_tx)
        g_last_immune_tx = now;
    for (const auto& pl : S->players()) {
        uint8_t p = pl.id;
        if (!rivals::valid_player(p))
            continue;
        void* pt = tribe_of_player(p);
        if (!pt)
            continue;
        int live = live_tribe_members(pt);
        if (live > 0)
            ensure_live_chieftain(pt);
        auto dec = step_wipe_recovery(g_wipe[p], g_wipe_recovery_fix, active(), true, live, now, g_wipe_respawn_delay,
                                      g_wipe_immune_duration);
        if (dec.started_wipe) {
            g_wipes_detected++;
            if (H && M)
                H->log(M, SK_INFO,
                       "tribe_rivals: player %u tribe %p wiped (0 live members); respawn in %.1fs, PvP shield %.0fs (BUG-1571)",
                       (unsigned)p, pt, g_wipe_respawn_delay, dec.immune_remaining);
            if (tribe::C.notice)
                tribe::C.notice(pl.name + i18n::tr("'s tribe was wiped out! Respawning in ") +
                                std::to_string((int)std::round(g_wipe_respawn_delay)) + "s (" +
                                fmt_mmss(dec.immune_remaining) + ").");
        }
        if (dec.do_respawn) {
            // Clear cTribe's chieftain knockout timer (+0x2C8) before promoting a respawned adult;
            // otherwise BT_CitizenKnockoutEnter (0xDB1A30) immediately knocks out the new chieftain on its next tick.
            void* old_ch = skg::readable(pt, 0x120) ? F<void*>(pt, 0x11C) : nullptr;
            if (skg::readable(pt, kTribeChiefKoTimerOff + 4))
                F<float>(pt, kTribeChiefKoTimerOff) = 0.0f;
            void* first = nullptr;
            int spawned = 0;
            for (int k = 0; k < kWipeRespawnAdults; k++) {
                if (void* c = tribe::spawn_member(pt, 1, p)) {
                    net::Vec3 spot = village_spawn_spot(pt, spawned);
                    game::teleport(c, spot, game::orientation(c));
                    game::set_grounded(c, true);
                    float mx = game::max_health(c);
                    if (mx > 0.0f && game::health(c) <= 0.0f)
                        game::set_health(c, mx);
                    if (!first)
                        first = c;
                    spawned++;
                }
            }
            if (first && a_SetChieftain) {
                thiscall<void>(a_SetChieftain, pt, first);
                if (old_ch && old_ch != first && game::listed(old_ch, kCitizenNoun) &&
                    (game::is_dead(old_ch) || game::health(old_ch) <= 0.0f)) {
                    vcall<void>(pt, 0x9C / 4, old_ch, 1); // RemoveMember(old_ch, destroy) so knocked-out chief doesn't linger
                }
            }
            if (tribe_food(pt) < kWipeRecoveryMinFood) {
                tribe_set_food(pt, kWipeRecoveryMinFood);
                if (p != 0)
                    host_send_food(p, true);
            }
            ensure_baby_button_unlocked();
            if (spawned > 0) {
                g_wipes_recovered++;
                if (H && M)
                    H->log(M, SK_INFO,
                           "tribe_rivals: respawned %d adult(s) for wiped player %u tribe %p (food=%d, shield=%.0fs, BUG-1571)",
                           spawned, (unsigned)p, pt, tribe_food(pt), dec.immune_remaining);
                if (tribe::C.notice)
                    tribe::C.notice(pl.name + i18n::tr("'s tribe respawned! PvP shield active for ") +
                                    fmt_mmss(dec.immune_remaining) + ".");
            } else {
                // Retry next tick if engine refused spawn for one frame
                g_wipe[p].wiping = true;
                g_wipe[p].wipe_at = now - g_wipe_respawn_delay;
            }
        }
        if (dec.immunity_changed || (periodic_tx && (dec.immune_now || g_wipe[p].wiping)))
            host_broadcast_immunity(p, now);
    }
}

static void reset_runtime(bool restore_game)
{
    if (restore_game) {
        restore_guest_perspective("reset");
        restore_host_species_slots();
    } else {
        g_swapped = false;
    }
    for (int i = 0; i < rivals::kMaxPlayers; i++) {
        g_owned[i] = OwnedTribe{};
        g_last_raid_at[i] = 0.0;
        g_wipe[i] = PlayerWipeState{};
    }
    g_last_wipe_tick = 0.0;
    g_last_immune_tx = 0.0;
    g_last_hut_floor_notice = 0.0;
    g_guest_assign.clear();
    g_guest_npc_rels.clear();
    g_orig_pt = nullptr;
    g_orig_pt_serial = 0;
    g_my_tribe = nullptr;
    g_my_tribe_serial = 0;
    g_save_temp_unswapped = false;
    g_npc_rels.clear();
    g_stage_won_reported = false;
    g_race_end_handled = false;
    g_was_active = false;
    g_last_guest_pvp_rel = 0.0;
    g_last_auto_form = 0.0;
    g_auto_form_count = 0;
    g_last_npc_count = 0;
    g_last_win_needed = 1;
}

LIFECYCLE_HOOK("tribe_rivals.session", lifecycle::kSessionEnd, -5,
               [](const lifecycle::Ctx&) {
                   g_pack_pets.clear_all();
                   reset_runtime(true);
               },
               false);
LIFECYCLE_HOOK("tribe_rivals.world", lifecycle::kWorldChange, -5,
               [](const lifecycle::Ctx&) {
                   g_pack_pets.reset_spawned(); // preserve Creature pack species across Creature -> Tribe transition
                   reset_runtime(false);
               },
               false);

void tick(double now)
{
    // BUG-1564: capture each player's living Creature-stage pack species before transitioning to Tribe.
    if (g_pack_pets_fix && is_host() && game::in_creature_stage() && S && now - g_last_pack_snap >= 0.5) {
        g_last_pack_snap = now;
        for (const auto& pl : S->players()) {
            if (!rivals::valid_player(pl.id))
                continue;
            auto sps = npc::pack_species_of(pl.id);
            record_pack_pets(g_pack_pets, pl.id, sps);
        }
    }
    bool act = active();
    if (!act) {
        if (g_was_active)
            reset_runtime(true);
        return;
    }
    g_was_active = true;
    store_load_if_opened();

    if (is_host()) {
        host_ensure_assignments(now);
        host_spawn_pack_pets();
        host_deliver_creature_payouts();
        host_wipe_recovery_tick(now); // BUG-1571
        host_eval_tick(now);
        if (now - g_last_food_tx >= 0.5) {
            g_last_food_tx = now;
            for (int p = 1; p < rivals::kMaxPlayers; p++)
                host_send_food((uint8_t)p, false);
        }
        if (now - g_last_rel_tx >= 1.5) {
            g_last_rel_tx = now;
            host_sync_npc_rels_to_guests();
        }
    } else if (is_guest()) {
        apply_guest_perspective();
        if (wipe_recovery_active())
            ensure_baby_button_unlocked();
        if (g_swapped && now - g_last_guest_pvp_rel >= 1.0) {
            g_last_guest_pvp_rel = now;
            sync_pvp_native_relations();
        }
    }
}

void init(const SkHost* host, SkMod* mod, net::Session* sess)
{
    H = host;
    M = mod;
    S = sess;

    a_SetPlayerTribe = H->sym("Simulator::cGameNounManager::SetPlayerTribe");
    a_PlannerUpdate = H->sym("SporeCoop::cTribePlanner_Update");
    a_HutTD = H->sym("SporeCoop::cTribeHut_TakeDamage");
    a_ToolTD = H->sym("SporeCoop::cTribeTool_TakeDamage");
    a_SetConquered = H->sym("SporeCoop::cTribe_SetConquered");
    a_SpeciesSlot = H->sym("SporeCoop::cTribe_SpeciesSlot");
    a_SetSpeciesSlot = H->sym("SporeCoop::cTribe_SetSpeciesSlot");
    a_IsToolPurchased = H->sym("SporeCoop::cTribe_IsToolPurchased");
    a_SetToolPurchased = H->sym("SporeCoop::cTribe_SetToolPurchased");
    a_AddFood = H->sym("SporeCoop::cTribe_AddFood");
    a_FoodFx = H->sym("Simulator::cTribe::UpdateFoodVisuals");
    a_GetCursor = H->sym("Simulator::cGameTerrainCursor::GetTerrainCursor");
    a_CursorSetObjects = H->sym("SporeCoop::cGameTerrainCursor_SetObjects");
    a_MinimapRebuild = H->sym("SporeCoop::TribeGameUI_MinimapRebuild");
    a_TribeUiGet = H->sym("SporeCoop::cTribeUI_Get");
    a_UnlockToolNotify = H->sym("SporeCoop::Tribe_UnlockToolNotify");
    a_ToolSetAt = H->sym("SporeCoop::TribeToolSet_At");
    a_RelMgrGet = H->sym("Simulator::cRelationshipManager::Get");
    a_RelCalc = H->sym("Simulator::cRelationshipManager::CalculateRelationship");
    a_RelCalcAbs = H->sym("Simulator::cRelationshipManager::CalculateRelationshipAbsolute");
    a_RelApply = H->sym("Simulator::cRelationshipManager::ApplyRelationship");
    a_RelLevel = H->sym("Simulator::cRelationshipManager::GetRelationshipLevel");
    a_SetSpecies = H->sym("Simulator::cCreatureAnimal::SetSpecies");
    a_SetSpeciesProfile = H->sym("SporeCoop::CreatureBase_SetSpeciesProfile");
    a_SetChieftain = H->sym("SporeCoop::cTribe_SetChieftain");
    a_CreateHerd = H->sym("Simulator::cGameNounManager::CreateHerd");
    a_HerdEnable = H->sym("Simulator::cHerd::SetEnabled");
    a_CreatureCreate = H->sym("Simulator::cCreatureAnimal::Create");
    a_GetDomHerd = H->sym("Simulator::cTribe::GetDomesticatedAnimalsHerd");
    a_TribeGoalListener = H->sym("SporeCoop::TribeGame_GoalListener");

    if (a_PlannerUpdate)
        H->hook_create(M, (void*)a_PlannerUpdate, (void*)h_PlannerUpdate, (void**)&o_PlannerUpdate);
    if (a_HutTD)
        H->hook_create(M, (void*)a_HutTD, (void*)h_HutTD, (void**)&o_HutTD);
    if (a_ToolTD)
        H->hook_create(M, (void*)a_ToolTD, (void*)h_ToolTD, (void**)&o_ToolTD);
    if (a_SetConquered)
        H->hook_create(M, (void*)a_SetConquered, (void*)h_SetConquered, (void**)&o_SetConquered);
    if (a_RelApply)
        H->hook_create(M, (void*)a_RelApply, (void*)h_ApplyRel, (void**)&o_ApplyRel);
    if (a_TribeGoalListener)
        H->hook_create(M, (void*)a_TribeGoalListener, (void*)h_TribeGoalListener, (void**)&o_TribeGoalListener);

    rivals::register_faction_handler(rivals::Stage::Tribe, on_faction_message);
    rivals::register_npc_influence_callback(rivals::Stage::Tribe, on_npc_influence);
    rivals::StageHooks hk;
    hk.on_buy_influence = on_buy_influence;
    hk.on_draft_pick = on_draft_pick;
    hk.on_stage_race_end = on_stage_race_end;
    hk.on_extra_ui_rows = on_extra_ui_rows;
    hk.on_npc_ui_list = on_npc_ui_list;
    rivals::register_stage_hooks(rivals::Stage::Tribe, std::move(hk));

    savestrip::add_save_hooks(save_start, save_end);
    H->log(M, SK_INFO,
           "tribe_rivals: initialized (fix=%d setpt=%d planner=%d hut=%d tool=%d conq=%d rel=%d goal=%d)",
           (int)g_rivals_tribe_fix, (int)(a_SetPlayerTribe != 0), (int)(o_PlannerUpdate != nullptr),
           (int)(o_HutTD != nullptr), (int)(o_ToolTD != nullptr), (int)(o_SetConquered != nullptr),
           (int)(o_ApplyRel != nullptr), (int)(o_TribeGoalListener != nullptr));
}

std::string status_summary()
{
    char b[480];
    void* mt = tribe_of_player(my_id());
    snprintf(b, sizeof b,
             "tribe_rivals: fix=%d active=%d swapped=%d orig_pt=%p my_tribe=%p my_pol=%08X my_food=%d "
             "planner_skipped=%u pvp_hits=%u pvp_blocked=%u hut_clamped=%u tool_pvp=%u conquer_blocked=%u "
             "gifts=%u raids=%u inf_earned=%u draft_unlocked=%u draft_payouts=%u species_swaps=%u "
             "pack_pets_spawned=%u pop_go_blocked=%u wipes=%u recovered=%u immune_blocked=%u immune_broken=%u "
             "polish=%d foodmat_blocked=%u npcs=%u win_needed=%d\n",
             (int)g_rivals_tribe_fix, (int)active(), (int)g_swapped, g_orig_pt, mt, tribe_pol(mt),
             mt ? tribe_food(mt) : 0, g_planner_skipped, g_pvp_hits, g_pvp_blocked, g_hut_clamped, g_tool_pvp_hits,
             g_conquer_blocked, g_gifts_done, g_raids_done, g_inf_earned, g_draft_unlocked, g_draft_payouts_applied,
             g_species_swaps, g_pack_pets_spawned, g_pop_gameover_blocked, g_wipes_detected, g_wipes_recovered,
             g_immune_blocked, g_immune_broken, (int)g_tribe_polish_fix, g_foodmat_blocked, g_last_npc_count,
             g_last_win_needed);
    std::string out = b;
    double now = H ? H->time_seconds() : 0.0;
    if (is_host() && active()) {
        for (int p = 0; p < rivals::kMaxPlayers; p++) {
            const auto& ot = g_owned[p];
            if (!ot.ptr || !tribe_alive(ot.ptr, ot.serial))
                continue;
            uint32_t sp_key = 0;
            if (a_SpeciesSlot) {
                void* sp = thiscall<void*>(a_SpeciesSlot, ot.ptr, 0);
                if (sp && skg::readable(sp, kProfKeyOff + 4))
                    sp_key = F<uint32_t>(sp, kProfKeyOff);
            }
            int imm_s = (int)std::ceil(player_immune_remaining(g_wipe[p], now));
            snprintf(b, sizeof b,
                     "  assign pid=%d pol=%08X ptr=%p food=%d members=%u tools=0x%X species=%08X swapped=%d live=%d immune=%d planner_disabled=1\n",
                     p, ot.pol, ot.ptr, tribe_food(ot.ptr), (unsigned)tribe::members(ot.ptr).size(),
                     tribe_tools_mask(ot.ptr), sp_key, (int)ot.species_swapped, live_tribe_members(ot.ptr), imm_s);
            out += b;
        }
        for (void* t : tribe::all_tribes()) {
            if (is_player_owned_tribe(t))
                continue;
            uint32_t np = tribe_pol(t);
            net::Vec3 p = tribe::tribe_pos(t);
            snprintf(b, sizeof b, "  npc pol=%08X pos=(%.1f,%.1f,%.1f) rels=[p0=%s,p1=%s] prot=[p0=%d,p1=%d]\n", np,
                     p.x, p.y, p.z, npc_rel_name(g_npc_rels.get(0, np)), npc_rel_name(g_npc_rels.get(1, np)),
                     (int)rivals::is_npc_protected(0, np), (int)rivals::is_npc_protected(1, np));
            out += b;
        }
    } else if (is_guest() && active()) {
        for (const auto& e : g_guest_assign) {
            void* lt = find_tribe_by_pol_or_pos(e.tribe_pol, e.x, e.y, e.z);
            int imm_s = rivals::valid_player(e.player_id) ? (int)std::ceil(player_immune_remaining(g_wipe[e.player_id], now)) : 0;
            snprintf(b, sizeof b, "  assign pid=%u pol=%08X ptr=%p food=%d members=%u tools=0x%X live=%d immune=%d\n",
                     (unsigned)e.player_id, e.tribe_pol, lt, lt ? tribe_food(lt) : 0,
                     lt ? (unsigned)tribe::members(lt).size() : 0u, lt ? tribe_tools_mask(lt) : 0u,
                     live_tribe_members(lt), imm_s);
            out += b;
        }
        for (const auto& e : g_guest_npc_rels) {
            snprintf(b, sizeof b, "  npc pol=%08X pos=(%.1f,%.1f,%.1f) rel=%s prot=%d\n", e.npc_pol, e.x, e.y, e.z,
                     npc_rel_name(e.rel), (int)e.is_protected);
            out += b;
        }
    }
    return out;
}

// ---- Console & test subcommands (`coop.tribe rivals ...`) ----
std::string debug_cmd(const std::vector<std::string>& a)
{
    // a[0] == "rivals"
    if (a.size() <= 1 || a[1] == "status")
        return status_summary();

    if (a[1] == "gift" && a.size() >= 5) {
        GiftKind kind = a[2] == "food" ? GiftKind::Food : a[2] == "tool" ? GiftKind::Tool : (GiftKind)0;
        uint8_t to = (uint8_t)atoi(a[3].c_str());
        int32_t val = (int32_t)atoi(a[4].c_str());
        std::string err = request_gift(to, kind, val);
        return err.empty() ? "ok: gift sent\n" : ("error: " + err + "\n");
    }

    if (a[1] == "raid" && a.size() >= 3) {
        uint8_t vic = (uint8_t)atoi(a[2].c_str());
        int32_t amt = a.size() >= 4 ? (int32_t)atoi(a[3].c_str()) : kDefaultRaidFoodSteal;
        if (!active())
            return "error: tribe rivals inactive\n";
        if (!rivals::at_war(my_id(), vic))
            return "error: not at war\n";
        if (is_host()) {
            host_try_war_food_raid(0, vic, amt);
            return "ok: raid applied\n";
        }
        net::Writer w;
        write_pvp_hit(w, {my_id(), vic, PvpHitKind::RaidFood, 0, (float)amt});
        rivals::send_faction_to(0, rivals::Stage::Tribe, wire::rivals_faction::SK_PVP_HIT, w);
        return "ok: raid requested\n";
    }

    if (a[1] == "form_npc" && is_host()) {
        if (!tribe::a_NpcCtrl || !tribe::o_Form)
            return "error: NpcTribeController not available\n";
        void* ctrl = skg::cdecl_call<void*>(tribe::a_NpcCtrl);
        if (!ctrl)
            return "error: no controller\n";
        tribe::FormSpecies fs;
        if (tribe::form_species_state(ctrl, fs) && fs.past)
            return "error: species list exhausted\n";
        void* made = tribe::o_Form(ctrl, 0);
        char b[96];
        snprintf(b, sizeof b, "ok: formed npc tribe %p (count=%u)\n", made, (unsigned)tribe::all_tribes().size());
        return b;
    }

    if (a[1] == "setrel" && a.size() >= 5 && is_host()) {
        uint8_t pid = (uint8_t)atoi(a[2].c_str());
        uint32_t np = (uint32_t)strtoul(a[3].c_str(), nullptr, 0);
        int rv = atoi(a[4].c_str());
        if (!rivals::valid_player(pid) || rv < 0 || rv > (int)NpcRel::Conquered)
            return "error: bad args\n";
        NpcRel prev = g_npc_rels.get(pid, np);
        NpcRel next = (NpcRel)rv;
        g_npc_rels.set(pid, np, next);
        if (pid != 0 && next != prev) {
            if (next == NpcRel::Allied)
                history::record_tribe_outcome(pid, true);
            else if (next == NpcRel::Conquered)
                history::record_tribe_outcome(pid, false);
        }
        void* pt = tribe_of_player(pid);
        if (pt && rel_mgr())
            apply_rel_internal(rel_mgr(), np, tribe_pol(pt), (NpcRel)rv);
        host_sync_npc_rels_to_guests();
        return "ok: setrel applied\n";
    }

    if (a[1] == "addfood" && a.size() >= 4 && is_host()) {
        uint8_t pid = (uint8_t)atoi(a[2].c_str());
        int32_t delta = (int32_t)atoi(a[3].c_str());
        void* pt = tribe_of_player(pid);
        if (!pt)
            return "error: no tribe for player\n";
        tribe_add_food(pt, delta);
        if (pid != 0)
            host_send_food(pid, true);
        char b[64];
        snprintf(b, sizeof b, "ok: pid=%u food=%d\n", (unsigned)pid, tribe_food(pt));
        return b;
    }

    if (a[1] == "unlock_tool" && a.size() >= 4 && is_host()) {
        uint8_t pid = (uint8_t)atoi(a[2].c_str());
        int kind = atoi(a[3].c_str());
        void* pt = tribe_of_player(pid);
        if (!pt || kind < kMinToolKind || kind > kMaxToolKind)
            return "error: bad args\n";
        if (pid == 0)
            unlock_tool_local(kind, pt);
        else if (a_SetToolPurchased && skg::readable(pt, 0x19DC))
            thiscall<void>(a_SetToolPurchased, pt, kind);
        host_send_food(pid, true);
        char b[64];
        snprintf(b, sizeof b, "ok: pid=%u tools=0x%X\n", (unsigned)pid, tribe_tools_mask(pt));
        return b;
    }

    if (a[1] == "swap_species" && a.size() >= 3 && is_host()) {
        uint8_t pid = (uint8_t)atoi(a[2].c_str());
        if (!rivals::valid_player(pid) || pid == 0 || !g_owned[pid].ptr)
            return "error: no guest tribe\n";
        void* prof = nullptr;
        if (a.size() >= 4) {
            uint32_t inst = (uint32_t)strtoul(a[3].c_str(), nullptr, 0);
            prof = game::species_by_key({inst, 0x2B978C46u, 0x40626200u});
        } else {
            // Pick host's player species or any distinct species profile from existing tribes/animals
            void* cur = a_SpeciesSlot ? thiscall<void*>(a_SpeciesSlot, g_owned[pid].ptr, 0) : nullptr;
            void* hsp = a_SpeciesSlot && g_owned[0].ptr ? thiscall<void*>(a_SpeciesSlot, g_owned[0].ptr, 0) : nullptr;
            if (hsp && hsp != cur)
                prof = hsp;
        }
        if (!prof)
            return "error: no species profile found\n";
        bool ok = swap_tribe_species(g_owned[pid], prof);
        host_broadcast_assign();
        char b[96];
        snprintf(b, sizeof b, "%s: pid=%u species=%08X\n", ok ? "ok" : "error", (unsigned)pid,
                 g_owned[pid].swapped_key[0]);
        return b;
    }

    if (a[1] == "hit_hut" && a.size() >= 5 && is_host()) {
        uint8_t att_p = (uint8_t)atoi(a[2].c_str());
        uint8_t vic_p = (uint8_t)atoi(a[3].c_str());
        float dmg = (float)atof(a[4].c_str());
        void* at = tribe_of_player(att_p);
        void* vt = tribe_of_player(vic_p);
        if (!at || !vt || !skg::readable(vt, 0x36C))
            return "error: tribe missing\n";
        void* hut = F<void*>(vt, 0x368);
        if (!hut || !skg::readable(hut, 0x160))
            return "error: hut missing\n";
        auto ams = tribe::members(at);
        void* att_comb = !ams.empty() ? (uint8_t*)ams[0] + 0x5A8 : nullptr;
        float before = F<float>(hut, 0x120 + 0x38);
        static const float zero[3] = {0, 0, 0};
        vcall<int>((uint8_t*)hut + 0x120, 0x18 / 4, dmg, tribe_pol(at), 0, zero, att_comb);
        float after = F<float>(hut, 0x120 + 0x38);
        char b[96];
        snprintf(b, sizeof b, "hut hp %.2f -> %.2f conquered=%d clamped=%u blocked=%u\n", before, after,
                 (int)tribe_conquered(vt), g_hut_clamped, g_pvp_blocked);
        return b;
    }

    if (a[1] == "hit_member" && a.size() >= 5 && is_host()) {
        uint8_t att_p = (uint8_t)atoi(a[2].c_str());
        uint8_t vic_p = (uint8_t)atoi(a[3].c_str());
        float dmg = (float)atof(a[4].c_str());
        void* at = tribe_of_player(att_p);
        void* vt = tribe_of_player(vic_p);
        if (!at || !vt)
            return "error: tribe missing\n";
        auto ams = tribe::members(at);
        auto vms = tribe::members(vt);
        if (ams.empty() || vms.empty())
            return "error: no members\n";
        void* ac = ams[0];
        for (void* c : ams) {
            if (c && !game::is_dead(c) && game::health(c) > 0.0f) {
                ac = c;
                break;
            }
        }
        void* vc = vms[0];
        for (void* c : vms) {
            if (c && !game::is_dead(c) && game::health(c) > 0.0f) {
                vc = c;
                break;
            }
        }
        float before = F<float>(vc, 0x5E0);
        static const float zero[3] = {0, 0, 0};
        vcall<int>((uint8_t*)vc + 0x5A8, 0x18 / 4, dmg, tribe_pol(at), 0, zero, (uint8_t*)ac + 0x5A8);
        float after = F<float>(vc, 0x5E0);
        char b[96];
        snprintf(b, sizeof b, "member hp %.2f -> %.2f pvp_hits=%u blocked=%u\n", before, after, g_pvp_hits, g_pvp_blocked);
        return b;
    }

    if (a[1] == "hit_tool" && a.size() >= 5 && is_host()) {
        uint8_t att_p = (uint8_t)atoi(a[2].c_str());
        uint8_t vic_p = (uint8_t)atoi(a[3].c_str());
        float dmg = (float)atof(a[4].c_str());
        void* at = tribe_of_player(att_p);
        void* vt = tribe_of_player(vic_p);
        if (!at || !vt)
            return "error: tribe missing\n";
        void* tool = nullptr;
        for (void* tl : tribe::tribe_tools(vt))
            if (tribe::tool_type(tl) != 10) { // skip indestructible firepit
                tool = tl;
                break;
            }
        if (!tool) {
            // Create a damageable tool (type 1 = Stone Axe rack) next to village for testing if none exists
            net::Vec3 p = tribe::tribe_pos(vt);
            p.x += 5.0f;
            tool = tribe::make_tool_in(vt, 1, p);
        }
        if (!tool || !skg::readable(tool, 0x160))
            return "error: no tool\n";
        auto ams = tribe::members(at);
        void* att_comb = !ams.empty() ? (uint8_t*)ams[0] + 0x5A8 : nullptr;
        float before = F<float>(tool, 0x120 + 0x38);
        static const float zero[3] = {0, 0, 0};
        vcall<int>((uint8_t*)tool + 0x120, 0x18 / 4, dmg, tribe_pol(at), 0, zero, att_comb);
        float after = F<float>(tool, 0x120 + 0x38);
        char b[96];
        snprintf(b, sizeof b, "tool hp %.2f -> %.2f tool_pvp=%u blocked=%u\n", before, after, g_tool_pvp_hits,
                 g_pvp_blocked);
        return b;
    }

    if ((a[1] == "music_win" || a[1] == "gift_win") && a.size() >= 4 && is_host()) {
        uint8_t pid = (uint8_t)atoi(a[2].c_str());
        uint32_t np = (uint32_t)strtoul(a[3].c_str(), nullptr, 0);
        void* pt = tribe_of_player(pid);
        void* mgr = rel_mgr();
        if (!pt || !mgr || !a_RelApply)
            return "error: missing tribe or rel mgr\n";
        uint32_t ev = (a[1] == "music_win") ? kRelEventSocial : kRelEventGift;
        (void)thiscall<float>(a_RelApply, mgr, np, tribe_pol(pt), ev, 1.0f);
        char b[96];
        snprintf(b, sizeof b, "ok: %s inf_bal=%d inf_earned=%u\n", a[1].c_str(),
                 (int)rivals::influence_balance(pid), g_inf_earned);
        return b;
    }

    if (a[1] == "set_pack_pets" && a.size() >= 4 && is_host()) {
        uint8_t pid = (uint8_t)atoi(a[2].c_str());
        if (!rivals::valid_player(pid))
            return "error: bad pid\n";
        std::vector<uint32_t> sps;
        if (a[3] == "auto") {
            struct Collect {
                std::vector<uint32_t> out;
                void* pt = nullptr;
            } col{{}, tribe::player_tribe()};
            game::for_each_object(
                [](void* o, uint32_t noun, void* u) {
                    auto* c = (Collect*)u;
                    if (noun != 0x18EB45Eu || !game::listed(o, 0x18EB45Eu) || game::is_dead(o))
                        return true;
                    if (tribe::is_domesticated_animal(o, c->pt))
                        return true;
                    uint32_t sp = game::species_instance(o);
                    if (sp && std::find(c->out.begin(), c->out.end(), sp) == c->out.end()) {
                        c->out.push_back(sp);
                        if (c->out.size() >= kMaxPackPets)
                            return false;
                    }
                    return true;
                },
                &col);
            sps = std::move(col.out);
        } else {
            for (size_t i = 3; i < a.size(); i++) {
                uint32_t sp = (uint32_t)strtoul(a[i].c_str(), nullptr, 0);
                if (sp)
                    sps.push_back(sp);
            }
        }
        record_pack_pets(g_pack_pets, pid, sps);
        g_pack_pets.spawned[pid] = false;
        host_spawn_pack_pets_for(pid);
        void* t = tribe_of_player(pid);
        auto dom = tribe_dom_pet_species(t);
        char b[160];
        snprintf(b, sizeof b, "ok: pid=%u recorded=%u dom_pets=%u pack_pets_spawned=%u\n", (unsigned)pid,
                 (unsigned)g_pack_pets.species[pid].size(), (unsigned)dom.size(), g_pack_pets_spawned);
        return b;
    }

    if (a[1] == "pets") {
        uint8_t pid = a.size() >= 3 ? (uint8_t)atoi(a[2].c_str()) : my_id();
        void* t = tribe_of_player(pid);
        auto dom = tribe_dom_pet_species(t);
        char b[160];
        snprintf(b, sizeof b, "pets pid=%u tribe=%p dom_count=%u pack_pets_spawned=%u recorded=%u spawned_flag=%d:",
                 (unsigned)pid, t, (unsigned)dom.size(), g_pack_pets_spawned,
                 rivals::valid_player(pid) ? (unsigned)g_pack_pets.species[pid].size() : 0u,
                 rivals::valid_player(pid) ? (int)g_pack_pets.spawned[pid] : 0);
        std::string out = b;
        for (uint32_t sp : dom) {
            char sb[16];
            snprintf(sb, sizeof sb, " %08X", sp);
            out += sb;
        }
        out += "\n";
        return out;
    }

    if (a[1] == "use_ability" && a.size() >= 3) {
        int ab_type = atoi(a[2].c_str());
        if (!tribe_logic::is_consequence_ability_type(ab_type))
            return "error: ability type must be 52..57\n";
        if (!is_guest())
            return "error: guest only\n";
        bool sent = tribe::guest_request_consequence_ability(ab_type);
        char b[96];
        snprintf(b, sizeof b, "%s: ability=%d (%s)\n", sent ? "ok" : "disabled", ab_type,
                 tribe_logic::consequence_ability_name(ab_type));
        return b;
    }

    if (a[1] == "wipe_timing" && a.size() >= 4) {
        g_wipe_respawn_delay = std::clamp(atof(a[2].c_str()), 0.5, 300.0);
        g_wipe_immune_duration = std::clamp(atof(a[3].c_str()), 0.0, 3600.0);
        char b[96];
        snprintf(b, sizeof b, "ok: respawn_delay=%.1f immune_duration=%.1f\n", g_wipe_respawn_delay,
                 g_wipe_immune_duration);
        return b;
    }

    if (a[1] == "wipe_all" && a.size() >= 4 && is_host()) {
        uint8_t att_p = (uint8_t)atoi(a[2].c_str());
        uint8_t vic_p = (uint8_t)atoi(a[3].c_str());
        void* at = tribe_of_player(att_p);
        void* vt = tribe_of_player(vic_p);
        if (!at || !vt)
            return "error: tribe missing\n";
        auto ams = tribe::members(at);
        void* att_comb = !ams.empty() ? (uint8_t*)ams[0] + 0x5A8 : nullptr;
        auto vms = tribe::members(vt);
        static const float zero[3] = {0, 0, 0};
        int hits = 0;
        for (void* vc : vms) {
            if (!vc || game::is_dead(vc) || game::health(vc) <= 0.0f)
                continue;
            vcall<int>((uint8_t*)vc + 0x5A8, 0x18 / 4, 5000.0f, tribe_pol(at), 0, zero, att_comb);
            // A chieftain knocked to ~0.3 HP while others were still alive takes one more hit once last
            if (!game::is_dead(vc) && game::health(vc) > 0.0f)
                vcall<int>((uint8_t*)vc + 0x5A8, 0x18 / 4, 5000.0f, tribe_pol(at), 0, zero, att_comb);
            hits++;
        }
        // Final pass in case chieftain survived first pass at 0.3 HP before last villager dropped
        for (void* vc : tribe::members(vt)) {
            if (!vc || game::is_dead(vc) || game::health(vc) <= 0.0f)
                continue;
            vcall<int>((uint8_t*)vc + 0x5A8, 0x18 / 4, 5000.0f, tribe_pol(at), 0, zero, att_comb);
        }
        char b[128];
        snprintf(b, sizeof b, "ok: hits=%d live_after=%d up=%d pop_go_blocked=%u\n", hits, live_tribe_members(vt),
                 tribe_up_count(vt), g_pop_gameover_blocked);
        return b;
    }

    if (a[1] == "kill_one" && a.size() >= 4 && is_host()) {
        uint8_t att_p = (uint8_t)atoi(a[2].c_str());
        uint8_t vic_p = (uint8_t)atoi(a[3].c_str());
        void* at = tribe_of_player(att_p);
        void* vt = tribe_of_player(vic_p);
        if (!at || !vt)
            return "error: tribe missing\n";
        auto ams = tribe::members(at);
        void* ac = !ams.empty() ? ams[0] : nullptr;
        for (void* c : ams)
            if (c && !game::is_dead(c) && game::health(c) > 0.0f) {
                ac = c;
                break;
            }
        void* att_comb = ac ? (uint8_t*)ac + 0x5A8 : nullptr;
        int live_before = live_tribe_members(vt);
        int up_before = tribe_up_count(vt);
        void* target = nullptr;
        void* ch = skg::readable(vt, 0x120) ? F<void*>(vt, 0x11C) : nullptr;
        // Prefer killing non-chieftain members first so the chieftain drops last (or alone)
        for (void* vc : tribe::members(vt)) {
            if (!vc || game::is_dead(vc) || game::health(vc) <= 0.0f)
                continue;
            if (!target || (target == ch && vc != ch))
                target = vc;
        }
        static const float zero[3] = {0, 0, 0};
        if (target) {
            vcall<int>((uint8_t*)target + 0x5A8, 0x18 / 4, 5000.0f, tribe_pol(at), 0, zero, att_comb);
            if (!game::is_dead(target) && game::health(target) > 0.0f)
                vcall<int>((uint8_t*)target + 0x5A8, 0x18 / 4, 5000.0f, tribe_pol(at), 0, zero, att_comb);
        }
        char b[160];
        snprintf(b, sizeof b, "ok: killed=%d live=%d->%d up=%d->%d pop_go_blocked=%u\n", target ? 1 : 0, live_before,
                 live_tribe_members(vt), up_before, tribe_up_count(vt), g_pop_gameover_blocked);
        return b;
    }

    if (a[1] == "baby" && a.size() >= 3 && is_host()) {
        uint8_t pid = (uint8_t)atoi(a[2].c_str());
        void* t = tribe_of_player(pid);
        if (!t)
            return "error: tribe missing\n";
        int before = (int)tribe::members(t).size();
        int f_before = tribe_food(t);
        if (pid == 0) {
            if (f_before < 10)
                return "error: not enough food\n";
            if (tribe::spawn_member(t, 0, 0))
                tribe_add_food(t, -10);
        } else {
            handle_guest_baby(pid, 0.f, 0.f, 0.f);
        }
        int after = (int)tribe::members(t).size();
        char b[96];
        snprintf(b, sizeof b, "%s: members %d->%d food %d->%d\n", after > before ? "ok" : "failed", before, after,
                 f_before, tribe_food(t));
        return b;
    }

    if (a[1] == "check_foodmat" && a.size() >= 3) {
        uint8_t pid = (uint8_t)atoi(a[2].c_str());
        void* vt = tribe_of_player(pid);
        if (!vt || !skg::readable(vt, 0x264))
            return "error: tribe missing\n";
        void* fm = F<void*>(vt, 0x260);
        if (!fm)
            return "error: foodmat missing\n";
        int res = override_choose_action(fm, 0x21);
        char b[96];
        snprintf(b, sizeof b, "foodmat pid=%u obj=%p action=%d blocked=%u\n", (unsigned)pid, fm, res, g_foodmat_blocked);
        return b;
    }

    return "usage: coop.tribe rivals [status|gift food|tool <pid> <val>|raid <pid> [amt]|form_npc|setrel <pid> <pol> <0..5>|addfood <pid> <d>|unlock_tool <pid> <k>|swap_species <pid> [key]|hit_hut|hit_member|hit_tool|music_win|gift_win|set_pack_pets <pid> <spec...|auto>|pets [pid]|use_ability <52..57>|wipe_timing <respawn_s> <immune_s>|wipe_all <att> <vic>|baby <pid>|check_foodmat <pid>]\n";
}

} // namespace tribe_rivals
