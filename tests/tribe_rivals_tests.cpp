// Native ASan+UBSan unit tests for tribe_rivals_logic.h (WP-R4 of 0.8.0, BUG-1548).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "tribe_rivals_logic.h"

using namespace tribe_rivals;

static int g_fail = 0;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                             \
        }                                                         \
    } while (0)

static void test_pick_rival_tribe()
{
    printf("tribe_rivals: base distance site picker (R2)\n");
    std::vector<TribeSite> sites = {
        {0x100, 0.f, 0.f, 0.f, true, false, false},    // host
        {0x200, 50.f, 0.f, 0.f, false, false, false},  // near (50)
        {0x300, 150.f, 0.f, 0.f, false, false, false}, // middle (150)
        {0x400, 300.f, 0.f, 0.f, false, false, false}, // far (300)
        {0x500, 10.f, 0.f, 0.f, false, true, false},   // conquered (excluded)
        {0x600, 20.f, 0.f, 0.f, false, false, true},   // already taken (excluded)
    };
    CHECK(pick_rival_tribe(sites, 0.f, 0.f, 0.f, rivals::BaseDistance::Near) == 1);
    CHECK(pick_rival_tribe(sites, 0.f, 0.f, 0.f, rivals::BaseDistance::Middle) == 2);
    CHECK(pick_rival_tribe(sites, 0.f, 0.f, 0.f, rivals::BaseDistance::Far) == 3);

    sites[1].taken = true;
    sites[2].taken = true;
    sites[3].taken = true;
    CHECK(pick_rival_tribe(sites, 0.f, 0.f, 0.f, rivals::BaseDistance::Middle) == -1);
}

static void test_npc_relations_and_influence()
{
    printf("tribe_rivals: per-player NPC relations & influence steps (R4)\n");
    NpcRelations rels;
    CHECK(rels.get(0, 0xA01) == NpcRel::Neutral);
    CHECK(rels.get(1, 0xA01) == NpcRel::Neutral);

    // Independent relations per player
    rels.set(0, 0xA01, NpcRel::Allied);
    CHECK(rels.get(0, 0xA01) == NpcRel::Allied);
    CHECK(rels.get(1, 0xA01) == NpcRel::Neutral);

    // TurnAgainst steps down 1 step at a time, stops at Hostile
    CHECK(rels.step_down(0, 0xA01) && rels.get(0, 0xA01) == NpcRel::Friendly);
    CHECK(rels.step_down(0, 0xA01) && rels.get(0, 0xA01) == NpcRel::Neutral);
    CHECK(rels.step_down(0, 0xA01) && rels.get(0, 0xA01) == NpcRel::Annoyed);
    CHECK(rels.step_down(0, 0xA01) && rels.get(0, 0xA01) == NpcRel::Hostile);
    CHECK(!rels.step_down(0, 0xA01));

    // SmoothOver steps up 1 step at a time, stops at Allied
    CHECK(rels.step_up(0, 0xA01) && rels.get(0, 0xA01) == NpcRel::Annoyed);
    CHECK(rels.step_up(0, 0xA01) && rels.get(0, 0xA01) == NpcRel::Neutral);
    CHECK(rels.step_up(0, 0xA01) && rels.get(0, 0xA01) == NpcRel::Friendly);
    CHECK(rels.step_up(0, 0xA01) && rels.get(0, 0xA01) == NpcRel::Allied);
    CHECK(!rels.step_up(0, 0xA01));

    // Conquered cannot be turned or smoothed
    rels.mark_conquered(1, 0xA01);
    CHECK(rels.get(1, 0xA01) == NpcRel::Conquered);
    CHECK(!rels.step_down(1, 0xA01));
    CHECK(!rels.step_up(1, 0xA01));

    // Threshold mapping
    CHECK(rel_target_float(NpcRel::Hostile, -6.f, -2.f, 2.f, 6.f) < -6.f);
    CHECK(rel_target_float(NpcRel::Neutral, -6.f, -2.f, 2.f, 6.f) == 0.f);
    CHECK(rel_target_float(NpcRel::Allied, -6.f, -2.f, 2.f, 6.f) > 6.f);
    CHECK(rel_from_game_level(4, false) == NpcRel::Allied);
    CHECK(rel_from_game_level(2, true) == NpcRel::Conquered);
}

