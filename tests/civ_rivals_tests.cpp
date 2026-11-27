// ASan + UBSan unit tests for Civilization Rivals portable logic (WP-R5, BUG-1549).
#include <cassert>
#include <cmath>
#include <cstdio>

#include "civ_rivals_logic.h"

using namespace civ_rivals_logic;

static void test_base_distance_selection()
{
    Vec3 host{0, 0, 0};
    std::vector<BaseCandidate> cands = {
        {0x2003, {300, 0, 0}, 1},
        {0x2001, {100, 0, 0}, 1},
        {0x2002, {200, 0, 0}, 1},
        {0x2004, {400, 0, 0}, 0}, // 0 cities: ignored
    };
    assert(select_base_nation(host, cands, rivals::BaseDistance::Near) == 0x2001);
    assert(select_base_nation(host, cands, rivals::BaseDistance::Middle) == 0x2002);
    assert(select_base_nation(host, cands, rivals::BaseDistance::Far) == 0x2003);

    // Already-taken exclusion
    assert(select_base_nation(host, cands, rivals::BaseDistance::Near, {0x2001}) == 0x2002);
    assert(select_base_nation(host, cands, rivals::BaseDistance::Far, {0x2003}) == 0x2002);
    assert(select_base_nation(host, cands, rivals::BaseDistance::Middle, {0x2001, 0x2002, 0x2003}) == 0);

    // Tie breaking by pol_id
    std::vector<BaseCandidate> tied = {
        {0x3002, {100, 0, 0}, 1},
        {0x3001, {100, 0, 0}, 1},
    };
    assert(select_base_nation(host, tied, rivals::BaseDistance::Near) == 0x3001);
    assert(select_base_nation(host, tied, rivals::BaseDistance::Far) == 0x3002);
}

static void test_combat_capture_and_capital_inviolability()
{
    // Neutral/Allied players cannot damage each other
    assert(pvp_damage_gate(0, 1, false, false, 0) == CombatGate::Block);
    assert(pvp_damage_gate(1, 0, false, true, 0) == CombatGate::Block);
    // At War: ordinary vehicle/city damage allowed
    assert(pvp_damage_gate(0, 1, true, false, 0) == CombatGate::Allow);
    // At War: superweapon damage types 0xC and 0xD blocked against ANY capital
    assert(pvp_damage_gate(0, 1, true, true, 0xC) == CombatGate::Block);
    assert(pvp_damage_gate(0, 1, true, true, 0xD) == CombatGate::Block);
    assert(pvp_damage_gate(0xFE, 1, false, true, 0xC) == CombatGate::Block);
    // NPC interactions pass through
    assert(pvp_damage_gate(0, 0xFE, false, false, 0) == CombatGate::Pass);
    assert(pvp_damage_gate(0xFE, 0, false, false, 0) == CombatGate::Pass);

    // City capture / conversion / buy gate
    // Any capital is strictly inviolable even at War or by NPC
    assert(city_capture_blocked(0, 1, true, true) == true);
    assert(city_capture_blocked(1, 0, true, true) == true);
    assert(city_capture_blocked(0xFE, 1, false, true) == true);
    // Ordinary rival city: blocked while Neutral/Allied, allowed at War
    assert(city_capture_blocked(0, 1, false, false) == true);
    assert(city_capture_blocked(0, 1, true, false) == false);
    // NPC ordinary city: always allowed
    assert(city_capture_blocked(0, 0xFF, false, false) == false);
    assert(city_capture_blocked(1, 0xFF, false, false) == false);

    // Superweapon targeting gate against rival capitals
    std::array<PlayerNation, kMaxPlayers> nations{};
    nations[0] = {0, 0x1001, {0, 0, 500}, 4000.0f, true};
    nations[1] = {1, 0x2001, {500, 0, 0}, 3500.0f, true};

    uint8_t prot = 255;
    // Targeted superweapon near rival's capital -> blocked
    assert(superweapon_blocked_by_capital(0, 4, true, {480, 0, 10}, nations, &prot) == true);
    assert(prot == 1);
    // Targeted superweapon far from rival's capital (e.g. on an ordinary NPC city) -> allowed
    assert(superweapon_blocked_by_capital(0, 4, true, {0, 500, 0}, nations, &prot) == false);
    // Global superweapons (2, 5, 8) blocked while rival has a capital
    assert(superweapon_blocked_by_capital(0, 2, false, {0, 0, 0}, nations, &prot) == true);
    assert(superweapon_blocked_by_capital(1, 5, false, {0, 0, 0}, nations, &prot) == true);
    assert(prot == 0);
    assert(superweapon_blocked_by_capital(0, 8, false, {0, 0, 0}, nations, &prot) == true);
    // Capital position matcher
    uint8_t cap_owner = 255;
    assert(is_capital_city_pos({505, 2, -3}, nations, &cap_owner) == true && cap_owner == 1);
    assert(is_capital_city_pos({200, 200, 200}, nations, &cap_owner) == false);
}

