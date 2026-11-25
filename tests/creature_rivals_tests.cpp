// Native Linux ASan+UBSan unit tests for Creature Rivals portable logic (WP-R3, BUG-1547).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "creature_rivals_logic.h"

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

    // Influence TurnAgainst: Player 0 turns Kook against Player 1 (6 -> 4 -> 3 -> 2 -> 1 -> false)
    assert(t.apply_turn_against(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelFriendly);
    assert(t.apply_turn_against(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelNeutral);
    assert(t.apply_turn_against(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelUnfriendly);
    assert(t.apply_turn_against(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelHostile);
    assert(!t.apply_turn_against(1, kook)); // already hostile

    // Influence SmoothOver: Player 1 smooths over Kook (1 -> 2 -> 3 -> 4 -> 6 -> false)
    assert(t.apply_smooth_over(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelUnfriendly);
    assert(t.apply_smooth_over(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelNeutral);
    assert(t.apply_smooth_over(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelFriendly);
    assert(t.apply_smooth_over(1, kook));
    assert(t.get_relation(1, kook[0]) == kRelAllied);
    assert(!t.apply_smooth_over(1, kook)); // already allied

    // Shared wild extinction: 3 kills across players wipes Joob for EVERYONE
    auto k1 = t.record_kill(0, joob, 3);
    assert(k1.changed && !k1.became_extinct && !k1.blocked_home);
    assert(t.get_relation(0, joob[0]) == kRelHostile);
    assert(t.get_relation(1, joob[0]) == kRelNone); // still independent before extinction!

    auto k2 = t.record_kill(1, joob, 3);
    assert(k2.changed && !k2.became_extinct);
    assert(t.get_relation(1, joob[0]) == kRelHostile);

    auto k3 = t.record_kill(0, joob, 3);
    assert(k3.changed && k3.became_extinct);
    assert(t.is_extinct(joob[0]));
    assert(t.get_relation(0, joob[0]) == kRelExtinct);
    assert(t.get_relation(1, joob[0]) == kRelExtinct);
    assert(t.get_relation(2, joob[0]) == kRelExtinct);
    // Cannot TurnAgainst or SmoothOver an extinct species
    assert(!t.apply_turn_against(1, joob));
    assert(!t.apply_smooth_over(1, joob));

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

static void test_war_pvp_and_family_damage()
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

    // Never hurt own avatar or own family
    assert(!allow_creature_damage(true, true, 0, 0, TargetKind::PlayerFamily, true));
    assert(!allow_creature_damage(true, true, 1, 1, TargetKind::PlayerFamily, true));

    // Together mode: gated strictly by friendly_fire_setting
    assert(!allow_creature_damage(false, false, 0, 1, TargetKind::PlayerAvatar, true));
    assert(allow_creature_damage(false, true, 0, 1, TargetKind::PlayerAvatar, false));
    assert(!allow_creature_damage(false, true, 0, 1, TargetKind::PlayerFamily, true));
}

static void test_brain_threshold_and_draft_items()
{
    assert(!reached_tribe_threshold(0));
    assert(!reached_tribe_threshold(3));
    assert(reached_tribe_threshold(4));
    assert(reached_tribe_threshold(5));

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

    // SK_NPC_RELS
    {
        std::vector<SpeciesEntry> in = {
            {{0x1234, 0x2B978C46, 0x40626200}, kRelAllied, 0, 3, 0},
            {{0x5678, 0x2B978C46, 0x40626200}, kRelHostile, 1, 0, 2},
        };
        net::Writer w;
        write_npc_rels(w, in);
        net::Reader r(w.data.data(), w.data.size());
        std::vector<SpeciesEntry> out;
        assert(read_npc_rels(r, out));
        assert(out == in);
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

int main()
{
    test_nest_selection_distances();
    test_per_player_species_relations_and_influence();
    test_war_pvp_and_family_damage();
    test_brain_threshold_and_draft_items();
    test_wire_codecs();
    std::printf("creature_rivals_tests: all passed\n");
    return 0;
}