static void test_stage_end_condition()
{
    printf("tribe_rivals: stage end >50%% NPC tribes & no-free tie-break (11.3.3)\n");
    bool present[rivals::kMaxPlayers] = {true, true, false, false};
    std::vector<uint32_t> npcs = {0xB1, 0xB2, 0xB3};
    NpcRelations rels;

    auto e0 = evaluate_stage_end(npcs, present, rels);
    CHECK(e0.total_npc == 3 && e0.win_needed == 2 && e0.free_npc == 3 && e0.winner == 255);

    rels.set(1, 0xB1, NpcRel::Allied);
    auto e1 = evaluate_stage_end(npcs, present, rels);
    CHECK(e1.subjugated[1] == 1 && e1.free_npc == 2 && e1.winner == 255);

    // Guest 1 conquers a second NPC tribe -> 2/3 (>50%) -> Guest 1 wins!
    rels.mark_conquered(1, 0xB2);
    auto e2 = evaluate_stage_end(npcs, present, rels);
    CHECK(e2.subjugated[1] == 2 && e2.allied[1] == 1 && e2.conquered[1] == 1 && e2.winner == 1);

    // 4 NPC tribes, 3 players: win_needed = 3, but no free NPC tribes left with split 2 / 1 / 1 -> leader wins
    bool pres3[rivals::kMaxPlayers] = {true, true, true, false};
    std::vector<uint32_t> npcs4 = {0xC1, 0xC2, 0xC3, 0xC4};
    NpcRelations rels4;
    rels4.set(0, 0xC1, NpcRel::Allied);
    rels4.set(1, 0xC2, NpcRel::Allied);
    rels4.set(1, 0xC3, NpcRel::Conquered);
    rels4.set(2, 0xC4, NpcRel::Allied);
    auto e4 = evaluate_stage_end(npcs4, pres3, rels4);
    CHECK(e4.win_needed == 3 && e4.free_npc == 0 && e4.winner == 1);

    // Empty NPC list -> no winner
    std::vector<uint32_t> empty;
    auto e_empty = evaluate_stage_end(empty, present, rels);
    CHECK(e_empty.winner == 255);
}

static void test_damage_and_inviolability()
{
    printf("tribe_rivals: war PvP & R7 main village inviolability\n");
    // Off -> passthrough
    CHECK(decide_damage(false, 0, 1, false, false) == DamageDecision::Passthrough);
    // Neutral / Allied -> Block member, tool, and hut
    CHECK(decide_damage(true, 0, 1, false, false) == DamageDecision::Block);
    CHECK(decide_damage(true, 0, 1, false, true) == DamageDecision::Block);
    // At War -> AllowPvp on members & extra huts, ClampMainHut on main hut
    CHECK(decide_damage(true, 0, 1, true, false) == DamageDecision::AllowPvp);
    CHECK(decide_damage(true, 0, 1, true, true) == DamageDecision::ClampMainHut);
    // NPC attacking player main hut -> ClampMainHut
    CHECK(decide_damage(true, kNoOwner, 1, false, true) == DamageDecision::ClampMainHut);
    // Player attacking NPC -> Passthrough
    CHECK(decide_damage(true, 1, kNoOwner, false, true) == DamageDecision::Passthrough);

    // Main hut damage clamp: never reaches 0
    CHECK(std::fabs(clamp_main_hut_damage(50.f, 100.f, 100.f) - 50.f) < 1e-4f);
    CHECK(std::fabs(clamp_main_hut_damage(200.f, 100.f, 100.f) - 90.f) < 1e-4f); // floor 10.0
    CHECK(std::fabs(clamp_main_hut_damage(50.f, 10.f, 100.f) - 0.f) < 1e-4f);    // already at floor
    CHECK(std::fabs(clamp_main_hut_damage(50.f, 5.f, 100.f) - 0.f) < 1e-4f);     // below floor
    CHECK(!can_conquer_tribe(true, 0));
    CHECK(!can_conquer_tribe(true, 1));
    CHECK(can_conquer_tribe(true, kNoOwner));
    CHECK(can_conquer_tribe(false, 0));
}