static void test_npc_relations_and_influence()
{
    NpcRelationTable tbl;
    tbl.ensure_npc(0x3001, 1.0f);
    tbl.ensure_npc(0x3002, 4.0f);
    assert(tbl.has_npc(0x3001));
    assert(std::fabs(tbl.get(0x3001, 0) - 1.0f) < 1e-4f);
    assert(std::fabs(tbl.get(0x3001, 1) - 1.0f) < 1e-4f);
    assert(tbl.allied_count(0) == 1);
    assert(tbl.allied_count(1) == 1);

    // TurnAgainst affects only rival (player 1), leaving player 0 unchanged
    float nv = 0.0f;
    assert(tbl.apply_turn_against(0x3002, 1, kNpcRelStep, &nv));
    assert(std::fabs(nv - 1.5f) < 1e-4f);
    assert(std::fabs(tbl.get(0x3002, 0) - 4.0f) < 1e-4f);
    assert(tbl.allied_count(0) == 1);
    assert(tbl.allied_count(1) == 0);

    // SmoothOver raises actor (player 1) back up
    assert(tbl.apply_smooth_over(0x3002, 1, kNpcRelStep, &nv));
    assert(std::fabs(nv - 4.0f) < 1e-4f);
    assert(tbl.allied_count(1) == 1);

    // Clamping at [-10, +10]
    for (int i = 0; i < 10; i++)
        tbl.apply_turn_against(0x3001, 1);
    assert(std::fabs(tbl.get(0x3001, 1) - (-10.0f)) < 1e-4f);
    assert(rel_category(tbl.get(0x3001, 1)) == 0);

    // Unknown NPC rejected
    assert(!tbl.apply_turn_against(0x9999, 1));
    assert(!tbl.apply_smooth_over(0x9999, 0));

    // Influence buy cost scaling
    assert(std::fabs(influence_buy_civ_cost(50) - 500.0f) < 1e-4f);
    assert(std::fabs(influence_buy_civ_cost(75) - 750.0f) < 1e-4f);
}

static void test_gifts_and_stage_win()
{
    assert(can_send_gift(0, 1, true, GiftKind::Money, 500.0f, 1000.0f) == true);
    assert(can_send_gift(0, 1, true, GiftKind::Spice, 250.0f, 1000.0f) == true);
    assert(can_send_gift(0, 1, false, GiftKind::Money, 500.0f, 1000.0f) == false); // not allied
    assert(can_send_gift(0, 1, true, GiftKind::Money, 1500.0f, 1000.0f) == false); // insufficient funds
    assert(can_send_gift(0, 0, true, GiftKind::Money, 100.0f, 1000.0f) == false);  // self
    assert(can_send_gift(0, 1, true, GiftKind::Money, -50.0f, 1000.0f) == false);  // negative

    // Stage win: 2 contestable NPC cities -> need > 1 (i.e. 2)
    CityCounts c{};
    c.present[0] = c.present[1] = true;
    c.total_cities[0] = 2;
    c.non_cap_cities[0] = 1;
    c.total_cities[1] = 1;
    c.non_cap_cities[1] = 0;
    c.free_npc_cities = 1;
    c.total_contestable = 2;
    auto e1 = evaluate_stage_win(c);
    assert(!e1.has_winner); // 1/2 = 50%, not >50%

    // Host captures 2nd NPC city -> 2/2 > 50% -> host wins
    c.total_cities[0] = 3;
    c.non_cap_cities[0] = 2;
    c.free_npc_cities = 0;
    auto e2 = evaluate_stage_win(c);
    assert(e2.has_winner && e2.winner == 0 && e2.qualified[0]);

    // Tie when no free NPC cities remain (1 each of 2) -> higher wealth wins
    CityCounts c3{};
    c3.present[0] = c3.present[1] = true;
    c3.total_cities[0] = 2;
    c3.non_cap_cities[0] = 1;
    c3.wealth[0] = 2000.0f;
    c3.total_cities[1] = 2;
    c3.non_cap_cities[1] = 1;
    c3.wealth[1] = 5000.0f;
    c3.free_npc_cities = 0;
    c3.total_contestable = 2;
    auto e3 = evaluate_stage_win(c3);
    assert(e3.has_winner && e3.winner == 1 && e3.all_npc_taken);

    // 4 contestable NPC cities -> need 3
    assert(cities_needed_to_win(4) == 3);
    assert(cities_needed_to_win(5) == 3);
    assert(cities_needed_to_win(6) == 4);
}

