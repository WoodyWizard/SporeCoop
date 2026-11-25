// Portable unit tests for Cell stage Rivals mode logic and wire codecs (WP-R2 of 0.8.0, BUG-1546).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

#include "cell_rivals_logic.h"
#include "wire/rivals_faction.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg)                                           \
    do {                                                           \
        if (cond) {                                                \
            g_pass++;                                              \
        } else {                                                   \
            g_fail++;                                              \
            printf("FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
        }                                                          \
    } while (0)

static void test_progress_and_parts()
{
    printf("cell_rivals: progress bar, parts mask, food rollback\n");
    using namespace cell_rivals_logic;
    CHECK(bar_fraction(-10) == 0.0f, "negative food clamps to 0.0");
    CHECK(bar_fraction(0) == 0.0f, "0 food is 0.0");
    CHECK(std::fabs(bar_fraction(250) - 0.25f) < 1e-6f, "250 food is 0.25");
    CHECK(bar_fraction(1000) == 1.0f, "1000 food is 1.0");
    CHECK(bar_fraction(1500) == 1.0f, "over 1000 clamps to 1.0");
    CHECK(bar_percent(0) == 0 && bar_percent(500) == 50 && bar_percent(1000) == 100, "bar_percent exact");
    CHECK(!bar_is_full(999) && bar_is_full(1000) && bar_is_full(1200), "bar_is_full threshold at 1000");

    int states[kCellParts] = {1, 0, 2, 0, 1, 0, 2, 0, 0, 0, 0, 0, 1};
    uint16_t mask = mask_from_states(states);
    CHECK(mask == ((1u << 0) | (1u << 2) | (1u << 4) | (1u << 6) | (1u << 12)), "mask_from_states extracts non-zero slots");
    CHECK(popcount16(mask) == 5, "popcount16 matches unlocked count");
    CHECK(mask_from_states(nullptr) == 0, "null states returns 0");

    CHECK(clamp_food_rollback(50, -12) == -12, "rollback within current food");
    CHECK(clamp_food_rollback(5, -12) == -5, "rollback clamps at 0 food");
    CHECK(clamp_food_rollback(0, -12) == 0, "rollback at 0 food is 0");
    CHECK(clamp_food_rollback(50, 10) == 0, "positive delta ignored by rollback");

    // BUG-1538 / BUG-1552: spent-DNA clamp after late food rollback
    CHECK(clamp_spent_after_rollback(80, 60) == 60, "spent capped at DNAForFood after rollback");
    CHECK(clamp_spent_after_rollback(40, 60) == 40, "spent below DNAForFood unchanged");
    CHECK(clamp_spent_after_rollback(-15, 0) == -15, "negative spent (sold starting parts) preserved");
    CHECK(clamp_spent_after_rollback(50, -1) == 50, "invalid negative DNAForFood leaves spent untouched");
}

static void test_policy_gates()
{
    printf("cell_rivals: policy gates (PvP war gate, KO, regen, parts, food, influence)\n");
    using namespace cell_rivals_logic;
    // Cell Rivals ON: Neutral/Allied blocked even if friendly_fire is ON; War allowed even if friendly_fire is OFF
    CHECK(!can_pvp_damage(true, false, false), "rivals neutral ff off -> no dmg");
    CHECK(!can_pvp_damage(true, false, true), "rivals neutral ff on -> no dmg (war-gated)");
    CHECK(can_pvp_damage(true, true, false), "rivals at war ff off -> dmg allowed");
    CHECK(can_pvp_damage(true, true, true), "rivals at war ff on -> dmg allowed");
    // Cell Rivals OFF (Together): follows friendly_fire
    CHECK(!can_pvp_damage(false, true, false), "together ff off -> no dmg");
    CHECK(can_pvp_damage(false, false, true), "together ff on -> dmg allowed");

    // Symbiosis KO, proximity regen, auto-shared parts, shared food, level clip, influence
    CHECK(!ko_allowed(true, true) && ko_allowed(false, true) && !ko_allowed(false, false), "KO is Together-only");
    CHECK(!regen_allowed(true, true) && regen_allowed(false, true), "regen is Together-only");
    CHECK(!parts_auto_share_allowed(true, true) && parts_auto_share_allowed(false, true), "parts auto-share paused in Cell Rivals");
    CHECK(!pool_food_shared(true, false) && !pool_food_shared(false, true) && pool_food_shared(false, false), "pool food unshared in Rivals");
    CHECK(!level_clip_allowed(true, true) && level_clip_allowed(false, true), "level_together clip disabled in Cell Rivals");
    CHECK(!influence_allowed_in_cell(true) && !influence_allowed_in_cell(false), "no influence in Cell stage");
}

static void test_war_pvp_routing()
{
    printf("cell_rivals: host-authoritative war PvP routing\n");
    using namespace cell_rivals_logic;
    CHECK(route_war_pvp_hit(false, true, 1, 1, 0, true) == WarHitAction::Refuse, "inactive rivals refuses");
    CHECK(route_war_pvp_hit(true, false, 1, 1, 0, true) == WarHitAction::Refuse, "not at war refuses");
    CHECK(route_war_pvp_hit(true, true, 1, 2, 0, true) == WarHitAction::Refuse, "spoofed attacker id refuses");
    CHECK(route_war_pvp_hit(true, true, 1, 1, 1, true) == WarHitAction::Refuse, "self-hit refuses");
    CHECK(route_war_pvp_hit(true, true, 1, 1, 0, true) == WarHitAction::HurtHost, "guest->host at war hurts host");
    CHECK(route_war_pvp_hit(true, true, 1, 1, 2, true) == WarHitAction::ForwardGuest, "guest->guest at war forwards");
    CHECK(route_war_pvp_hit(true, true, 1, 1, 2, false) == WarHitAction::Refuse, "absent guest victim refuses");
    CHECK(route_war_pvp_hit(true, true, 1, 1, 5, true) == WarHitAction::Refuse, "out-of-range player refuses");
}

static void test_meat_drop_and_scale()
{
    printf("cell_rivals: meat drop ring and world scale conversion\n");
    using namespace cell_rivals_logic;
    MeatSpot spots[kPvpMeatChunks];
    meat_drop_spots(kPvpMeatChunks, 2.0f, spots);
    for (int i = 0; i < kPvpMeatChunks; i++) {
        float r = std::hypot(spots[i].dx, spots[i].dy);
        CHECK(std::isfinite(r) && std::fabs(r - 2.0f) < 1e-4f, "meat chunk sits on drop radius");
        CHECK(std::isfinite(spots[i].vx) && std::isfinite(spots[i].vy), "meat outward velocity is finite");
    }
    // Non-finite radius/speed safely clamped to defaults
    meat_drop_spots(kPvpMeatChunks, std::numeric_limits<float>::quiet_NaN(), spots, -5.0f);
    CHECK(std::isfinite(spots[0].dx) && std::fabs(spots[0].dx - kMeatDropRadius) < 1e-4f, "NaN radius falls back to default");
    CHECK(meat_drop_level(0) == 0 && meat_drop_level(1) == 0 && meat_drop_level(3) == 2 && meat_drop_level(20) == 15,
          "meat_drop_level clamped to [0, 15]");
    CHECK(std::fabs(convert_scale_coord(10.0f, 2.0f, 4.0f) - 5.0f) < 1e-5f, "scale conversion scales coordinates");
    CHECK(convert_scale_coord(std::numeric_limits<float>::quiet_NaN(), 2.0f, 4.0f) == 0.0f, "NaN coordinate sanitizes to 0");
    CHECK(convert_scale_coord(10.0f, 0.0f, 4.0f) == 10.0f, "zero source scale leaves coord unchanged");
}

static void test_cell_part_draft_and_race_flow()
{
    printf("cell_rivals: Cell part draft items + full race -> catchup -> draft -> payout\n");
    using namespace cell_rivals_logic;
    // Winner has parts 0 (starting), 4 (Jet), 6 (Spike), 8 (Electric); guest has part 0 only
    uint16_t winner_mask = (uint16_t)((1u << 0) | (1u << 4) | (1u << 6) | (1u << 8));
    uint16_t common_mask = (uint16_t)(1u << 0);
    auto items = build_draft_items(winner_mask, common_mask);
    CHECK(items.size() == 3, "draft offers the 3 winner parts the picker lacks");
    CHECK(item_id_to_part_index(items[0].id) == 4 && items[0].kind == kPartMovement, "part 4 offered first");
    CHECK(item_id_to_part_index(items[1].id) == 6 && items[1].kind == kPartWeapon, "part 6 offered second");
    CHECK(item_id_to_part_index(items[2].id) == 8 && items[2].kind == kPartWeapon, "part 8 offered third");
    CHECK(item_id_to_part_index(0) == -1 && item_id_to_part_index(14) == -1, "invalid item IDs return -1");

    // End-to-end integration with rivals::State
    rivals::State st;
    st.config.set_stage_mode(rivals::Stage::Cell, rivals::StageMode::Rivals);
    st.config.catchup_secs = 60;
    st.config.draft_picks = 3;
    st.race.reset(rivals::Stage::Cell);

    rivals::PlayerProgress p0{}, p1{};
    p0.player_id = 0; p0.present = true; p0.primary = bar_fraction(1000); p0.secondary = 95.0f; p0.stat_a = 4;
    p1.player_id = 1; p1.present = true; p1.primary = bar_fraction(320); p1.secondary = 30.0f; p1.stat_a = 1;
    st.race.set_progress(0, p0);
    st.race.set_progress(1, p1);

    uint8_t rank = st.race.report_complete(0, 2, 60.0, 100.0);
    CHECK(rank == 1 && st.race.winner == 0 && st.race.catchup_active, "first to 1000 wins and starts catch-up");
    CHECK(!st.race.tick(130.0) && st.race.catchup_active, "catch-up still running at 30 s");
    CHECK(st.race.tick(161.0) && st.race.stage_finished, "catch-up expires at 60 s");

    uint8_t ranks[rivals::kMaxPlayers] = {1, 0, 0, 0};
    bool present[rivals::kMaxPlayers] = {true, true, false, false};
    CHECK(st.draft.start(rivals::Stage::Cell, 0, items, st.config.draft_picks, ranks, present, 161.0),
          "Cell part draft starts with winner parts");

    int32_t pay1 = 0, pay2 = 0;
    CHECK(st.draft.pick(1, part_index_to_item_id(6), &pay1).empty() && pay1 == 100, "1st pick unlocks part 6, pays 100");
    CHECK(st.draft.pick(1, part_index_to_item_id(8), &pay2).empty() && pay2 == 150, "2nd pick unlocks part 8, pays 150");
    CHECK(st.draft.take_pending_payout(0) == 250, "winner claims escalating next-stage DNA payout (250)");
    CHECK(st.draft.take_pending_payout(0) == 0, "payout cleared after claim");
}

static void test_wire_codecs()
{
    printf("cell_rivals: MSG_RIVALS_FACTION Stage::Cell wire codecs & fuzz safety\n");
    using namespace cell_rivals_logic;
    // Progress
    {
        net::Writer w;
        wire::rivals_faction::write_header(w, rivals::Stage::Cell, wire::rivals_faction::SK_PROGRESS);
        write_progress(w, {740, 85, 0x0154});
        net::Reader r(w.data.data(), w.data.size());
        rivals::Stage st = rivals::Stage::None;
        uint8_t sk = 0;
        ProgressMsg out{};
        CHECK(wire::rivals_faction::read_header(r, st, sk) && st == rivals::Stage::Cell &&
                  sk == wire::rivals_faction::SK_PROGRESS,
              "progress header round-trip");
        CHECK(read_progress(r, out) && out.food == 740 && out.dna == 85 && out.parts_mask == 0x0154,
              "progress payload round-trip");
    }
    // PvP hit
    {
        net::Writer w;
        write_pvp_hit(w, {1, 0, 3, 4});
        net::Reader r(w.data.data(), w.data.size());
        PvpHitMsg out{};
        CHECK(read_pvp_hit(r, out) && out.attacker == 1 && out.victim == 0 && out.dmg == 3 && out.type == 4,
              "pvp_hit round-trip");
        // Invalid self-hit rejected
        net::Writer bad;
        bad.u8(1); bad.u8(1); bad.u8(3); bad.u8(4);
        net::Reader rb(bad.data.data(), bad.data.size());
        CHECK(!read_pvp_hit(rb, out), "pvp_hit self-hit rejected");
    }
    // PvP kill
    {
        net::Writer w;
        write_pvp_kill(w, {0, 12.5f, -4.25f, 0.0f, 1.5f, 2});
        net::Reader r(w.data.data(), w.data.size());
        PvpKillMsg out{};
        CHECK(read_pvp_kill(r, out) && out.killer == 0 && std::fabs(out.x - 12.5f) < 1e-5f &&
                  std::fabs(out.y + 4.25f) < 1e-5f && out.level == 2,
              "pvp_kill round-trip");
        // NaN position rejected
        net::Writer bad;
        bad.u8(0); bad.f32(std::numeric_limits<float>::quiet_NaN()); bad.f32(0.0f); bad.f32(0.0f); bad.f32(1.0f); bad.u8(1);
        net::Reader rb(bad.data.data(), bad.data.size());
        CHECK(!read_pvp_kill(rb, out), "pvp_kill with NaN pos rejected");
    }
    // Draft apply
    {
        net::Writer w;
        write_draft_apply(w, {kDraftOpUnlockPart, 6});
        net::Reader r(w.data.data(), w.data.size());
        DraftApplyMsg out{};
        CHECK(read_draft_apply(r, out) && out.op == kDraftOpUnlockPart && out.value == 6, "draft unlock round-trip");

        net::Writer w2;
        write_draft_apply(w2, {kDraftOpPayout, 250});
        net::Reader r2(w2.data.data(), w2.data.size());
        CHECK(read_draft_apply(r2, out) && out.op == kDraftOpPayout && out.value == 250, "draft payout round-trip");

        net::Writer bad;
        bad.u8(kDraftOpUnlockPart); bad.u32(99);
        net::Reader rb(bad.data.data(), bad.data.size());
        CHECK(!read_draft_apply(rb, out), "draft unlock out-of-range part rejected");
    }
    // Food rollback
    {
        net::Writer w;
        write_food_rollback(w, -12);
        net::Reader r(w.data.data(), w.data.size());
        FoodRollbackMsg out{};
        CHECK(read_food_rollback(r, out) && out.delta == -12, "food rollback round-trip");

        net::Writer bad;
        bad.u32(50);
        net::Reader rb(bad.data.data(), bad.data.size());
        CHECK(!read_food_rollback(rb, out), "positive food rollback rejected");
    }
    // Truncated input safety
    {
        uint8_t tiny[2] = {1, 2};
        net::Reader r1(tiny, 2);
        ProgressMsg pm{};
        CHECK(!read_progress(r1, pm), "truncated progress rejected");
        net::Reader r2(tiny, 2);
        PvpHitMsg hm{};
        CHECK(!read_pvp_hit(r2, hm), "truncated pvp_hit rejected");
        net::Reader r3(tiny, 2);
        PvpKillMsg km{};
        CHECK(!read_pvp_kill(r3, km), "truncated pvp_kill rejected");
        net::Reader r4(tiny, 2);
        DraftApplyMsg dm{};
        CHECK(!read_draft_apply(r4, dm), "truncated draft_apply rejected");
        net::Reader r5(tiny, 2);
        FoodRollbackMsg fm{};
        CHECK(!read_food_rollback(r5, fm), "truncated food_rollback rejected");
    }
}

static void test_stage_tagged_payout_non_theft()
{
    printf("cell_rivals: stage-tagged draft payout non-theft (BUG-1552)\n");
    using namespace cell_rivals_logic;
    rivals::State st;
    st.config.draft_picks = 2;
    uint8_t ranks[rivals::kMaxPlayers] = {1, 0, 0, 0};
    bool present[rivals::kMaxPlayers] = {true, true, false, false};

    // Creature stage draft finishes while host is in Creature stage: Cell Rivals must NOT steal it!
    std::vector<rivals::Draft::Item> c_items = {{101, 1, "Bite L3"}, {102, 1, "Spit L3"}};
    CHECK(st.draft.start(rivals::Stage::Creature, 0, c_items, 2, ranks, present, 10.0), "Creature draft starts");
    int32_t pay = 0;
    CHECK(st.draft.pick(1, 101, &pay).empty() && pay == 100, "Creature draft pick 1 pays 100");
    CHECK(st.draft.pick(1, 102, &pay).empty() && pay == 150, "Creature draft pick 2 pays 150");
    CHECK(!st.draft.active && st.draft.pending_payout[0] == 250 &&
              st.draft.pending_payout_stage[0] == rivals::Stage::Creature,
          "Creature payout tagged with Stage::Creature");
    CHECK(st.draft.take_pending_payout_for(rivals::Stage::Cell, 0) == 0,
          "Cell Rivals take_pending_payout_for(Stage::Cell) refuses Creature payout");
    CHECK(st.draft.pending_payout[0] == 250 && st.draft.pending_payout_stage[0] == rivals::Stage::Creature,
          "Creature payout remains intact for Tribe stage");
    CHECK(st.draft.take_pending_payout_for(rivals::Stage::Creature, 0) == 250,
          "Tribe stage claims Creature payout cleanly");
    CHECK(st.draft.take_pending_payout_for(rivals::Stage::Creature, 0) == 0, "Creature payout cleared once claimed");

    // Cell stage draft payout IS claimed by Stage::Cell
    uint8_t ranks_p1[rivals::kMaxPlayers] = {0, 1, 0, 0};
    auto cell_items = build_draft_items((uint16_t)((1u << 6) | (1u << 8)), 0);
    CHECK(st.draft.start(rivals::Stage::Cell, 1, cell_items, 2, ranks_p1, present, 20.0), "Cell draft starts for guest winner");
    CHECK(st.draft.pick(0, part_index_to_item_id(6), &pay).empty() && pay == 100, "host picks winner part 6");
    CHECK(st.draft.take_pending_payout_for(rivals::Stage::Creature, 1) == 0, "Creature refuses Cell payout");
    CHECK(st.draft.take_pending_payout_for(rivals::Stage::Cell, 1) == 100, "Cell claims Cell payout");
}

static void test_guest_wins_first_host_unlock()
{
    printf("cell_rivals: guest-wins-first host Cell->Creature unlock policy (BUG-1552)\n");
    using namespace cell_rivals_logic;
    // Only unlock on host, in Cell Rivals, AFTER race end handled, catch-up ended, draft ended, guest won, host < 1000
    CHECK(!should_unlock_host_stage_end(false, true, true, true, false, false, 1, 400, false), "guest never runs host unlock");
    CHECK(!should_unlock_host_stage_end(true, false, true, true, false, false, 1, 400, false), "inactive rivals never unlocks");
    CHECK(!should_unlock_host_stage_end(true, true, false, true, false, false, 1, 400, false), "waits for on_cell_race_end");
    CHECK(!should_unlock_host_stage_end(true, true, true, false, false, false, 1, 400, false), "waits for stage_finished");
    CHECK(!should_unlock_host_stage_end(true, true, true, true, true, false, 1, 400, false), "waits for catch-up to finish");
    CHECK(!should_unlock_host_stage_end(true, true, true, true, false, true, 1, 400, false), "waits for draft to finish");
    CHECK(!should_unlock_host_stage_end(true, true, true, true, false, false, 0, 400, false), "host-won race does not trigger");
    CHECK(!should_unlock_host_stage_end(true, true, true, true, false, false, 255, 400, false), "no winner does not trigger");
    CHECK(!should_unlock_host_stage_end(true, true, true, true, false, false, 1, 1000, false), "host already at 1000 skipped");
    CHECK(!should_unlock_host_stage_end(true, true, true, true, false, false, 1, 400, true), "one-shot guard prevents repeat");
    CHECK(should_unlock_host_stage_end(true, true, true, true, false, false, 1, 400, false),
          "guest winner + catch-up & draft done + host < 1000 unlocks host bar");
    CHECK(should_unlock_host_stage_end(true, true, true, true, false, false, 3, 0, false),
          "4p guest winner #3 + host at 0 unlocks host bar");
}

static void test_3p_4p_draft_filter_and_resolution()
{
    printf("cell_rivals: 3-4 player Cell draft filter & no-op pick prevention (BUG-1552)\n");
    using namespace cell_rivals_logic;
    // Winner P0 owns {0, 4, 6, 8}; P1 owns {0, 4}; P2 owns {0, 6}; P3 owns {0, 4, 6}
    uint16_t winner = (uint16_t)((1u << 0) | (1u << 4) | (1u << 6) | (1u << 8));
    uint16_t pickers[3] = {
        (uint16_t)((1u << 0) | (1u << 4)),
        (uint16_t)((1u << 0) | (1u << 6)),
        (uint16_t)((1u << 0) | (1u << 4) | (1u << 6)),
    };
    uint16_t mask = draft_missing_union_mask(winner, pickers, 3);
    CHECK(mask == ((1u << 4) | (1u << 6) | (1u << 8)), "union of missing excludes universally-owned part 0");

    auto items = build_draft_items_for_pickers(winner, pickers, 3);
    CHECK(items.size() == 3, "3 parts offered across 3 pickers");
    // Part 8 is missing on ALL 3 pickers -> prioritized first!
    CHECK(item_id_to_part_index(items[0].id) == 8, "universally-missing part 8 prioritized first");
    CHECK(item_id_to_part_index(items[1].id) == 4 && item_id_to_part_index(items[2].id) == 6,
          "remaining missing parts follow unlockable order");

    // When every picker already owns every winner part, draft list is empty (no pointless no-op draft)
    uint16_t all_owned[2] = {winner, (uint16_t)(winner | (1u << 9))};
    CHECK(draft_missing_union_mask(winner, all_owned, 2) == 0, "all pickers own all winner parts -> mask 0");
    CHECK(build_draft_items_for_pickers(winner, all_owned, 2).empty(), "empty draft when all pickers own all winner parts");
    CHECK(build_draft_items(winner, winner).empty(), "2-arg build_draft_items returns empty when common_mask covers winner");

    // Per-picker resolution prevents wasted no-op picks when picker clicks a part another picker lacked
    CHECK(picker_needs_part(pickers[0], 6) && !picker_needs_part(pickers[0], 4), "picker_needs_part checks ownership");
    CHECK(resolve_draft_pick(winner, pickers[0], 6) == 6, "P1 picking missing part 6 grants part 6");
    CHECK(resolve_draft_pick(winner, pickers[0], 4) == 6, "P1 picking already-owned part 4 substitutes first missing part 6");
    CHECK(resolve_draft_pick(winner, pickers[2], 4) == 8, "P3 picking already-owned part 4 substitutes only missing part 8");
    CHECK(resolve_draft_pick(winner, winner, 8) == -1, "picker owning all winner parts returns -1 (no-op skipped)");
}

static void test_poison_cloud_pvp_attribution()
{
    printf("cell_rivals: 3-4 player poison cloud owner flags & PvP attribution (BUG-1552)\n");
    using namespace cell_rivals_logic;
    // Flags round-trip for players 0..3 while preserving dying/dead/product/own bits (0..3)
    for (uint8_t owner = 0; owner < rivals::kMaxPlayers; owner++) {
        uint8_t base = 0x05u; // dying | product
        uint8_t fl = encode_cloud_owner_flags(base, owner);
        CHECK((fl & 0x0Fu) == base, "cloud owner encoding preserves low 4 state flags");
        uint8_t decoded = 255;
        CHECK(decode_cloud_owner_flags(fl, &decoded) && decoded == owner, "cloud owner decodes accurately");
    }
    // Legacy host without bit 4 set
    uint8_t legacy_owner = 99;
    CHECK(!decode_cloud_owner_flags(0x04u, &legacy_owner) && legacy_owner == 99, "legacy flags without bit 4 decode as unknown");

    // 3-4p Cell Rivals: P1 is at War with P2, Neutral with P0 (host)
    CHECK(!allow_poison_cloud_pvp(true, 1, true, 0, false, true, false),
          "neutral host P0 cloud blocked on P1 even though P1 is at war with P2");
    CHECK(allow_poison_cloud_pvp(true, 1, true, 2, true, true, false),
          "enemy P2 cloud allowed on P1 when at war");
    CHECK(!allow_poison_cloud_pvp(true, 1, true, 1, true, true, true),
          "own cloud never hurts self even if war flag true");
    CHECK(allow_poison_cloud_pvp(true, 1, false, 255, false, true, false),
          "legacy host without owner bits falls back to can_pvp_any");
    CHECK(!allow_poison_cloud_pvp(true, 1, false, 255, false, false, true),
          "legacy host with no wars blocks poison");
    // Together mode: follows together_allow
    CHECK(!allow_poison_cloud_pvp(false, 1, true, 0, true, true, false), "Together ff off blocks partner poison");
    CHECK(allow_poison_cloud_pvp(false, 1, true, 0, false, false, true), "Together ff on allows partner poison");
}

static void test_catchup_wscale_policy()
{
    printf("cell_rivals: catch-up wscale divergence & local pool hysteresis (BUG-1552)\n");
    using namespace cell_rivals_logic;
    // Extreme divergence: host level 10 (15000) vs guest level 1 (0.5) -> ratio 30,000x
    CHECK(scale_diverged(15000.0f, 0.5f), "30,000x scale ratio triggers divergence");
    CHECK(scale_diverged(0.5f, 15000.0f), "1/30,000x scale ratio triggers divergence");
    CHECK(!scale_diverged(1.0f, 1.0f), "1x ratio is not diverged");
    CHECK(!scale_diverged(8.0f, 1.0f), "exact 8.0x boundary is not diverged");
    CHECK(scale_diverged(8.1f, 1.0f), "above 8.0x is diverged");
    CHECK(!scale_diverged(0.125f, 1.0f), "exact 0.125x boundary is not diverged");
    CHECK(scale_diverged(0.12f, 1.0f), "below 0.125x is diverged");
    CHECK(!scale_diverged(0.0f, 1.0f) && !scale_diverged(1.0f, 0.0f), "zero scale does not trigger divergence");
    CHECK(!scale_diverged(std::numeric_limits<float>::quiet_NaN(), 1.0f), "NaN scale does not trigger divergence");

    // Hysteresis: enter at >8x / <0.125x, stay local in (4x, 8x], return to host pool at <=4x & >=0.25x
    CHECK(!should_use_local_pool(false, 15000.0f, 0.5f, false), "Together mode never switches to local pool");
    CHECK(should_use_local_pool(true, 15000.0f, 0.5f, false), "Rivals enters local pool at 30,000x");
    CHECK(should_use_local_pool(true, 6.0f, 1.0f, true), "stays local at 6x (hysteresis)");
    CHECK(!should_use_local_pool(true, 6.0f, 1.0f, false), "does not enter local at 6x");
    CHECK(!should_use_local_pool(true, 4.0f, 1.0f, true), "rejoins host pool once converged to 4x");
    CHECK(should_use_local_pool(true, 0.2f, 1.0f, true), "stays local at 0.2x (hysteresis)");
    CHECK(!should_use_local_pool(true, 0.25f, 1.0f, true), "rejoins host pool once converged to 0.25x");
}

int main()
{
    test_progress_and_parts();
    test_policy_gates();
    test_war_pvp_routing();
    test_meat_drop_and_scale();
    test_cell_part_draft_and_race_flow();
    test_wire_codecs();
    test_stage_tagged_payout_non_theft();
    test_guest_wins_first_host_unlock();
    test_3p_4p_draft_filter_and_resolution();
    test_poison_cloud_pvp_attribution();
    test_catchup_wscale_policy();
    printf("cell_rivals_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