static void test_gifts_and_draft()
{
    printf("tribe_rivals: alliance gifts & Tribe draft items (11.3.3, 11.6)\n");
    uint32_t tools = (1u << 1) | (1u << 4) | (1u << 9);
    CHECK(!validate_gift(false, true, 0, 1, GiftKind::Food, 10, 50, tools).ok);
    CHECK(!validate_gift(true, false, 0, 1, GiftKind::Food, 10, 50, tools).ok);
    CHECK(!validate_gift(true, true, 0, 1, GiftKind::Food, 60, 50, tools).ok);
    CHECK(validate_gift(true, true, 0, 1, GiftKind::Food, 25, 50, tools).ok);
    CHECK(!validate_gift(true, true, 0, 1, GiftKind::Tool, 2, 50, tools).ok);
    CHECK(validate_gift(true, true, 0, 1, GiftKind::Tool, 9, 50, tools).ok);

    auto items = build_tribe_draft_items(tools);
    CHECK(items.size() == 3 && items[0].id == 1 && items[1].id == 4 && items[2].id == 9);
    auto fallback = build_tribe_draft_items(0);
    CHECK(fallback.size() == 2 && fallback[0].id == 1 && fallback[1].id == 5);
}

static void test_wire_roundtrip_and_bounds()
{
    printf("tribe_rivals: wire payload roundtrip & truncated bounds\n");
    {
        net::Writer w;
        std::vector<AssignEntry> in = {
            {0, 0x1111, 1.f, 2.f, 3.f, 0xAAA, 0x2B978C46, 0x40626200},
            {1, 0x2222, 4.f, 5.f, 6.f, 0xBBB, 0x2B978C46, 0x40626200},
        };
        write_assign(w, 7, in);
        net::Reader r(w.data.data(), w.data.size());
        uint32_t rev = 0;
        std::vector<AssignEntry> out;
        CHECK(read_assign(r, rev, out) && rev == 7 && out.size() == 2);
        CHECK(out[1].player_id == 1 && out[1].tribe_pol == 0x2222 && out[1].sp_inst == 0xBBB);

        net::Reader bad(w.data.data(), 3);
        CHECK(!read_assign(bad, rev, out));
    }
    {
        net::Writer w;
        std::vector<NpcRelEntry> in = {
            {0x3333, 10.f, 20.f, 30.f, NpcRel::Allied, true},
            {0x4444, 40.f, 50.f, 60.f, NpcRel::Hostile, false},
        };
        write_npc_rels(w, 1, in);
        net::Reader r(w.data.data(), w.data.size());
        uint8_t pid = 255;
        std::vector<NpcRelEntry> out;
        CHECK(read_npc_rels(r, pid, out) && pid == 1 && out.size() == 2);
        CHECK(out[0].rel == NpcRel::Allied && out[0].is_protected);
        CHECK(out[1].rel == NpcRel::Hostile && !out[1].is_protected);
    }
    {
        net::Writer w;
        GiftMsg in{0, 1, GiftKind::Food, 25};
        write_gift(w, in);
        net::Reader r(w.data.data(), w.data.size());
        GiftMsg out;
        CHECK(read_gift(r, out) && out.from_pid == 0 && out.to_pid == 1 && out.kind == GiftKind::Food && out.value == 25);
    }
    {
        net::Writer w;
        PvpHitMsg in{1, 0, PvpHitKind::RaidFood, 42, 15.f};
        write_pvp_hit(w, in);
        net::Reader r(w.data.data(), w.data.size());
        PvpHitMsg out;
        CHECK(read_pvp_hit(r, out) && out.attacker_pid == 1 && out.victim_pid == 0 && out.kind == PvpHitKind::RaidFood &&
              out.target_id == 42 && std::fabs(out.amount - 15.f) < 1e-4f);
    }
    {
        net::Writer w;
        DraftApplyMsg in{1, DraftApplyKind::ToolUnlock, 9};
        write_draft_apply(w, in);
        net::Reader r(w.data.data(), w.data.size());
        DraftApplyMsg out;
        CHECK(read_draft_apply(r, out) && out.target_pid == 1 && out.kind == DraftApplyKind::ToolUnlock && out.value == 9);
    }
    {
        net::Writer w;
        FoodSyncMsg in{CustomOp::FoodTotal, 1, 75, 0x22};
        write_food_sync(w, in);
        net::Reader r(w.data.data(), w.data.size());
        FoodSyncMsg out;
        CHECK(read_food_sync(r, out) && out.op == CustomOp::FoodTotal && out.player_id == 1 && out.value == 75 &&
              out.tools_mask == 0x22);
    }
}