static void test_draft_and_wire_codecs()
{
    auto items = build_civ_draft_items({0x7D433FADu, 0x4E3F7777u});
    assert(items.size() == 2);
    assert(items[0].id == 0x7D433FADu && items[0].kind == 1);
    assert(items[1].id == 0x4E3F7777u && items[1].kind == 2);
    auto fb = build_civ_draft_items({});
    assert(fb.size() >= 4);

    // SK_ASSIGN roundtrip
    std::array<PlayerNation, kMaxPlayers> a{}, b{};
    a[0] = {0, 0x1001, {10, 20, 30}, 4500.0f, true};
    a[1] = {1, 0x2002, {-50, 60, 70}, 3200.0f, true};
    net::Writer w1;
    write_assign(w1, a);
    net::Reader r1(w1.data.data(), w1.data.size());
    assert(read_assign(r1, b));
    assert(b[0].assigned && b[0].pol_id == 0x1001 && std::fabs(b[0].wealth - 4500.0f) < 1e-3f);
    assert(b[1].assigned && b[1].pol_id == 0x2002 && std::fabs(b[1].cap_pos.x - (-50.0f)) < 1e-3f);
    assert(!b[2].assigned);

    // SK_NPC_RELS roundtrip
    NpcRelationTable tbl;
    tbl.ensure_npc(0x3001, 2.5f);
    tbl.set(0x3001, 1, -4.0f);
    net::Writer w2;
    write_npc_rels(w2, 1, tbl);
    net::Reader r2(w2.data.data(), w2.data.size());
    uint8_t tpid = 255;
    std::vector<std::pair<uint32_t, float>> rows;
    assert(read_npc_rels(r2, tpid, rows));
    assert(tpid == 1 && rows.size() == 1 && rows[0].first == 0x3001 && std::fabs(rows[0].second - (-4.0f)) < 1e-3f);

    // SK_GIFT roundtrip + malformed rejection
    net::Writer w3;
    write_gift(w3, {1, GiftKind::Spice, 750.0f});
    net::Reader r3(w3.data.data(), w3.data.size());
    GiftMsg gm;
    assert(read_gift(r3, gm) && gm.target_pid == 1 && gm.kind == GiftKind::Spice && std::fabs(gm.amount - 750.0f) < 1e-3f);

    net::Writer w3_bad;
    w3_bad.u8(1);
    w3_bad.u8(99);
    w3_bad.f32(100.0f);
    net::Reader r3_bad(w3_bad.data.data(), w3_bad.data.size());
    assert(!read_gift(r3_bad, gm));

    // SK_DRAFT_APPLY roundtrip
    net::Writer w4;
    write_draft_apply(w4, {1, 0x7D433FADu, {0x11111111u, 0x24682294u, 0x40646200u}});
    net::Reader r4(w4.data.data(), w4.data.size());
    DraftApplyMsg dm;
    assert(read_draft_apply(r4, dm) && dm.target_pid == 1 && dm.design_id == 0x7D433FADu && dm.key[0] == 0x11111111u);
}

static void test_bidirectional_native_npc_diplomacy_sync()
{
    NpcRelationTable tbl;
    const uint32_t npc = 0x5001u;

    // 1. First observation initializes from native without overwriting it.
    auto r0 = tbl.sync_native_slot(npc, 0, 0.5f);
    assert(r0.action == NativeSyncAction::ReadInit);
    assert(std::fabs(tbl.get(npc, 0) - 0.5f) < 1e-4f);
    assert(!tbl.is_dirty_native(npc, 0));

    // Player 1 initialized independently with a different native value.
    auto r1 = tbl.sync_native_slot(npc, 1, -2.0f);
    assert(r1.action == NativeSyncAction::ReadInit);
    assert(std::fabs(tbl.get(npc, 1) - (-2.0f)) < 1e-4f);

    // 2. Subsequent 1 s tick with unchanged native value returns None (never overwrites).
    auto r_idle = tbl.sync_native_slot(npc, 0, 0.52f); // within kNativeSyncEps
    assert(r_idle.action == NativeSyncAction::None);

    // 3. Real in-game gift raises native relation (+2.0 -> 2.5): read back + award diplomacy influence!
    auto r_gift = tbl.sync_native_slot(npc, 0, 2.5f);
    assert(r_gift.action == NativeSyncAction::ReadBack);
    assert(std::fabs(tbl.get(npc, 0) - 2.5f) < 1e-4f);
    assert(!r_gift.newly_allied);
    assert(r_gift.influence_award == kDiplomacyImproveInfluenceGain);

    // 4. Real in-game alliance crosses >= kNpcAllyThreshold (3.0): read back + award alliance influence!
    auto r_ally = tbl.sync_native_slot(npc, 0, 5.0f);
    assert(r_ally.action == NativeSyncAction::ReadBack);
    assert(std::fabs(tbl.get(npc, 0) - 5.0f) < 1e-4f);
    assert(r_ally.newly_allied);
    assert(r_ally.influence_award == kDiplomacyAllyInfluenceGain);

    // 5. Real in-game insult / war declaration drops native relation (-6.0): read back, 0 influence, not wiped!
    auto r_war = tbl.sync_native_slot(npc, 1, -7.0f);
    assert(r_war.action == NativeSyncAction::ReadBack);
    assert(std::fabs(tbl.get(npc, 1) - (-7.0f)) < 1e-4f);
    assert(r_war.influence_award == 0);
    assert(tbl.sync_native_slot(npc, 1, -7.0f).action == NativeSyncAction::None);

    // 6. Rivals TurnAgainst / SmoothOver marks dirty -> PushNative; confirm_native_push prevents false readback/award.
    float nv = 0.0f;
    assert(tbl.apply_turn_against(npc, 0, kNpcRelStep, &nv));
    assert(std::fabs(nv - 2.5f) < 1e-4f);
    assert(tbl.is_dirty_native(npc, 0));
    auto r_push = tbl.sync_native_slot(npc, 0, 5.0f);
    assert(r_push.action == NativeSyncAction::PushNative);
    assert(std::fabs(r_push.target_native - 2.5f) < 1e-4f);
    tbl.confirm_native_push(npc, 0, 2.5f);
    assert(!tbl.is_dirty_native(npc, 0));
    assert(tbl.sync_native_slot(npc, 0, 2.5f).action == NativeSyncAction::None);

    // SmoothOver confirmed immediately does NOT award real-diplomacy influence on next tick.
    assert(tbl.apply_smooth_over(npc, 0, kNpcRelStep, &nv));
    assert(std::fabs(nv - 5.0f) < 1e-4f);
    tbl.confirm_native_push(npc, 0, 5.0f);
    auto r_after_smooth = tbl.sync_native_slot(npc, 0, 5.0f);
    assert(r_after_smooth.action == NativeSyncAction::None && r_after_smooth.influence_award == 0);
}

