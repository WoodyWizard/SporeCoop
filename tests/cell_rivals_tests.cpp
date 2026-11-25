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

int main()
{
    test_progress_and_parts();
    test_policy_gates();
    test_war_pvp_routing();
    test_meat_drop_and_scale();
    test_cell_part_draft_and_race_flow();
    test_wire_codecs();
    printf("cell_rivals_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