static void test_war_order_routing()
{
    printf("tribe_rivals: War attack order routing vs Together FF (BUG-1554)\n");
    // Together mode: always delegates to ff_try_order
    CHECK(decide_host_order(false, 7, 1, 0, false) == OrderDecision::RunFriendlyFire);
    CHECK(decide_host_order(false, 7, 1, 0, true) == OrderDecision::RunFriendlyFire);

    // Tribe Rivals: guest attack (7) or raid (15) on host (0) at War -> AllowNative (never swallowed by FF!)
    CHECK(decide_host_order(true, 7, 1, 0, true) == OrderDecision::AllowNative);
    CHECK(decide_host_order(true, 15, 1, 0, true) == OrderDecision::AllowNative);
    CHECK(decide_host_order(true, 7, 2, 1, true) == OrderDecision::AllowNative);

    // Tribe Rivals: hostile order against rival while NOT at War -> BlockHostile
    CHECK(decide_host_order(true, 7, 1, 0, false) == OrderDecision::BlockHostile);
    CHECK(decide_host_order(true, 15, 1, 0, false) == OrderDecision::BlockHostile);
    CHECK(decide_host_order(true, 7, 0, 1, false) == OrderDecision::BlockHostile);

    // Tribe Rivals: attacks on NPC tribes, non-hostile actions, and self-target -> AllowNative
    CHECK(decide_host_order(true, 7, 1, kNoOwner, false) == OrderDecision::AllowNative);
    CHECK(decide_host_order(true, 15, 1, kNoOwner, false) == OrderDecision::AllowNative);
    CHECK(decide_host_order(true, 9, 1, 0, false) == OrderDecision::AllowNative);  // gift
    CHECK(decide_host_order(true, 2, 1, 0, false) == OrderDecision::AllowNative);  // gather
    CHECK(decide_host_order(true, 7, 1, 1, false) == OrderDecision::AllowNative);  // self
    CHECK(is_hostile_citizen_action(7) && is_hostile_citizen_action(15) && !is_hostile_citizen_action(9));
}