static void test_guest_direct_spend_and_handoff_and_payout()
{
    // Direct wealth deduction avoids native SpendMoney NPC budget refill on mIsPlayer==0 civs.
    float wealth = 3000.0f, dom = 450.0f, exp = 250.0f;
    assert(!deduct_direct_wealth(wealth, dom, exp, 3500.0f));
    assert(std::fabs(wealth - 3000.0f) < 1e-4f);
    assert(deduct_direct_wealth(wealth, dom, exp, 800.0f));
    assert(std::fabs(wealth - 2200.0f) < 1e-4f && dom == 0.0f && exp == 0.0f);

    // spend_direct_wealth clamps at >= 0 and zeroes NPC budgets.
    dom = 100.0f;
    exp = 200.0f;
    spend_direct_wealth(wealth, dom, exp, 5000.0f);
    assert(wealth == 0.0f && dom == 0.0f && exp == 0.0f);

    // Tribe -> Civ draft payout scaling
    assert(std::fabs(civ_draft_payout_money(0) - 0.0f) < 1e-4f);
    assert(std::fabs(civ_draft_payout_money(-50) - 0.0f) < 1e-4f);
    assert(std::fabs(civ_draft_payout_money(100) - 1000.0f) < 1e-4f);
    assert(std::fabs(civ_draft_payout_money(150) - 1500.0f) < 1e-4f);
    assert(std::fabs(civ_draft_payout_money(250) - 2500.0f) < 1e-4f);

    // Civ -> Space city handoff count clamped to 1..3 (docs/11-rivals.md 11.3.5)
    assert(clamp_space_handoff_cities(-2) == 1);
    assert(clamp_space_handoff_cities(0) == 1);
    assert(clamp_space_handoff_cities(1) == 1);
    assert(clamp_space_handoff_cities(2) == 2);
    assert(clamp_space_handoff_cities(3) == 3);
    assert(clamp_space_handoff_cities(8) == 3);
}

static void test_civ_npc_ui_list()
{
    assert(std::string(classify_civ_color(0.95f, 0.1f, 0.1f)) == "Red");
    assert(std::string(classify_civ_color(0.1f, 0.2f, 0.95f)) == "Blue");
    assert(std::string(classify_civ_color(0.1f, 0.85f, 0.2f)) == "Green");
    assert(std::string(classify_civ_color(0.9f, 0.88f, 0.9f)) == "White");

    std::vector<CivNpcSnapshot> snaps;
    // Allied Red Military nation (2 cities)
    snaps.push_back({0x6001u, 0, 2, true, 0.9f, 0.1f, 0.1f, 4.5f, true});
    // Hostile Blue Economic nation (1 city)
    snaps.push_back({0x6002u, 2, 1, true, 0.1f, 0.2f, 0.95f, -4.0f, true});
    // Neutral Religious nation without color
    snaps.push_back({0x6003u, 1, 3, false, 0, 0, 0, 0.5f, false});
    // Max-relation nation (cannot smooth further)
    snaps.push_back({0x6004u, -1, 1, false, 0, 0, 0, 10.0f, true});

    auto list = build_civ_npc_ui_list(snaps, true);
    assert(list.size() == 4);
    // Sorted highest relation first: 0x6004 (+10.0), 0x6001 (+4.5), 0x6003 (+0.5), 0x6002 (-4.0)
    assert(list[0].npc_id == 0x6004u && list[0].allied && !list[0].hostile && list[0].can_protect && !list[0].can_smooth &&
           list[0].can_turn);
    assert(list[0].label.find("0x") == std::string::npos);

    assert(list[1].npc_id == 0x6001u && list[1].allied && !list[1].hostile && list[1].can_protect && list[1].can_smooth &&
           list[1].can_turn);
    assert(list[1].label.find("Red Military") != std::string::npos);
    assert(list[1].label.find("2 cities") != std::string::npos);

    assert(list[2].npc_id == 0x6003u && !list[2].allied && !list[2].hostile && !list[2].can_protect &&
           list[2].can_smooth && !list[2].can_turn);
    assert(list[2].label.find("Religious Nation") != std::string::npos);

    assert(list[3].npc_id == 0x6002u && !list[3].allied && list[3].hostile && !list[3].can_protect &&
           list[3].can_smooth && list[3].can_turn);
    assert(list[3].label.find("Blue Economic") != std::string::npos);
    assert(list[3].label.find("1 city") != std::string::npos);

    // Without rival, can_turn is false for all entries
    auto solo = build_civ_npc_ui_list(snaps, false);
    for (const auto& e : solo)
        assert(!e.can_turn);
}

