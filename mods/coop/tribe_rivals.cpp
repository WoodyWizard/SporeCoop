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
#include "lifecycle.h"
#include "modes.h"
#include "rivals.h"
#include "savestrip.h"
#include "species_line.h"
#include "sporekit_game.hpp"
#include "store.h"
#include "tribe.h"
#include "tribe_internal.h"
#include "tribe_rivals_logic.h"
#include "wire/rivals_faction.h"
#include "world.h"

using skg::thiscall;
using skg::vcall;

namespace tribe_rivals {

static const SkHost* H = nullptr;
static SkMod* M = nullptr;
static net::Session* S = nullptr;

static bool g_rivals_tribe_fix = true;
COOP_FIX("rivals.tribe", "BUG-1548", g_rivals_tribe_fix,
         "tribe: Tribe Rivals mode (NPC tribe takeover, guest perspective swap, per-player NPC relations & influence, "
         "war PvP with inviolable main village, alliance gifts, >50% stage end & Tribe draft)");

// ---- Game addresses (verified GOG 2024 dc04aee5, docs/re/tribe.md#tribe-rivals-re) ----
static uintptr_t a_SetPlayerTribe = 0;
static uintptr_t a_PlannerUpdate = 0;
static uintptr_t a_HutTD = 0;
static uintptr_t a_ToolTD = 0;
static uintptr_t a_SetConquered = 0;
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

constexpr uint32_t kTribeNoun = 0x18C6D19u;
constexpr uint32_t kCitizenNoun = 0x18EB4B7u;
constexpr uint32_t kHutNoun = 0x1E4DAAEu;
constexpr uint32_t kToolNoun = 0x18C8F0Cu;
constexpr uint32_t kRelEventSocial = 0x0530CF07u;
constexpr uint32_t kRelEventGift = 0x0530CF02u;
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
static uint32_t g_last_npc_count = 0;
static int g_last_win_needed = 1;

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
        auto dec = decide_damage(true, ao, vo, war, true);
        if (dec == DamageDecision::Block) {
            g_pvp_blocked++;
            return 0;
        }
        if (dec == DamageDecision::ClampMainHut) {
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
            if (clamped <= 0.0f)
                return 0;
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
        auto dec = decide_damage(true, ao, vo, war, false);
        if (dec == DamageDecision::Block) {
            g_pvp_blocked++;
            return 0;
        }
        if (dec == DamageDecision::AllowPvp)
            g_tool_pvp_hits++;
    }
    return o_ToolTD(comb, dmg, pol, type, pos, att);
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
    auto dec = decide_damage(true, ao, vo, war, false);
    if (dec == DamageDecision::Block) {
        g_pvp_blocked++;
        return false;
    }
    if (dec == DamageDecision::AllowPvp)
        g_pvp_hits++;
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
    if (!target_tribe)
        return 0;
    uint8_t to = owner_of_tribe(target_tribe);
    uint8_t me = my_id();
    if (!rivals::valid_player(to) || to == me)
        return 0;
    if (!rivals::at_war(me, to)) {
        // Block hostile actions (0x0A attack, 0x0C raid, 0x21 raid food) on Neutral / Allied rival tribes
        if (vanilla_action == 0x0A || vanilla_action == 0x0C || vanilla_action == 0x21)
            return -1;
        return 0;
    }
    // At War: allow attack on rival citizen, raid on rival hut/tool
    if (noun == kCitizenNoun && !game::is_dead(gd))
        return 0x0A;
    if (noun == kHutNoun || noun == kToolNoun)
        return 0x0C;
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
    void* at = tribe_of_player(att);
    void* vt = tribe_of_player(vic);
    if (!at || !vt)
        return;
    int32_t avail = tribe_food(vt);
    int32_t steal = std::min(want_amount, avail);
    if (steal <= 0)
        return;
    tribe_add_food(vt, -steal);
    tribe_add_food(at, steal);
    g_raids_done++;
    host_send_food(att, true);
    host_send_food(vic, true);
    net::Writer w;
    write_pvp_hit(w, {att, vic, PvpHitKind::RaidFood, 0, (float)steal});
    rivals::broadcast_faction(rivals::Stage::Tribe, wire::rivals_faction::SK_PVP_HIT, w);
    if (tribe::C.notice && S)
        tribe::C.notice(S->name_of(att) + " raided " + std::to_string(steal) + " food from " + S->name_of(vic));
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
    for (const auto& p : S->players()) {
        if (p.id == me || !rivals::valid_player(p.id))
            continue;
        if (rivals::allied(me, p.id)) {
            uint8_t to = p.id;
            rows.push_back({"  Ally gift to " + p.name, "Gift 25 food", [to] { (void)request_gift(to, GiftKind::Food, 25); }, 0});
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
    tribe_add_food(gt, -cost);
    void* baby = tribe::spawn_member(gt, 0, from);
    if (baby) {
        host_send_food(from, true);
        if (H && M)
            H->log(M, SK_INFO, "tribe_rivals: player %u bought baby in own tribe %p (food now %d)", (unsigned)from, gt,
                   tribe_food(gt));
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
        if (tribe_food(picked) < 25)
            tribe_set_food(picked, 25);
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
    }
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

LIFECYCLE_HOOK("tribe_rivals.session", lifecycle::kSessionEnd, -5, [](const lifecycle::Ctx&) { reset_runtime(true); }, false);
LIFECYCLE_HOOK("tribe_rivals.world", lifecycle::kWorldChange, -5, [](const lifecycle::Ctx&) { reset_runtime(false); }, false);

void tick(double now)
{
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
        host_deliver_creature_payouts();
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
           "tribe_rivals: initialized (fix=%d setpt=%d planner=%d hut=%d tool=%d conq=%d rel=%d)",
           (int)g_rivals_tribe_fix, (int)(a_SetPlayerTribe != 0), (int)(o_PlannerUpdate != nullptr),
           (int)(o_HutTD != nullptr), (int)(o_ToolTD != nullptr), (int)(o_SetConquered != nullptr),
           (int)(o_ApplyRel != nullptr));
}

std::string status_summary()
{
    char b[360];
    void* mt = tribe_of_player(my_id());
    snprintf(b, sizeof b,
             "tribe_rivals: fix=%d active=%d swapped=%d orig_pt=%p my_tribe=%p my_pol=%08X my_food=%d "
             "planner_skipped=%u pvp_hits=%u pvp_blocked=%u hut_clamped=%u tool_pvp=%u conquer_blocked=%u "
             "gifts=%u raids=%u inf_earned=%u draft_unlocked=%u draft_payouts=%u species_swaps=%u npcs=%u win_needed=%d\n",
             (int)g_rivals_tribe_fix, (int)active(), (int)g_swapped, g_orig_pt, mt, tribe_pol(mt),
             mt ? tribe_food(mt) : 0, g_planner_skipped, g_pvp_hits, g_pvp_blocked, g_hut_clamped, g_tool_pvp_hits,
             g_conquer_blocked, g_gifts_done, g_raids_done, g_inf_earned, g_draft_unlocked, g_draft_payouts_applied,
             g_species_swaps, g_last_npc_count, g_last_win_needed);
    std::string out = b;
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
            snprintf(b, sizeof b,
                     "  assign pid=%d pol=%08X ptr=%p food=%d members=%u tools=0x%X species=%08X swapped=%d planner_disabled=1\n",
                     p, ot.pol, ot.ptr, tribe_food(ot.ptr), (unsigned)tribe::members(ot.ptr).size(),
                     tribe_tools_mask(ot.ptr), sp_key, (int)ot.species_swapped);
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
            snprintf(b, sizeof b, "  assign pid=%u pol=%08X ptr=%p food=%d members=%u tools=0x%X\n",
                     (unsigned)e.player_id, e.tribe_pol, lt, lt ? tribe_food(lt) : 0,
                     lt ? (unsigned)tribe::members(lt).size() : 0u, lt ? tribe_tools_mask(lt) : 0u);
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
        void* vc = vms[0];
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

    return "usage: coop.tribe rivals [status|gift food|tool <pid> <val>|raid <pid> [amt]|form_npc|setrel <pid> <pol> <0..5>|addfood <pid> <d>|unlock_tool <pid> <k>|swap_species <pid> [key]|hit_hut|hit_member|hit_tool|music_win|gift_win]\n";
}

} // namespace tribe_rivals