static void test_guest_tool_buy_routing()
{
    printf("tribe_rivals: guest tool/hut purchase routing & food charging (BUG-1554)\n");
    // Together mode: routes to host tribe 0, food handled by shared ledger
    auto tog = plan_tool_buy(false, 1, 1, 20.f, false, 0, true);
    CHECK(tog.ok && tog.target_owner == 0 && tog.food_cost == 0);

    // Tribe Rivals: guest buys Stone Axe (1, cost 15) near own hut with sufficient food
    auto g_ok = plan_tool_buy(true, 1, 1, 25.f, false, 30, true);
    CHECK(g_ok.ok && g_ok.target_owner == 1 && g_ok.food_cost == 15);

    // Tribe Rivals: guest buys Torch (3, cost 25) with insufficient food -> rejected
    auto g_poor = plan_tool_buy(true, 1, 3, 25.f, false, 20, true);
    CHECK(!g_poor.ok && g_poor.food_cost == 25);

    // Tribe Rivals: moving existing tool hut is free even with 0 food
    auto g_move = plan_tool_buy(true, 1, 3, 25.f, true, 0, true);
    CHECK(g_move.ok && g_move.target_owner == 1 && g_move.food_cost == 0);

    // Tribe Rivals: distance check is against target tribe's hut (rejects > 150 m or NaN)
    CHECK(!plan_tool_buy(true, 1, 1, 151.f, false, 100, true).ok);
    CHECK(!plan_tool_buy(true, 1, 1, NAN, false, 100, true).ok);
    CHECK(!plan_tool_buy(true, 1, 1, 10.f, false, 100, false).ok);

    // Host buys locally in palette (0 extra deduction on host), invalid tool types rejected
    auto h_ok = plan_tool_buy(true, 0, 6, 15.f, false, 0, true);
    CHECK(h_ok.ok && h_ok.target_owner == 0 && h_ok.food_cost == 0);
    CHECK(!plan_tool_buy(true, 1, 0, 10.f, false, 100, true).ok);
    CHECK(!plan_tool_buy(true, 1, 11, 10.f, false, 100, true).ok);
    CHECK(tool_buy_cost(10, false) == 0 && tool_buy_cost(9, false) == 20 && tool_buy_cost(6, false) == 25);
}

static void test_wsync_isolation_policy()
{
    printf("tribe_rivals: wsync isolation policy (BUG-1554)\n");
    // Together mode (both off)
    auto p_tog = wsync_policy(false, false);
    CHECK(p_tog.apply_tribe_level && p_tog.apply_tribe_goal && p_tog.push_guest_goal_target &&
          !p_tog.include_host_tribe_food && p_tog.apply_creature_nest);

    // Tribe Rivals active: disable host level/goal clobbering & guest->host goal_target, include host village food
    auto p_tr = wsync_policy(true, false);
    CHECK(!p_tr.apply_tribe_level && !p_tr.apply_tribe_goal && !p_tr.push_guest_goal_target &&
          p_tr.include_host_tribe_food && p_tr.apply_creature_nest);

    // Creature Rivals active: disable host nest migration sync
    auto p_cr = wsync_policy(false, true);
    CHECK(!p_cr.apply_creature_nest && p_cr.apply_tribe_level && p_cr.apply_tribe_goal);

    // Food sync targeting: Together syncs NPCs only; Tribe Rivals syncs host + NPCs + other rivals, never guest's own
    CHECK(!should_sync_tribe_food(false, true, false));
    CHECK(should_sync_tribe_food(false, false, false));
    CHECK(!should_sync_tribe_food(true, true, true));   // guest's own swapped tribe: NEVER clobber
    CHECK(!should_sync_tribe_food(true, false, true));  // guest's assigned tribe even before swap: NEVER clobber
    CHECK(should_sync_tribe_food(true, false, false));  // host's village (orig_pt) or NPC tribe: sync!
    CHECK(should_sync_tribe_food(true, true, false));   // pre-swap local player_tribe (host village): sync!
}