static void test_specialty_conversion_and_superweapon_spend()
{
    // BUG-1563 Part A: native specialty validation, conversion decision, and vehicle editor mapping
    assert(valid_native_specialty(kCitySpecMilitary));
    assert(valid_native_specialty(kCitySpecReligious));
    assert(valid_native_specialty(kCitySpecEconomic));
    assert(!valid_native_specialty(-1));
    assert(!valid_native_specialty(3));

    // Mismatch requires conversion; match or invalid desired specialty does not
    assert(needs_specialty_conversion(kCitySpecMilitary, kCitySpecEconomic));
    assert(needs_specialty_conversion(kCitySpecEconomic, kCitySpecReligious));
    assert(needs_specialty_conversion(-1, kCitySpecMilitary));
    assert(!needs_specialty_conversion(kCitySpecEconomic, kCitySpecEconomic));
    assert(!needs_specialty_conversion(kCitySpecMilitary, -1));
    assert(!needs_specialty_conversion(kCitySpecMilitary, 99));

    // Land vehicle editor asset IDs match native specialties
    assert(land_vehicle_editor_for_specialty(kCitySpecMilitary) == 0x7D433FADu);
    assert(land_vehicle_editor_for_specialty(kCitySpecReligious) == 0x9AD7D4AAu);
    assert(land_vehicle_editor_for_specialty(kCitySpecEconomic) == 0xF670AA43u);
    assert(land_vehicle_editor_for_specialty(-1) == 0u);

    // SK_ASSIGN roundtrips cap_specialty AND remains backward-compatible with legacy payloads
    std::array<PlayerNation, kMaxPlayers> a{}, b{};
    a[0] = {0, 0x1001, {10, 20, 30}, 4500.0f, true, kCitySpecMilitary};
    a[1] = {1, 0x2002, {-50, 60, 70}, 3200.0f, true, kCitySpecEconomic};
    net::Writer w_new;
    write_assign(w_new, a);
    net::Reader r_new(w_new.data.data(), w_new.data.size());
    assert(read_assign(r_new, b));
    assert(b[0].cap_specialty == kCitySpecMilitary);
    assert(b[1].cap_specialty == kCitySpecEconomic);
    assert(!b[2].assigned && b[2].cap_specialty == -1);

    // Legacy SK_ASSIGN payload without trailing specialty bytes decodes with cap_specialty == -1
    net::Writer w_old;
    w_old.u8(1);
    w_old.u8(1);
    w_old.u32(0x2002);
    w_old.f32(1.0f);
    w_old.f32(2.0f);
    w_old.f32(3.0f);
    w_old.f32(3000.0f);
    std::array<PlayerNation, kMaxPlayers> c{};
    net::Reader r_old(w_old.data.data(), w_old.data.size());
    assert(read_assign(r_old, c));
    assert(c[1].assigned && c[1].pol_id == 0x2002 && c[1].cap_specialty == -1);

    // BUG-1563 Part B: superweapon cost table, affordability, and double-spend / budget-refill guard
    assert(std::fabs(superweapon_cost(0, 3) - 4000.0f) < 1e-4f);
    assert(std::fabs(superweapon_cost(6, 3) - 4000.0f) < 1e-4f);
    assert(std::fabs(superweapon_cost(11, 3) - 4000.0f) < 1e-4f);
    assert(std::fabs(superweapon_cost(1, 3) - 16000.0f) < 1e-4f);
    assert(std::fabs(superweapon_cost(4, 3) - 16000.0f) < 1e-4f);
    assert(std::fabs(superweapon_cost(7, 3) - 16000.0f) < 1e-4f);
    assert(std::fabs(superweapon_cost(3, 3) - 2000.0f) < 1e-4f);
    assert(std::fabs(superweapon_cost(9, 3) - 2000.0f) < 1e-4f);
    assert(std::fabs(superweapon_cost(10, 3) - 2000.0f) < 1e-4f);
    assert(std::fabs(superweapon_cost(2, 0) - 12000.0f) < 1e-4f);
    assert(std::fabs(superweapon_cost(5, 4) - 48000.0f) < 1e-4f);
    assert(std::fabs(superweapon_cost(8, 2) - 24000.0f) < 1e-4f);
    assert(superweapon_cost(-1, 3) < 0.0f);
    assert(superweapon_cost(12, 3) < 0.0f);

    // Affordability rejection
    assert(!can_afford_superweapon(1999.0f, 2000.0f));
    assert(can_afford_superweapon(2000.0f, 2000.0f));
    assert(can_afford_superweapon(5000.0f, 2000.0f));
    assert(!can_afford_superweapon(5000.0f, -1.0f));

    // Finalizing wealth clamps to before - cost (prevents both double-spend and NPC budget refill)
    float dom = 750.0f, exp = 500.0f;
    float after = finalize_superweapon_wealth(5000.0f, 2000.0f, dom, exp);
    assert(std::fabs(after - 3000.0f) < 1e-4f && dom == 0.0f && exp == 0.0f);

    // Never goes negative
    dom = 300.0f;
    exp = 200.0f;
    float clamped = finalize_superweapon_wealth(1500.0f, 2000.0f, dom, exp);
    assert(clamped == 0.0f && dom == 0.0f && exp == 0.0f);
}

