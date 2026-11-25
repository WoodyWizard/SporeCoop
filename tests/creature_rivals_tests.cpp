// Native Linux ASan+UBSan unit tests for Creature Rivals portable logic (WP-R3 BUG-1547, WP-3 BUG-1553).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "creature_rivals_logic.h"
#include "settings_core.h"

using namespace creature_rivals_logic;

static void test_nest_selection_distances()
{
    NestCandidate host{1, 500.f, 0.f, 0.f, 0xAAAA, true, true, false, false, 4};
    std::vector<NestCandidate> cands = {
        {1, 500.f, 0.f, 0.f, 0xAAAA, true, true, false, false, 4},     // avatar herd -> skip
        {2, 505.f, 0.f, 0.f, 0x1000, false, true, false, false, 3},    // too close (<25 m) -> fallback only
        {3, 450.f, 50.f, 0.f, 0x1001, false, true, false, false, 3},   // d ~ 70.7 (Near)
        {4, 300.f, 200.f, 0.f, 0x1002, false, true, false, false, 3},  // d ~ 282.8 (Middle)
        {5, 0.f, 500.f, 0.f, 0x1003, false, true, false, false, 3},    // d ~ 707.1
        {6, -500.f, 0.f, 0.f, 0x1004, false, true, false, false, 3},   // d = 1000 (Far)
        {7, -400.f, 0.f, 0.f, 0x1005, false, true, true, false, 1},    // epic -> skip
        {8, -450.f, 0.f, 0.f, 0x1006, false, true, false, true, 0},    // extinct -> skip
        {9, -460.f, 0.f, 0.f, 0x1007, false, false, false, false, 2},  // no nest -> skip
    };

    assert(select_nest_for_guest(host, cands, {}, rivals::BaseDistance::Near) == 3);
    assert(select_nest_for_guest(host, cands, {}, rivals::BaseDistance::Middle) == 5);
    assert(select_nest_for_guest(host, cands, {}, rivals::BaseDistance::Far) == 6);

    // Second guest avoids already-taken nest
    assert(select_nest_for_guest(host, cands, {3}, rivals::BaseDistance::Near) == 4);
    assert(select_nest_for_guest(host, cands, {6}, rivals::BaseDistance::Far) == 5);

    // Fallback when only close nest remains
    assert(select_nest_for_guest(host, cands, {3, 4, 5, 6}, rivals::BaseDistance::Near) == 2);
    // Exhausted
    assert(select_nest_for_guest(host, cands, {2, 3, 4, 5, 6}, rivals::BaseDistance::Near) == 0);
}