static void test_pvp_diplomacy_and_creature_payout()
{
    printf("tribe_rivals: PvP native diplomacy & Creature -> Tribe draft payout (BUG-1554)\n");
    CHECK(pvp_npc_rel_for(rivals::Relation::War) == NpcRel::Hostile);
    CHECK(pvp_npc_rel_for(rivals::Relation::Ally) == NpcRel::Allied);
    CHECK(pvp_npc_rel_for(rivals::Relation::Neutral) == NpcRel::Neutral);

    // Creature Rivals draft payout for guest winner (pid = 1)
    rivals::Draft d;
    d.pending_payout[1] = 250;
    d.pending_payout_stage[1] = rivals::Stage::Creature;
    d.pending_payout[0] = 100;
    d.pending_payout_stage[0] = rivals::Stage::Cell; // wrong stage: must NOT be consumed by Tribe!

    // Blocked while inactive, while draft is active, or before tribe is assigned
    CHECK(deliver_creature_payout_for(d, false, true, 1) == 0);
    d.active = true;
    CHECK(deliver_creature_payout_for(d, true, true, 1) == 0);
    d.active = false;
    CHECK(deliver_creature_payout_for(d, true, false, 1) == 0);

    // Does not steal Cell payout
    CHECK(deliver_creature_payout_for(d, true, true, 0) == 0);
    CHECK(d.pending_payout[0] == 100);

    // Delivers Creature payout once to guest, then 0 on subsequent calls
    CHECK(deliver_creature_payout_for(d, true, true, 1) == 250);
    CHECK(deliver_creature_payout_for(d, true, true, 1) == 0);

    // Delivers Creature payout to host (pid = 0) and clamps huge values
    d.pending_payout[0] = 5000;
    d.pending_payout_stage[0] = rivals::Stage::Creature;
    CHECK(deliver_creature_payout_for(d, true, true, 0) == kMaxDraftFoodPayout);
    CHECK(deliver_creature_payout_for(d, true, true, 0) == 0);
    CHECK(creature_draft_payout_food(0) == 0 && creature_draft_payout_food(-10) == 0);
}

static void test_npc_ui_list()
{
    printf("tribe_rivals: F9 Influence UI NPC tribe list generation (BUG-1554)\n");
    std::vector<NpcUiSource> src = {
        {0, NpcRel::Neutral, false, ""},                  // invalid pol -> skipped
        {0x101, NpcRel::Neutral, false, ""},              // #1 Neutral
        {0x102, NpcRel::Allied, false, "Blue Tribe"},     // #2 Allied (custom name)
        {0x103, NpcRel::Hostile, false, ""},              // #3 Hostile
        {0x104, NpcRel::Annoyed, false, ""},              // #4 Annoyed (hostile=true)
        {0x105, NpcRel::Friendly, false, ""},             // #5 Friendly
        {0x106, NpcRel::Conquered, false, ""},            // conquered rel -> skipped
        {0x107, NpcRel::Neutral, true, ""},               // conquered flag -> skipped
    };
    auto out = build_npc_ui_list(src);
    CHECK(out.size() == 5);

    CHECK(out[0].npc_id == 0x101 && out[0].label == "NPC Tribe #1 (Neutral)");
    CHECK(!out[0].allied && !out[0].hostile && out[0].can_turn && !out[0].can_protect && !out[0].can_smooth);

    CHECK(out[1].npc_id == 0x102 && out[1].label == "Blue Tribe (Allied)");
    CHECK(out[1].allied && !out[1].hostile && out[1].can_turn && out[1].can_protect && !out[1].can_smooth);

    CHECK(out[2].npc_id == 0x103 && out[2].label == "NPC Tribe #3 (Hostile)");
    CHECK(!out[2].allied && out[2].hostile && out[2].can_turn && !out[2].can_protect && out[2].can_smooth);

    CHECK(out[3].npc_id == 0x104 && out[3].label == "NPC Tribe #4 (Annoyed)");
    CHECK(!out[3].allied && out[3].hostile && out[3].can_turn && !out[3].can_protect && out[3].can_smooth);

    CHECK(out[4].npc_id == 0x105 && out[4].label == "NPC Tribe #5 (Friendly)");
    CHECK(!out[4].allied && !out[4].hostile && out[4].can_turn && !out[4].can_protect && !out[4].can_smooth);
}

int main()
{
    test_pick_rival_tribe();
    test_npc_relations_and_influence();
    test_stage_end_condition();
    test_damage_and_inviolability();
    test_gifts_and_draft();
    test_wire_roundtrip_and_bounds();
    test_war_order_routing();
    test_guest_tool_buy_routing();
    test_wsync_isolation_policy();
    test_pvp_diplomacy_and_creature_payout();
    test_npc_ui_list();
    if (g_fail) {
        printf("tribe_rivals_tests: %d FAILED\n", g_fail);
        return 1;
    }
    printf("tribe_rivals_tests: all passed\n");
    return 0;
}