static void test_rejoin_nation_memory_and_store_codec()
{
    // BUG-1581: player_key -> pol_id persists across disconnect/reconnect and store reload
    NationMemory mem;
    mem.remember("dermogen", 0x2282u, {100.0f, 200.0f, 300.0f}, kCitySpecMilitary);
    assert(mem.has("dermogen"));
    assert(mem.is_reserved_pol(0x2282u));
    assert(!mem.is_reserved_pol(0x2285u));
    assert(mem.is_reserved_for_other(0x2282u, "newcomer"));
    assert(!mem.is_reserved_for_other(0x2282u, "dermogen"));

    std::vector<BaseCandidate> live = {
        {0x2282u, {100.0f, 200.0f, 300.0f}, 1, 10, false},
        {0x2285u, {-200.0f, 50.0f, 100.0f}, 1, 10, false},
    };
    // Exact pol_id reclaim on reconnect
    assert(mem.find_reclaim("dermogen", live, {}) == 0x2282u);
    // Already actively taken by someone else -> cannot double-assign
    assert(mem.find_reclaim("dermogen", live, {0x2282u}) == 0u);

    // Fallback by capital position within kCapitalMatchRadius (e.g. pol_id changed after reload)
    std::vector<BaseCandidate> reloaded = {
        {0x3009u, {105.0f, 198.0f, 302.0f}, 1, 10, false},
        {0x3010u, {-200.0f, 50.0f, 100.0f}, 1, 10, false},
    };
    assert(mem.find_reclaim("dermogen", reloaded, {}) == 0x3009u);

    // Zero-city (eliminated) nation is not reclaimed
    std::vector<BaseCandidate> dead = {
        {0x2282u, {100.0f, 200.0f, 300.0f}, 0, 10, false},
    };
    assert(mem.find_reclaim("dermogen", dead, {}) == 0u);

    // Key adopt migrates ownership cleanly
    mem.adopt("dermogen", "dermogen_v2");
    assert(!mem.has("dermogen") && mem.has("dermogen_v2"));
    assert(mem.find_reclaim("dermogen_v2", live, {}) == 0x2282u);

    // Store persistence codec roundtrip + malformed rejection
    net::Writer w;
    write_saved_nations(w, mem);
    net::Reader r(w.data.data(), w.data.size());
    NationMemory out;
    assert(read_saved_nations(r, out));
    assert(out.has("dermogen_v2"));
    assert(out.by_key["dermogen_v2"].pol_id == 0x2282u);
    assert(out.by_key["dermogen_v2"].cap_specialty == kCitySpecMilitary);

    net::Writer w_bad;
    w_bad.u8(99); // > 32
    net::Reader r_bad(w_bad.data.data(), w_bad.data.size());
    assert(!read_saved_nations(r_bad, out));
}

static void test_large_city_selection_preference()
{
    // BUG-1581: prefer non-small / higher-slot starting city for guest
    Vec3 host{0, 0, 0};
    std::vector<BaseCandidate> cands = {
        {0x2281u, {80.0f, 0, 0}, 1, 5, true},   // closest, but small coastal city (5 slots)
        {0x2282u, {150.0f, 0, 0}, 1, 11, false}, // large inland city (11 slots)
        {0x2283u, {280.0f, 0, 0}, 1, 10, false}, // large city (10 slots)
        {0x2284u, {200.0f, 0, 0}, 1, 6, false},  // non-small flag, but low slot count (6 vs max 11 -> outside top tier)
    };
    // With prefer_large=true, Near skips small 0x2281 and low-slot 0x2284, picking 0x2282
    assert(select_base_nation(host, cands, rivals::BaseDistance::Near, {}, true) == 0x2282u);
    assert(select_base_nation(host, cands, rivals::BaseDistance::Far, {}, true) == 0x2283u);
    // With prefer_large=false (legacy), Near picks closest 0x2281
    assert(select_base_nation(host, cands, rivals::BaseDistance::Near, {}, false) == 0x2281u);
    // Fallback when only small cities remain free
    assert(select_base_nation(host, cands, rivals::BaseDistance::Near, {0x2282u, 0x2283u, 0x2284u}, true) == 0x2281u);
}

static void test_superweapon_borrow_safety()
{
    // BUG-1581: validate +0x470 (48 bytes) and +0x490 (384 bytes) vector ranges before borrowing
    assert(!sw_vec_valid(0u, 0u, kSwToolsMinBytes));
    assert(!sw_vec_valid(0x1000u, 0x1000u + 20u, kSwToolsMinBytes)); // too short
    assert(sw_vec_valid(0x1000u, 0x1000u + (uint32_t)kSwToolsMinBytes, kSwToolsMinBytes));
    assert(!sw_vec_valid(0x2000u, 0x2000u + 128u, kSwTimersMinBytes));
    assert(sw_vec_valid(0x2000u, 0x2000u + (uint32_t)kSwTimersMinBytes, kSwTimersMinBytes));

    // Guest NPC-origin civ has empty +0x470 and +0x490; orig player civ has valid vectors -> borrow needed
    assert(sw_borrow_needed(false, 0u, 0u, 0x1000u, 0x1000u + (uint32_t)kSwToolsMinBytes, kSwToolsMinBytes));
    assert(sw_borrow_needed(false, 0u, 0u, 0x2000u, 0x2000u + (uint32_t)kSwTimersMinBytes, kSwTimersMinBytes));
    // Same civ (host's own civ): never borrow
    assert(!sw_borrow_needed(true, 0u, 0u, 0x1000u, 0x1000u + (uint32_t)kSwToolsMinBytes, kSwToolsMinBytes));
    // Already populated destination: no borrow needed
    assert(!sw_borrow_needed(false, 0x3000u, 0x3000u + (uint32_t)kSwToolsMinBytes, 0x1000u,
                             0x1000u + (uint32_t)kSwToolsMinBytes, kSwToolsMinBytes));
    // Uninitialized source: cannot borrow
    assert(!sw_borrow_needed(false, 0u, 0u, 0u, 0u, kSwToolsMinBytes));
}