static void test_relationship_enum_and_predator_not_extinct()
{
    // Verified Spore GetRelationship enum values (BUG-1553):
    // 1 = scared (flee), 2 = aggressive/hostile, 3 = neutral, 4 = friendly, 5 = predator/aggressive, 6 = allied
    assert(kRelNone == 0);
    assert(kRelScared == 1);
    assert(kRelHostile == 2);
    assert(kRelNeutral == 3);
    assert(kRelFriendly == 4);
    assert(kRelPredator == 5);
    assert(kRelAllied == 6);

    assert(!is_hostile_rel(kRelNone));
    assert(!is_hostile_rel(kRelScared));
    assert(is_hostile_rel(kRelHostile));
    assert(!is_hostile_rel(kRelNeutral));
    assert(!is_hostile_rel(kRelFriendly));
    assert(is_hostile_rel(kRelPredator));
    assert(!is_hostile_rel(kRelAllied));
    assert(is_allied_rel(kRelAllied));
    assert(!is_allied_rel(kRelFriendly));

    // Stepping toward hostile writes 2 (aggressive), NEVER 1 (scared/flee)
    int32_t out = 0;
    assert(step_toward_hostile(kRelAllied, out) && out == kRelFriendly);
    assert(step_toward_hostile(kRelFriendly, out) && out == kRelNeutral);
    assert(step_toward_hostile(kRelNeutral, out) && out == kRelHostile && out == 2);
    assert(step_toward_hostile(kRelScared, out) && out == kRelHostile && out == 2);
    assert(step_toward_hostile(kRelNone, out) && out == kRelHostile && out == 2);
    assert(!step_toward_hostile(kRelHostile, out));
    assert(!step_toward_hostile(kRelPredator, out));

    // Stepping toward friendly calms both hostile (2) and predator (5) and scared (1) to neutral (3)
    assert(step_toward_friendly(kRelHostile, out) && out == kRelNeutral);
    assert(step_toward_friendly(kRelPredator, out) && out == kRelNeutral);
    assert(step_toward_friendly(kRelScared, out) && out == kRelNeutral);
    assert(step_toward_friendly(kRelNone, out) && out == kRelNeutral);
    assert(step_toward_friendly(kRelNeutral, out) && out == kRelFriendly);
    assert(step_toward_friendly(kRelFriendly, out) && out == kRelAllied);
    assert(!step_toward_friendly(kRelAllied, out));

    // Relation 5 (predator / egg-eat / baby-kill) from host native table MUST NOT mark species extinct!
    PerPlayerSpeciesTables t;
    t.set_home_species(0, 0x1111);
    t.set_home_species(1, 0x2222);

    SpeciesEntry pred{};
    pred.key[0] = 0x5555;
    pred.key[1] = 0x2B978C46;
    pred.key[2] = 0x40626200;
    pred.relation = kRelPredator; // 5 = aggressive predator, NOT extinct
    pred.extinct = false;

    t.sync_from_host_native({pred});
    assert(!t.is_extinct(0x5555));
    assert(t.get_relation(0, 0x5555) == kRelPredator);
    assert(is_hostile_rel(t.get_relation(0, 0x5555)));
    assert(t.get_relation(1, 0x5555) == kRelNone); // did NOT pollute guest with extinction!

    // Predator (rel=5) can still be smoothed over (5 -> 3 -> 4 -> 6) and impressed
    assert(t.apply_smooth_over(0, pred.key));
    assert(t.get_relation(0, 0x5555) == kRelNeutral);

    // Impressing a predator (rel=5) promotes it properly (numerical 5 > 4 must not block promotion!)
    t.by_player[1][0x5555] = pred;
    auto imp1 = t.record_impress(1, pred.key, 3, 2);
    assert(imp1.changed && !imp1.became_allied && t.get_relation(1, 0x5555) == kRelNeutral);
    auto imp2 = t.record_impress(1, pred.key, 3, 2);
    assert(imp2.changed && !imp2.became_allied && t.get_relation(1, 0x5555) == kRelFriendly);
    auto imp3 = t.record_impress(1, pred.key, 3, 2);
    assert(imp3.changed && imp3.became_allied && t.get_relation(1, 0x5555) == kRelAllied);

    // Explicit extinction (extinct=true) propagates across all players
    SpeciesEntry dead_sp{};
    dead_sp.key[0] = 0x6666;
    dead_sp.key[1] = 0x2B978C46;
    dead_sp.key[2] = 0x40626200;
    dead_sp.relation = kRelPredator;
    dead_sp.extinct = true;
    t.sync_from_host_native({dead_sp});
    assert(t.is_extinct(0x6666));
    assert(!t.apply_turn_against(1, dead_sp.key));
    assert(!t.apply_smooth_over(0, dead_sp.key));
}

static void test_per_player_species_relations_and_influence()
{
    PerPlayerSpeciesTables t;
    t.set_home_species(0, 0x1111);
    t.set_home_species(1, 0x2222);

    const uint32_t kook[3] = {0x3333, 0x2B978C46, 0x40626200};
    const uint32_t joob[3] = {0x4444, 0x2B978C46, 0x40626200};
    const uint32_t guest_home[3] = {0x2222, 0x2B978C46, 0x40626200};

    // Player 1 impresses Kook to Ally while Player 0 stays neutral/unset
    auto r1 = t.record_impress(1, kook, 3, 2);
    assert(r1.changed && !r1.became_allied);
    assert(t.get_relation(1, kook[0]) == kRelNeutral);
    assert(t.get_relation(0, kook[0]) == kRelNone);

    auto r2 = t.record_impress(1, kook, 3, 2);
    assert(r2.changed && !r2.became_allied);
    assert(t.get_relation(1, kook[0]) == kRelFriendly);

    auto r3 = t.record_impress(1, kook, 3, 2);
    assert(r3.changed && r3.became_allied);
    assert(t.get_relation(1, kook[0]) == kRelAllied);
    assert(t.get_relation(0, kook[0]) == kRelNone);

    uint16_t a0 = 0, e0 = 0, a1 = 0, e1 = 0;
    t.count_stats(0, a0, e0);
    t.count_stats(1, a1, e1);
    assert(a0 == 0 && e0 == 0);
    assert(a1 == 1 && e1 == 0);

    // Influence TurnAgainst: Player 0 turns Kook against Player 1 (6 -> 4 -> 3 -> 2 -> false)
    assert(t.apply_turn_against(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelFriendly);
    assert(t.apply_turn_against(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelNeutral);
    assert(t.apply_turn_against(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelHostile && t.get_relation(1, kook[0]) == 2);
    assert(!t.apply_turn_against(1, kook)); // already aggressive/hostile

    // Influence SmoothOver: Player 1 smooths over Kook (2 -> 3 -> 4 -> 6 -> false)
    assert(t.apply_smooth_over(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelNeutral);
    assert(t.apply_smooth_over(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelFriendly);
    assert(t.apply_smooth_over(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelAllied);
    assert(!t.apply_smooth_over(1, kook)); // already allied

    // Guest attack (event == 2) lowers ONLY guest's wild relation (6 -> 4 -> 3 -> 2), leaving host untouched
    assert(t.record_attack(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelFriendly);
    assert(t.get_relation(0, kook[0]) == kRelNone);
    assert(t.record_attack(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelNeutral);
    assert(t.record_attack(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelHostile);
    assert(!t.record_attack(1, kook)); // already hostile
    assert(!t.is_extinct(kook[0]));

    // Shared wild extinction: 3 kills across players wipes Joob for EVERYONE (kills write kRelHostile == 2)
    auto k1 = t.record_kill(0, joob, 3);
    assert(k1.changed && !k1.became_extinct && !k1.blocked_home);
    assert(t.get_relation(0, joob[0]) == kRelHostile && t.get_relation(0, joob[0]) == 2);
    assert(t.get_relation(1, joob[0]) == kRelNone); // still independent before extinction!

    auto k2 = t.record_kill(1, joob, 3);
    assert(k2.changed && !k2.became_extinct);
    assert(t.get_relation(1, joob[0]) == kRelHostile && t.get_relation(1, joob[0]) == 2);

    auto k3 = t.record_kill(0, joob, 3);
    assert(k3.changed && k3.became_extinct);
    assert(t.is_extinct(joob[0]));
    assert(t.get_relation(0, joob[0]) == kRelPredator);
    assert(t.get_relation(1, joob[0]) == kRelPredator);
    assert(t.get_relation(2, joob[0]) == kRelPredator);
    assert(t.by_player[0][joob[0]].extinct && t.by_player[1][joob[0]].extinct);
    // Cannot TurnAgainst, SmoothOver, or attack an extinct species
    assert(!t.apply_turn_against(1, joob));
    assert(!t.apply_smooth_over(1, joob));
    assert(!t.record_attack(1, joob));

    // R7 Home Nest Protection: killing guest's home species is NEVER recorded as extinct
    for (int i = 0; i < 10; i++) {
        auto hk = t.record_kill(0, guest_home, 3);
        assert(hk.blocked_home && !hk.became_extinct && !hk.changed);
    }
    assert(!t.is_extinct(guest_home[0]));
    assert(!t.apply_turn_against(1, guest_home));
    assert(!t.apply_smooth_over(0, guest_home));
    assert(block_extinction(true, true));
    assert(!block_extinction(true, false));
    assert(!block_extinction(false, true));
}

static void test_war_pvp_hover_collision_and_settings()
{
    // Wild NPC always damageable
    assert(allow_creature_damage(true, false, 0, 255, TargetKind::WildNpc, false));

    // Creature Rivals: Neutral/Allied cannot damage rival avatar or rival family (even if friendly_fire is on)
    assert(!allow_creature_damage(true, true, 0, 1, TargetKind::PlayerAvatar, false));
    assert(!allow_creature_damage(true, true, 0, 1, TargetKind::PlayerFamily, false));
    assert(!allow_creature_damage(true, true, 1, 0, TargetKind::PlayerAvatar, false));
    assert(!allow_creature_damage(true, true, 1, 0, TargetKind::PlayerFamily, false));

    // Creature Rivals: At War CAN damage rival avatar and rival family (even if friendly_fire is off)
    assert(allow_creature_damage(true, false, 0, 1, TargetKind::PlayerAvatar, true));
    assert(allow_creature_damage(true, false, 0, 1, TargetKind::PlayerFamily, true));
    assert(allow_creature_damage(true, false, 1, 0, TargetKind::PlayerAvatar, true));
    assert(allow_creature_damage(true, false, 1, 0, TargetKind::PlayerFamily, true));

    // 3-4p: guest 1 at War with guest 2 can hit guest 2's family; cannot hit host 0's family when Neutral
    assert(allow_creature_damage(true, false, 1, 2, TargetKind::PlayerFamily, true));
    assert(!allow_creature_damage(true, false, 1, 0, TargetKind::PlayerFamily, false));

    // Never hurt own avatar or own family
    assert(!allow_creature_damage(true, true, 0, 0, TargetKind::PlayerFamily, true));
    assert(!allow_creature_damage(true, true, 1, 1, TargetKind::PlayerFamily, true));

    // Together mode: gated strictly by friendly_fire_setting
    assert(!allow_creature_damage(false, false, 0, 1, TargetKind::PlayerAvatar, true));
    assert(allow_creature_damage(false, true, 0, 1, TargetKind::PlayerAvatar, false));
    assert(!allow_creature_damage(false, true, 0, 1, TargetKind::PlayerFamily, true));

    // Puppet hover/collision pass-through policy (BUG-1553):
    // In Creature Rivals: War allows hover & collision (false); Neutral/Allied passes through (true) regardless of FF
    assert(!should_pass_through_puppet(true, false, true));
    assert(!should_pass_through_puppet(true, true, true));
    assert(should_pass_through_puppet(true, false, false));
    assert(should_pass_through_puppet(true, true, false));
    // Outside Creature Rivals: follows friendly_fire setting
    assert(should_pass_through_puppet(false, false, false));
    assert(should_pass_through_puppet(false, false, true));
    assert(!should_pass_through_puppet(false, true, false));

    // Co-op pack bonuses must be disabled in Rivals (settings_core.h)
    assert(settings::kDefs[settings::kPackRescue].off_in_rivals);
    assert(settings::kDefs[settings::kRevive].off_in_rivals);
    assert(settings::kDefs[settings::kPackHeal].off_in_rivals);
    assert(settings::kDefs[settings::kJointHunt].off_in_rivals);
}

static void test_guest_pack_recruitment_and_impressed_isolation()
{
    // Guest 1 (home species 0x2222) vs Host 0 (home species 0x1111) vs Guest 2 (home species 0x2222 or 0x3333)
    // Own family member: CAN recruit
    assert(can_guest_recruit_in_rivals(1, 0x2222, 1, 0x2222, false));
    // Host's family member: CANNOT recruit (even if species matches or guest_allied were true!)
    assert(!can_guest_recruit_in_rivals(1, 0x1111, 0, 0x2222, true));
    assert(!can_guest_recruit_in_rivals(1, 0x2222, 0, 0x2222, true));
    // Another guest's family member (player 2): CANNOT recruit
    assert(!can_guest_recruit_in_rivals(1, 0x3333, 2, 0x2222, true));
    // Wild species allied with guest 1: CAN recruit
    assert(can_guest_recruit_in_rivals(1, 0x7777, 255, 0x2222, true));
    // Wild species allied ONLY with host (not guest 1): CANNOT recruit
    assert(!can_guest_recruit_in_rivals(1, 0x7777, 255, 0x2222, false));
    // Wild creature of guest's own species: CAN recruit
    assert(can_guest_recruit_in_rivals(1, 0x2222, 255, 0x2222, false));

    // Per-player impressed isolation
    PerPlayerImpressed imp;
    assert(!imp.has(1, 42, 1001));
    assert(!imp.has(2, 42, 1001));

    // Player 1 impressing NPC #42 (serial 1001) does NOT block Player 2 or Player 0
    imp.mark(1, 42, 1001);
    assert(imp.has(1, 42, 1001));
    assert(!imp.has(2, 42, 1001));
    assert(!imp.has(0, 42, 1001));

    // Player 2 can also impress NPC #42 independently
    imp.mark(2, 42, 1001);
    assert(imp.has(1, 42, 1001));
    assert(imp.has(2, 42, 1001));

    // NPC ID reuse with new serial automatically invalidates stale impressed bits
    assert(!imp.has(1, 42, 2002));
    assert(!imp.has(2, 42, 2002));
    imp.mark(1, 42, 2002);
    assert(imp.has(1, 42, 2002));
    assert(!imp.has(2, 42, 2002));

    // Leaving player's bits are cleared without touching remaining players
    imp.mark(2, 42, 2002);
    imp.forget_player(1);
    assert(!imp.has(1, 42, 2002));
    assert(imp.has(2, 42, 2002));
}

static void test_npc_ui_list_generation()
{
    PerPlayerSpeciesTables t;
    t.set_home_species(0, 0x1111);
    t.set_home_species(1, 0x2222);

    const uint32_t sp_ally[3] = {0xAAA1, 0x2B978C46, 0x40626200};
    const uint32_t sp_hostile[3] = {0xBBB2, 0x2B978C46, 0x40626200};
    const uint32_t sp_pred[3] = {0xCCC3, 0x2B978C46, 0x40626200};
    const uint32_t sp_ext[3] = {0xDDD4, 0x2B978C46, 0x40626200};
    const uint32_t sp_neutral = 0xEEE5;

    // Player 1 allies sp_ally, angers sp_hostile (rel=2)
    t.record_impress(1, sp_ally, 3, 2);
    t.record_impress(1, sp_ally, 3, 2);
    t.record_impress(1, sp_ally, 3, 2);
    t.record_kill(1, sp_hostile, 3);

    // Host sees sp_pred as live predator (rel=5, extinct=false)
    SpeciesEntry pred{};
    pred.key[0] = sp_pred[0];
    pred.key[1] = sp_pred[1];
    pred.key[2] = sp_pred[2];
    pred.relation = kRelPredator;
    pred.extinct = false;
    t.sync_from_host_native({pred});

    // sp_ext is genuinely extinct
    t.mark_extinct(sp_ext);

    std::vector<uint32_t> discovered = {0x1111, 0x2222, sp_ally[0], sp_hostile[0], sp_pred[0], sp_ext[0], sp_neutral};
    std::map<uint32_t, std::string> names = {{sp_ally[0], "Kook"}};

    // Viewer = Player 1
    auto rows1 = build_wild_npc_ui_list(t, 1, discovered, &names);
    // Home species (0x1111, 0x2222) and extinct (0xDDD4) must be excluded -> 4 entries left
    assert(rows1.size() == 4);
    assert(rows1[0].npc_id == 0xAAA1 && rows1[0].label == "Kook");
    assert(rows1[0].allied && !rows1[0].hostile && rows1[0].can_turn && rows1[0].can_protect && !rows1[0].can_smooth);

    assert(rows1[1].npc_id == 0xBBB2 && rows1[1].label == "Wild 0x0000BBB2");
    assert(!rows1[1].allied && rows1[1].hostile && rows1[1].can_turn && !rows1[1].can_protect && rows1[1].can_smooth);

    assert(rows1[2].npc_id == 0xCCC3 && rows1[2].label == "Wild 0x0000CCC3");
    assert(!rows1[2].allied && !rows1[2].hostile && rows1[2].can_turn && !rows1[2].can_protect && !rows1[2].can_smooth);

    assert(rows1[3].npc_id == 0xEEE5 && rows1[3].label == "Wild 0x0000EEE5");
    assert(!rows1[3].allied && !rows1[3].hostile && rows1[3].can_turn && !rows1[3].can_protect && !rows1[3].can_smooth);

    // Viewer = Host 0: sees sp_pred (rel=5, NOT extinct!) as hostile & smoothable, sp_ally as NOT allied
    auto rows0 = build_wild_npc_ui_list(t, 0, discovered, &names);
    assert(rows0.size() == 4);
    assert(rows0[0].npc_id == 0xAAA1 && !rows0[0].allied && !rows0[0].can_protect && rows0[0].can_turn);
    assert(rows0[2].npc_id == 0xCCC3 && rows0[2].hostile && rows0[2].can_smooth && rows0[2].can_turn);
}

static void test_auto_tribe_transition_and_draft_items()
{
    assert(!reached_tribe_threshold(0));
    assert(!reached_tribe_threshold(3));
    assert(reached_tribe_threshold(4));
    assert(reached_tribe_threshold(5));

    // Auto transition blocked during catchup, during active draft, before settle window, or while cinematic plays
    assert(!should_auto_trigger_tribe_transition(true, true, false, true, false, 1, false, false, false, 10.0, 20.0));
    assert(!should_auto_trigger_tribe_transition(true, true, true, false, true, 1, true, false, false, 10.0, 20.0));
    assert(!should_auto_trigger_tribe_transition(true, true, true, false, false, 1, true, false, false, 10.0, 11.5));
    assert(!should_auto_trigger_tribe_transition(true, true, true, false, false, 1, true, false, true, 10.0, 15.0));
    assert(!should_auto_trigger_tribe_transition(false, true, true, false, false, 1, true, false, false, 10.0, 15.0));
    // Fires once catch-up + draft finish and settle window elapses (for both guest win=1 and host win=0)
    assert(should_auto_trigger_tribe_transition(true, true, true, false, false, 1, true, false, false, 10.0, 12.1));
    assert(should_auto_trigger_tribe_transition(true, true, true, false, false, 0, true, false, false, 10.0, 12.1));
    // Never fires a second time
    assert(!should_auto_trigger_tribe_transition(true, true, true, false, false, 1, true, true, false, 10.0, 15.0));

    std::vector<PartKey> winner = {{0x100, 0x1}, {0x200, 0x2}, {0x300, 0x3}, {0x100, 0x1}};
    std::set<PartKey> baseline = {{0x100, 0x1}};
    std::vector<rivals::Draft::Item> items;
    std::vector<PartKey> keys;
    build_creature_draft_items(winner, baseline, items, keys);

    assert(items.size() == 3 && keys.size() == 3);
    // Unique winner-only parts come first
    assert(keys[0].first == 0x200 && keys[1].first == 0x300 && keys[2].first == 0x100);
    assert(items[0].id == 1 && items[1].id == 2 && items[2].id == 3);
    assert(items[0].kind == 1);
}

static void test_wire_codecs()
{
    // SK_ASSIGN
    {
        std::vector<AssignEntry> in = {
            {0, 100.5f, 200.f, 300.f, {0xAA, 0xBB, 0xCC}, 3},
            {1, -100.f, 50.f, 400.f, {0x11, 0x22, 0x33}, 1},
        };
        net::Writer w;
        write_assign(w, in);
        net::Reader r(w.data.data(), w.data.size());
        std::vector<AssignEntry> out;
        assert(read_assign(r, out));
        assert(out.size() == 2);
        assert(out[1].player_id == 1 && std::fabs(out[1].nest_x - (-100.f)) < 1e-4f && out[1].species_key[0] == 0x11);

        // Truncated rejected
        net::Reader bad(w.data.data(), 4);
        assert(!read_assign(bad, out));
    }

    // SK_NPC_RELS (with explicit extinct trailer, distinguishing live rel=5 predator from extinct)
    {
        std::vector<SpeciesEntry> in = {
            {{0x1234, 0x2B978C46, 0x40626200}, kRelAllied, 0, 3, 0, false},
            {{0x5678, 0x2B978C46, 0x40626200}, kRelPredator, 1, 0, 1, false}, // live predator
            {{0x9ABC, 0x2B978C46, 0x40626200}, kRelPredator, 0, 0, 3, true},  // truly extinct
        };
        net::Writer w;
        write_npc_rels(w, in);
        net::Reader r(w.data.data(), w.data.size());
        std::vector<SpeciesEntry> out;
        assert(read_npc_rels(r, out));
        assert(out == in);
        assert(!out[1].extinct && out[2].extinct);

        // Legacy payload without trailing extinct flags decodes cleanly with extinct=false
        net::Reader legacy(w.data.data(), w.data.size() - in.size());
        std::vector<SpeciesEntry> out_legacy;
        assert(read_npc_rels(legacy, out_legacy));
        assert(out_legacy.size() == 3 && !out_legacy[1].extinct && !out_legacy[2].extinct);
    }

    // SK_PROGRESS
    {
        ProgressMsg in{3, 145.5f, 220.0f, 42, {{0x10, 0x20}, {0x30, 0x40}}};
        net::Writer w;
        write_progress(w, in);
        net::Reader r(w.data.data(), w.data.size());
        ProgressMsg out;
        assert(read_progress(r, out));
        assert(out.brain_level == 3 && std::fabs(out.dna - 145.5f) < 1e-4f && out.parts_total == 42);
        assert(out.parts.size() == 2 && out.parts[1].first == 0x30 && out.parts[1].second == 0x40);
    }

    // SK_DRAFT_APPLY
    {
        DraftApplyMsg in{kDraftOpUnlockPart, 0xABCD, 0xEF01, 0};
        net::Writer w;
        write_draft_apply(w, in);
        net::Reader r(w.data.data(), w.data.size());
        DraftApplyMsg out;
        assert(read_draft_apply(r, out));
        assert(out.op == kDraftOpUnlockPart && out.lo == 0xABCD && out.hi == 0xEF01);
    }

    // SK_PVP_HIT
    {
        PvpHitMsg in{1, 0, 12.5f, 2};
        net::Writer w;
        write_pvp_hit(w, in);
        net::Reader r(w.data.data(), w.data.size());
        PvpHitMsg out;
        assert(read_pvp_hit(r, out));
        assert(out.victim_player == 1 && out.attacker_player == 0 && std::fabs(out.damage - 12.5f) < 1e-4f);

        // Out-of-bounds damage rejected
        PvpHitMsg bad_in{1, 0, 5000.f, 2};
        net::Writer w2;
        write_pvp_hit(w2, bad_in);
        net::Reader r2(w2.data.data(), w2.data.size());
        assert(!read_pvp_hit(r2, out));
    }
}

static void test_rivals_drama_isolation_policy()
{
    // Fix disabled or Together mode -> always Allow
    {
        DramaQuery q{};
        q.fix_enabled = false;
        q.creature_rivals = true;
        q.is_host = true;
        q.kind = 8;
        q.is_summon_for_guest = true;
        assert(evaluate_rivals_drama(q) == DramaAction::Allow);

        q.fix_enabled = true;
        q.creature_rivals = false;
        q.tribe_rivals = false;
        assert(evaluate_rivals_drama(q) == DramaAction::Allow);
    }

    // Creature Rivals: rival guest Summon Flock (K_FLOCK -> summon_for) blocked on host
    {
        DramaQuery q{};
        q.fix_enabled = true;
        q.creature_rivals = true;
        q.is_host = true;
        q.kind = 8;
        q.is_summon_for_guest = true;
        assert(evaluate_rivals_drama(q) == DramaAction::BlockEvent);

        // Host's OWN Summon Flock is allowed in Creature Rivals
        q.is_summon_for_guest = false;
        assert(evaluate_rivals_drama(q) == DramaAction::Allow);

        // Guest local Summon Flock blocked in Creature Rivals
        q.is_host = false;
        q.is_guest = true;
        assert(evaluate_rivals_drama(q) == DramaAction::BlockEvent);
    }

    // Creature Rivals: host meteor (3) and rain (5) stay host-local (BlockRelay), never broadcast to rival guests
    for (unsigned k : {3u, 5u}) {
        DramaQuery host_tx{};
        host_tx.fix_enabled = true;
        host_tx.creature_rivals = true;
        host_tx.is_host = true;
        host_tx.kind = k;
        assert(evaluate_rivals_drama(host_tx) == DramaAction::BlockRelay);

        DramaQuery guest_rx{};
        guest_rx.fix_enabled = true;
        guest_rx.creature_rivals = true;
        guest_rx.is_guest = true;
        guest_rx.kind = k;
        guest_rx.is_relay_rx = true;
        assert(evaluate_rivals_drama(guest_rx) == DramaAction::BlockRelay);

        // Together mode allows relay
        host_tx.creature_rivals = false;
        guest_rx.creature_rivals = false;
        assert(evaluate_rivals_drama(host_tx) == DramaAction::Allow);
        assert(evaluate_rivals_drama(guest_rx) == DramaAction::Allow);
    }

    // Creature Rivals: sea monster (6) remains local Allow on both host and guest
    {
        DramaQuery sea{};
        sea.fix_enabled = true;
        sea.creature_rivals = true;
        sea.is_guest = true;
        sea.kind = 6;
        assert(evaluate_rivals_drama(sea) == DramaAction::Allow);
    }

    // Tribe Rivals: creature_attack (7) blocked on host and guest so converted rival villages are not unfairly raided
    {
        DramaQuery raid{};
        raid.fix_enabled = true;
        raid.tribe_rivals = true;
        raid.is_host = true;
        raid.kind = 7;
        assert(evaluate_rivals_drama(raid) == DramaAction::BlockEvent);

        raid.is_host = false;
        raid.is_guest = true;
        assert(evaluate_rivals_drama(raid) == DramaAction::BlockEvent);

        // Together Tribe allows host creature_attack
        raid.tribe_rivals = false;
        raid.is_host = true;
        raid.is_guest = false;
        assert(evaluate_rivals_drama(raid) == DramaAction::Allow);
    }
}

static void test_rivals_editor_no_reload_policy()
{
    EditorShareQuery base{};
    base.fix_enabled = true;
    base.rivals_active = true;
    base.is_manual_share = false;
    base.same_stage_editor = true;
    base.is_stage_transition = false;
    base.world_changed = false;
    base.peer_already_in_world = true;
    base.peer_needs_recovery = false;

    // Same-stage host Creature Editor return in Creature Rivals for active peer -> SKIP reload share
    assert(should_skip_editor_world_share(base));

    // Fix OFF -> do NOT skip
    {
        auto q = base;
        q.fix_enabled = false;
        assert(!should_skip_editor_world_share(q));
    }
    // Together mode -> do NOT skip
    {
        auto q = base;
        q.rivals_active = false;
        assert(!should_skip_editor_world_share(q));
    }
    // Manual share (coop.share / UI share_all, tag == 0) -> NEVER skip
    {
        auto q = base;
        q.is_manual_share = true;
        assert(!should_skip_editor_world_share(q));
    }
    // Not a same-stage editor return -> NEVER skip
    {
        auto q = base;
        q.same_stage_editor = false;
        assert(!should_skip_editor_world_share(q));
    }
    // Stage transition (Cell->Creature, Creature->Tribe) -> NEVER skip
    {
        auto q = base;
        q.is_stage_transition = true;
        assert(!should_skip_editor_world_share(q));
    }
    // Planet / save changed -> NEVER skip
    {
        auto q = base;
        q.world_changed = true;
        assert(!should_skip_editor_world_share(q));
    }
    // Fresh joiner (peer not yet shared in this stage/world) -> NEVER skip
    {
        auto q = base;
        q.peer_already_in_world = false;
        assert(!should_skip_editor_world_share(q));
    }
    // Peer left to menu / another stage and needs recovery -> NEVER skip
    {
        auto q = base;
        q.peer_needs_recovery = true;
        assert(!should_skip_editor_world_share(q));
    }
}

int main()
{
    test_nest_selection_distances();
    test_relationship_enum_and_predator_not_extinct();
    test_per_player_species_relations_and_influence();
    test_war_pvp_hover_collision_and_settings();
    test_guest_pack_recruitment_and_impressed_isolation();
    test_npc_ui_list_generation();
    test_auto_tribe_transition_and_draft_items();
    test_wire_codecs();
    test_rivals_drama_isolation_policy();
    test_rivals_editor_no_reload_policy();
    std::printf("creature_rivals_tests: all passed\n");
    return 0;
}