static void test_planner_sync_codec_and_wealth_clamp()
{
    // BUG-1581: atomic planner sync codec roundtrip (buildings + turrets + net_spend)
    PlannerSyncMsg in;
    in.ci = 3;
    in.removed.push_back({0x18EB106u, {10.0f, 20.0f, 30.0f}, 800.0f});
    in.moved.push_back({0x18EA2CCu, {1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}});
    in.added.push_back({0x1A56ABAu, {11.0f, 22.0f, 33.0f}, 800.0f});
    in.turret_delta = 2;
    in.removed_turrets.push_back({50.0f, 60.0f, 70.0f});
    in.net_spend = 1100.0f;

    net::Writer w;
    write_planner_sync(w, in);
    net::Reader r(w.data.data(), w.data.size());
    assert(r.u8() == kCmdPlannerSync);
    PlannerSyncMsg out;
    assert(read_planner_sync(r, out));
    assert(out.ci == 3);
    assert(out.removed.size() == 1 && out.removed[0].noun == 0x18EB106u && std::fabs(out.removed[0].price - 800.0f) < 1e-3f);
    assert(out.moved.size() == 1 && out.moved[0].noun == 0x18EA2CCu && std::fabs(out.moved[0].to.x - 4.0f) < 1e-3f);
    assert(out.added.size() == 1 && out.added[0].noun == 0x1A56ABAu && std::fabs(out.added[0].pos.z - 33.0f) < 1e-3f);
    assert(out.turret_delta == 2);
    assert(out.removed_turrets.size() == 1 && std::fabs(out.removed_turrets[0].x - 50.0f) < 1e-3f);
    assert(std::fabs(out.net_spend - 1100.0f) < 1e-3f);

    // Malformed count rejected
    net::Writer w_bad;
    w_bad.u8(1);
    w_bad.u8(40); // nr > 32
    net::Reader r_bad(w_bad.data.data(), w_bad.data.size());
    assert(!read_planner_sync(r_bad, out));

    // Wealth delta clamping protects host against tampered or desynced planner spend
    assert(std::fabs(clamp_planner_net_spend(1800.0f, 1800.0f, 400.0f) - 1800.0f) < 1e-3f);
    assert(std::fabs(clamp_planner_net_spend(9999.0f, 1800.0f, 400.0f) - 1800.0f) < 1e-3f);
    assert(std::fabs(clamp_planner_net_spend(-5000.0f, 1800.0f, 400.0f) - (-400.0f)) < 1e-3f);
    assert(clamp_planner_net_spend(std::nanf(""), 1000.0f, 1000.0f) == 0.0f);
}

static void test_diplo_gift_capital_softlock_and_clean_notice()
{
    // BUG-1581: vanilla diplomacy Sporebucks gift validation + wire codec
    assert(can_apply_diplo_gift(0, 1, 1000.0f, 2500.0f));
    assert(can_apply_diplo_gift(1, 0, 500.0f, 500.0f));
    assert(!can_apply_diplo_gift(0, 0, 500.0f, 5000.0f));     // self
    assert(!can_apply_diplo_gift(0, 1, 3000.0f, 2500.0f));    // insufficient funds
    assert(!can_apply_diplo_gift(0, 1, 0.0f, 2500.0f));       // non-positive
    assert(!can_apply_diplo_gift(0, 1, -100.0f, 2500.0f));    // negative
    assert(!can_apply_diplo_gift(0, 1, std::nanf(""), 2500.0f));

    net::Writer wd;
    write_diplo_gift(wd, {1, 1500.0f});
    net::Reader rd(wd.data.data(), wd.data.size());
    DiploGiftMsg dm;
    assert(read_diplo_gift(rd, dm) && dm.target_pid == 1 && std::fabs(dm.amount - 1500.0f) < 1e-3f);

    net::Writer wd_bad;
    wd_bad.u8(1);
    wd_bad.f32(-50.0f);
    net::Reader rd_bad(wd_bad.data.data(), wd_bad.data.size());
    assert(!read_diplo_gift(rd_bad, dm));

    // BUG-1581: capital softlock guard
    // Non-capital: always Allow
    assert(capital_siege_gate(false, true, true, 0, 100.0f, 1000.0f, 500.0f) == CapitalDamageAction::Allow);
    // Capital Hall attacked by NPC -> BlockAndRepel
    assert(capital_siege_gate(true, true, true, 5, 800.0f, 1000.0f, 100.0f) == CapitalDamageAction::BlockAndRepel);
    // Capital Hall attacked by rival player: Allow above 25% floor, ClampHp when crossing floor
    assert(capital_siege_gate(true, true, false, 5, 800.0f, 1000.0f, 100.0f) == CapitalDamageAction::Allow);
    assert(capital_siege_gate(true, true, false, 5, 300.0f, 1000.0f, 100.0f) == CapitalDamageAction::ClampHp);
    assert(std::fabs(clamped_hall_damage(300.0f, 1000.0f, 100.0f) - 50.0f) < 1e-3f);
    assert(clamped_hall_damage(250.0f, 1000.0f, 100.0f) == 0.0f);
    // Capital ordinary building attacked by NPC: allowed while >2 alive, BlockAndRepel at <=2
    assert(capital_siege_gate(true, false, true, 4, 200.0f, 200.0f, 50.0f) == CapitalDamageAction::Allow);
    assert(capital_siege_gate(true, false, true, 2, 200.0f, 200.0f, 50.0f) == CapitalDamageAction::BlockAndRepel);

    // BUG-1581: player-facing rival takeover notice never exposes raw hex IDs
    std::string n1 = format_rival_takeover_notice("DERMOGEN", "Brown", kCitySpecMilitary, false);
    std::string n2 = format_rival_takeover_notice("DERMOGEN", "", kCitySpecEconomic, false);
    std::string n3 = format_rival_takeover_notice("DERMOGEN", "Red", -1, false);
    std::string n4 = format_rival_takeover_notice("DERMOGEN", "", -1, false);
    std::string n5 = format_rival_takeover_notice("DERMOGEN", "Blue", kCitySpecMilitary, true);
    for (const auto& s : {n1, n2, n3, n4, n5})
        assert(s.find("0x") == std::string::npos);
    assert(n1.find("Brown Military") != std::string::npos);
    assert(n5.find("reclaimed") != std::string::npos);
}

static void test_guest_starting_specialty_pick()
{
    // BUG-1590: eligibility rule (only while owning starting capital = 1 city)
    assert(can_pick_starting_specialty(true, 1, kCitySpecMilitary));
    assert(can_pick_starting_specialty(true, 1, kCitySpecReligious));
    assert(can_pick_starting_specialty(true, 1, kCitySpecEconomic));
    assert(!can_pick_starting_specialty(false, 1, kCitySpecMilitary)); // unassigned
    assert(!can_pick_starting_specialty(true, 0, kCitySpecMilitary));  // 0 cities
    assert(!can_pick_starting_specialty(true, 2, kCitySpecMilitary));  // already expanded past capital
    assert(!can_pick_starting_specialty(true, 1, 3));                  // invalid specialty
    assert(!can_pick_starting_specialty(true, 1, -1));

    // CLI / arg parsing + RU names
    assert(parse_civ_specialty_arg("mil") == kCitySpecMilitary);
    assert(parse_civ_specialty_arg("Military") == kCitySpecMilitary);
    assert(parse_civ_specialty_arg("0") == kCitySpecMilitary);
    assert(parse_civ_specialty_arg("rel") == kCitySpecReligious);
    assert(parse_civ_specialty_arg("Religious") == kCitySpecReligious);
    assert(parse_civ_specialty_arg("eco") == kCitySpecEconomic);
    assert(parse_civ_specialty_arg("economic") == kCitySpecEconomic);
    assert(parse_civ_specialty_arg("invalid") == -1);
    assert(civ_specialty_name_ru(kCitySpecMilitary)[0] != '\0');
    assert(civ_specialty_name_ru(kCitySpecReligious)[0] != '\0');
    assert(civ_specialty_name_ru(kCitySpecEconomic)[0] != '\0');

    // kSkSpecPick wire codec roundtrip + malformed rejection
    for (uint8_t sp = 0; sp <= 2; sp++) {
        net::Writer w;
        write_spec_pick(w, {sp});
        net::Reader r(w.data.data(), w.data.size());
        SpecPickMsg out;
        assert(read_spec_pick(r, out) && out.specialty == sp);
    }
    net::Writer wbad;
    wbad.u8(3);
    net::Reader rbad(wbad.data.data(), wbad.data.size());
    SpecPickMsg out_bad;
    assert(!read_spec_pick(rbad, out_bad));
}

int main()
{
    test_base_distance_selection();
    test_combat_capture_and_capital_inviolability();
    test_npc_relations_and_influence();
    test_gifts_and_stage_win();
    test_draft_and_wire_codecs();
    test_bidirectional_native_npc_diplomacy_sync();
    test_guest_direct_spend_and_handoff_and_payout();
    test_civ_npc_ui_list();
    test_specialty_conversion_and_superweapon_spend();
    test_rejoin_nation_memory_and_store_codec();
    test_large_city_selection_preference();
    test_superweapon_borrow_safety();
    test_planner_sync_codec_and_wealth_clamp();
    test_diplo_gift_capital_softlock_and_clean_notice();
    test_guest_starting_specialty_pick();
    printf("civ_rivals_tests: all passed\n");
    return 0;
}
