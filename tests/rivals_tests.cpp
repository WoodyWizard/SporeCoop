// Portable unit tests for shared Rivals mode logic and wire codec (WP-R1 of 0.8.0, BUG-0794).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "rivals_logic.h"
#include "wire/rivals.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (cond) {                                                           \
            g_pass++;                                                         \
        } else {                                                              \
            g_fail++;                                                         \
            printf("FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg);            \
        }                                                                     \
    } while (0)

static void test_stage_modes_and_config()
{
    printf("rivals: stage modes, presets, config clamping\n");
    rivals::Config c;
    CHECK(c.preset() == rivals::Preset::AllTogether, "default preset is AllTogether");
    CHECK(!c.any_rivals(), "default has no rivals stages");
    CHECK(c.catchup_secs == 300, "default catch-up timer is 300 s");
    CHECK(c.draft_picks == 3, "default draft picks is 3");
    CHECK(c.base_distance == rivals::BaseDistance::Middle, "default base distance is Middle");

    c.apply_preset(rivals::Preset::AllRivals);
    CHECK(c.preset() == rivals::Preset::AllRivals, "AllRivals preset sets all 5 stages");
    for (int i = 0; i < rivals::kStages; i++)
        CHECK(c.stage_is_rivals((rivals::Stage)i), "every stage is Rivals");

    c.set_stage_mode(rivals::Stage::Tribe, rivals::StageMode::Together);
    CHECK(c.preset() == rivals::Preset::Custom, "mixed stages report Custom preset");
    CHECK(!c.stage_is_rivals(rivals::Stage::Tribe), "Tribe toggled back to Together");
    CHECK(c.stage_is_rivals(rivals::Stage::Civ), "Civ remains Rivals");

    c.apply_preset(rivals::Preset::AllTogether);
    CHECK(c.preset() == rivals::Preset::AllTogether, "AllTogether resets all stages");

    c.catchup_secs = 10;
    c.draft_picks = 99;
    c.base_distance = (rivals::BaseDistance)9;
    c.clamp();
    CHECK(c.catchup_secs == 60, "catchup_secs clamps to min 60");
    CHECK(c.draft_picks == 5, "draft_picks clamps to max 5");
    CHECK(c.base_distance == rivals::BaseDistance::Middle, "invalid base_distance resets to Middle");

    c.catchup_secs = 9999;
    c.clamp();
    CHECK(c.catchup_secs == 600, "catchup_secs clamps to max 600");

    CHECK(rivals::stage_from_mode(rivals::kModeCell) == rivals::Stage::Cell, "Cell mode mapping");
    CHECK(rivals::stage_from_mode(rivals::kModeCreature) == rivals::Stage::Creature, "Creature mode mapping");
    CHECK(rivals::stage_from_mode(rivals::kModeTribe) == rivals::Stage::Tribe, "Tribe mode mapping");
    CHECK(rivals::stage_from_mode(rivals::kModeCiv) == rivals::Stage::Civ, "Civ mode mapping");
    CHECK(rivals::stage_from_mode(rivals::kModeSpace) == rivals::Stage::Space, "Space mode mapping");
    CHECK(rivals::stage_from_mode(0) == rivals::Stage::None, "unknown mode maps to None");
    CHECK(rivals::parse_stage("civ") == rivals::Stage::Civ, "parse_stage civ");
    CHECK(rivals::parse_stage("Civilization") == rivals::Stage::Civ, "parse_stage Civilization");
}

static void test_symmetric_relations_and_break_countdown()
{
    printf("rivals: symmetric 4x4 relations, proposals, 60 s alliance break countdown\n");
    rivals::Relations rel;
    CHECK(rel.get(0, 1) == rivals::Relation::Neutral && rel.get(1, 0) == rivals::Relation::Neutral,
          "initial relation is Neutral");

    std::string ev;
    // Propose alliance 0 -> 1, then 1 declines
    CHECK(rel.apply_action(0, 1, rivals::RelAction::ProposeAlly, 10.0, &ev).empty() && ev == "proposed_ally",
          "0 proposes ally to 1");
    CHECK(rel.has_proposal(0, 1, rivals::ProposalKind::Ally), "proposal stored");
    CHECK(rel.apply_action(1, 0, rivals::RelAction::DeclineAlly, 11.0, &ev).empty() && ev == "declined_ally",
          "1 declines ally");
    CHECK(!rel.has_proposal(0, 1, rivals::ProposalKind::Ally), "declined proposal cleared");
    CHECK(rel.get(0, 1) == rivals::Relation::Neutral, "still neutral after decline");

    // Propose alliance 0 -> 1, then 1 accepts -> symmetric Ally
    CHECK(rel.apply_action(0, 1, rivals::RelAction::ProposeAlly, 12.0, &ev).empty(), "re-propose ally");
    CHECK(rel.apply_action(1, 0, rivals::RelAction::AcceptAlly, 13.0, &ev).empty() && ev == "allied",
          "1 accepts ally");
    CHECK(rel.get(0, 1) == rivals::Relation::Ally && rel.get(1, 0) == rivals::Relation::Ally,
          "symmetric Ally");

    // Direct DeclareWar while allied is rejected (must break alliance with 60 s notice)
    CHECK(!rel.apply_action(0, 1, rivals::RelAction::DeclareWar, 14.0).empty(),
          "cannot skip break notice when allied");
    CHECK(rel.get(0, 1) == rivals::Relation::Ally, "still allied");

    // BreakAlly starts 60 s countdown
    CHECK(rel.apply_action(0, 1, rivals::RelAction::BreakAlly, 100.0, &ev).empty() && ev == "break_pending",
          "BreakAlly starts 60 s notice");
    CHECK(!rel.apply_action(1, 0, rivals::RelAction::BreakAlly, 105.0).empty(),
          "duplicate BreakAlly rejected while pending");
    const auto* bn = rel.find_break_notice(1, 0);
    CHECK(bn != nullptr && bn->a == 0 && bn->b == 1 && bn->initiator == 0, "break notice found symmetrically");

    // At t = 159.0 (59 s elapsed): still Ally
    auto exp1 = rel.tick(159.0);
    CHECK(exp1.empty(), "not expired at 59 s");
    CHECK(rel.get(0, 1) == rivals::Relation::Ally && rel.get(1, 0) == rivals::Relation::Ally,
          "still Ally during countdown");
    CHECK(std::fabs(rel.find_break_notice(0, 1)->remaining_secs - 1.0) < 1e-6, "remaining_secs updated to 1 s");

    // At t = 160.0 (60 s elapsed): auto-transitions to War!
    auto exp2 = rel.tick(160.0);
    CHECK(exp2.size() == 1 && exp2[0].a == 0 && exp2[0].b == 1, "expired at 60 s");
    CHECK(rel.get(0, 1) == rivals::Relation::War && rel.get(1, 0) == rivals::Relation::War,
          "auto-transitioned to symmetric War");
    CHECK(rel.find_break_notice(0, 1) == nullptr, "break notice removed after transition");

    // Peace proposal + accept -> back to Neutral
    CHECK(rel.apply_action(0, 1, rivals::RelAction::ProposePeace, 170.0, &ev).empty() && ev == "proposed_peace",
          "0 proposes peace");
    CHECK(rel.apply_action(1, 0, rivals::RelAction::AcceptPeace, 171.0, &ev).empty() && ev == "peace",
          "1 accepts peace");
    CHECK(rel.get(0, 1) == rivals::Relation::Neutral && rel.get(1, 0) == rivals::Relation::Neutral,
          "back to Neutral after peace");

    // Unilateral DeclareWar on Neutral pair (2, 3) works immediately
    CHECK(rel.apply_action(2, 3, rivals::RelAction::DeclareWar, 180.0, &ev).empty() && ev == "war",
          "unilateral DeclareWar works on Neutral");
    CHECK(rel.get(2, 3) == rivals::Relation::War && rel.get(3, 2) == rivals::Relation::War,
          "pair (2,3) symmetrically at War");
    CHECK(rel.get(0, 2) == rivals::Relation::Neutral, "unrelated pair (0,2) stays Neutral");

    // Configurable delay seam
    rel.break_delay_secs = 2.0;
    CHECK(rel.apply_action(0, 2, rivals::RelAction::ProposeAlly, 200.0).empty(), "0 proposes ally to 2");
    CHECK(rel.apply_action(2, 0, rivals::RelAction::ProposeAlly, 200.5, &ev).empty() && ev == "allied",
          "mutual ProposeAlly completes alliance");
    CHECK(rel.apply_action(2, 0, rivals::RelAction::BreakAlly, 201.0).empty(), "fast break starts");
    CHECK(rel.tick(202.5).empty() && rel.get(0, 2) == rivals::Relation::Ally, "still Ally at 1.5 s");
    CHECK(rel.tick(203.1).size() == 1 && rel.get(0, 2) == rivals::Relation::War, "War at 2.1 s");
}

static void test_influence_ledger()
{
    printf("rivals: influence earn, rising buy cost, protect/turn/smooth\n");
    rivals::Influence inf;
    CHECK(inf.balance[0] == 0 && inf.buy_cost(0) == 50, "initial balance 0 and buy cost 50");

    // Rising cost: base * (1 + buys) => 50, 100, 150
    int32_t cost = 0;
    CHECK(inf.buy(0, cost) && cost == 50 && inf.balance[0] == 25 && inf.buys[0] == 1, "1st buy cost 50");
    CHECK(inf.buy(0, cost) && cost == 100 && inf.balance[0] == 50 && inf.buys[0] == 2, "2nd buy cost 100");
    CHECK(inf.buy(0, cost) && cost == 150 && inf.balance[0] == 75 && inf.buys[0] == 3, "3rd buy cost 150");

    // Earn influence for player 1
    CHECK(inf.earn(1, 40) && inf.balance[1] == 40, "player 1 earns 40 influence");

    // Player 1 protects NPC 0x100 for 600 s
    CHECK(inf.spend_protect(1, 0x100u, 1000.0).empty() && inf.balance[1] == 20, "player 1 spends 20 to protect");
    CHECK(inf.is_protected(1, 0x100u, 1000.0), "NPC 0x100 protected for player 1");
    CHECK(inf.is_protected(1, 0x100u, 1599.0), "still protected at 599 s");

    // Player 0 tries TurnAgainst on protected NPC -> blocked, no influence deducted
    CHECK(!inf.spend_turn_against(0, 0x100u, 1, 1100.0).empty(), "TurnAgainst blocked by active Protect");
    CHECK(inf.balance[0] == 75, "blocked TurnAgainst deducts no influence");

    // After 600 s protection expires -> TurnAgainst succeeds
    CHECK(inf.tick(1600.0), "protection pruned at 600 s");
    CHECK(!inf.is_protected(1, 0x100u, 1600.0), "protection expired");
    CHECK(inf.spend_turn_against(0, 0x100u, 1, 1600.0).empty() && inf.balance[0] == 45,
          "TurnAgainst succeeds after protection expiry");

    // SmoothOver spends 25
    CHECK(inf.spend_smooth_over(0, 0x200u, 1601.0).empty() && inf.balance[0] == 20, "SmoothOver deducts 25");
    CHECK(!inf.spend_smooth_over(0, 0x200u, 1602.0).empty() && inf.balance[0] == 20,
          "SmoothOver rejected when insufficient balance");
}

static void test_race_and_catchup_timer()
{
    printf("rivals: stage race progress, finish order, catch-up timer\n");
    rivals::Race race;
    race.reset(rivals::Stage::Creature);

    rivals::PlayerProgress p0;
    p0.player_id = 0;
    p0.primary = 4.0f;
    p0.secondary = 250.0f;
    p0.stat_a = 3;
    p0.stat_b = 1;
    race.set_progress(0, p0);
    CHECK(race.players[0].present && race.players[0].primary == 4.0f && race.players[0].stat_a == 3,
          "progress stored");

    // 3 active players; player 1 wins first -> starts 300 s catch-up timer
    CHECK(race.report_complete(1, 3, 300.0, 50.0) == 1, "player 1 finishes 1st");
    CHECK(race.winner == 1 && race.catchup_active && !race.stage_finished, "catch-up started");
    CHECK(race.report_complete(1, 3, 300.0, 55.0) == 1, "duplicate complete returns existing rank");

    // Player 0 finishes 2nd during timer -> timer still running for player 2
    CHECK(race.report_complete(0, 3, 300.0, 100.0) == 2, "player 0 finishes 2nd");
    CHECK(race.catchup_active && !race.stage_finished, "catch-up still running with 1 player left");

    // Tick before expiry
    CHECK(!race.tick(349.0) && std::fabs(race.catchup_remaining_secs - 1.0) < 1e-6, "1 s left");
    // Tick at expiry -> stage finished
    CHECK(race.tick(350.0) && !race.catchup_active && race.stage_finished, "catch-up expired at 300 s");
    CHECK(race.players[2].finish_rank == 0, "player 2 did not finish in time");

    // All players finish early -> catch-up stops immediately
    rivals::Race r2;
    r2.reset(rivals::Stage::Cell);
    CHECK(r2.report_complete(0, 2, 300.0, 10.0) == 1 && r2.catchup_active, "2-player race catch-up active");
    CHECK(r2.report_complete(1, 2, 300.0, 20.0) == 2 && !r2.catchup_active && r2.stage_finished,
          "all players finished ends catch-up immediately");
}

static void test_draft_and_escalating_payouts()
{
    printf("rivals: draft picks, rank penalty, escalating payouts, timeout\n");
    rivals::Draft d;
    std::vector<rivals::Draft::Item> items = {
        {101, 1, "Spit"},
        {102, 1, "Wings"},
        {103, 2, "Horns"},
        {104, 2, "Feathers"},
    };
    uint8_t ranks[4] = {1, 2, 0, 0}; // P0 winner, P1 2nd (in time), P2 unfinished, P3 absent
    bool present[4] = {true, true, true, false};

    CHECK(d.start(rivals::Stage::Creature, 0, items, 3, ranks, present, 500.0), "draft started");
    CHECK(d.active && d.winner == 0, "draft active with winner 0");
    CHECK(d.players[0].picks_left == 0 && d.players[0].done, "winner has 0 picks");
    CHECK(d.players[1].picks_left == 2 && !d.players[1].done, "2nd place gets picks-1 = 2");
    CHECK(d.players[2].picks_left == 3 && !d.players[2].done, "unfinished player gets full 3 picks");
    CHECK(d.players[3].done, "absent player marked done");

    // Edge case: base_picks = 1 -> 2nd place still gets min 1!
    CHECK(rivals::Draft::compute_picks(1, 2) == 1, "rank penalty min 1 when base_picks=1");
    CHECK(rivals::Draft::compute_picks(0, 2) == 0, "0 picks when base_picks=0");

    // Picks pay 100, 150, 200...
    int32_t pay = 0;
    CHECK(d.pick(1, 101, &pay).empty() && pay == 100, "1st pick pays 100");
    CHECK(!d.pick(1, 101, &pay).empty(), "duplicate pick by same player rejected");
    CHECK(d.pick(2, 101, &pay).empty() && pay == 150, "2nd pick (by P2) pays 150");
    CHECK(d.pick(1, 102, &pay).empty() && pay == 200, "3rd pick pays 200");
    CHECK(d.players[1].done && d.players[1].picks_left == 0, "P1 done after 2 picks");
    CHECK(!d.pick(1, 103, &pay).empty(), "P1 cannot pick more");
    CHECK(d.winner_payout == 450 && d.pending_payout[0] == 450, "total winner payout 100+150+200=450");

    // P2 skips remaining picks -> draft completes immediately
    CHECK(d.skip(2).empty() && !d.active, "skip ends draft when all done");
    CHECK(d.take_pending_payout(0) == 450 && d.take_pending_payout(0) == 0,
          "take_pending_payout returns 450 once then 0");

    // Timeout test (60 s)
    rivals::Draft d2;
    CHECK(d2.start(rivals::Stage::Cell, 0, items, 2, ranks, present, 1000.0), "second draft started");
    CHECK(!d2.tick(1059.0) && d2.active, "still active at 59 s");
    CHECK(d2.tick(1060.0) && !d2.active, "expired at 60 s");
}

static void test_chronicle_and_store_roundtrip()
{
    printf("rivals: Chronicle scoring and PersistedWorld store round-trip\n");
    rivals::Chronicle ch;
    uint8_t ranks[4] = {2, 1, 3, 0};
    ch.award_stage_placements(rivals::Stage::Creature, ranks);
    CHECK(ch.totals[1] == 100 && ch.totals[0] == 60 && ch.totals[2] == 35 && ch.totals[3] == 0,
          "stage placements awarded 100/60/35");
    CHECK(ch.leader() == 1, "P1 leads");

    ch.add_points(0, rivals::Stage::Space, rivals::kPtsGalaxyCore, "galaxy core finale");
    CHECK(ch.totals[0] == 310 && ch.leader() == 0, "galaxy core finale flips leader to P0");

    // History cap (32)
    for (int i = 0; i < 40; i++)
        ch.add_points(2, rivals::Stage::Tribe, 5, "ally tribe");
    CHECK((int)ch.history.size() == rivals::kMaxChronicleHistory, "Chronicle history capped at 32");

    // Store persistence round-trip
    rivals::PersistedWorld pw;
    pw.config.set_stage_mode(rivals::Stage::Cell, rivals::StageMode::Rivals);
    pw.config.set_stage_mode(rivals::Stage::Space, rivals::StageMode::Rivals);
    pw.config.catchup_secs = 240;
    pw.config.draft_picks = 4;
    pw.config.base_distance = rivals::BaseDistance::Far;
    pw.rel_by_key[rivals::PersistedWorld::norm_pair("host", "s:1234")] = rivals::Relation::War;
    pw.chronicle_by_key["host"] = 310;
    pw.chronicle_by_key["s:1234"] = 100;
    pw.history.push_back({rivals::Stage::Space, 0, 250, "galaxy core finale"});

    auto blob = pw.encode();
    rivals::PersistedWorld pw2;
    CHECK(pw2.decode(blob), "PersistedWorld decodes cleanly");
    CHECK(pw2.config == pw.config, "config matches after store round-trip");
    CHECK(pw2.rel_by_key[rivals::PersistedWorld::norm_pair("host", "s:1234")] == rivals::Relation::War,
          "relation matches after store round-trip");
    CHECK(pw2.chronicle_by_key["host"] == 310 && pw2.chronicle_by_key["s:1234"] == 100,
          "chronicle totals match after store round-trip");
    CHECK(pw2.history.size() == 1 && pw2.history[0].reason == "galaxy core finale",
          "history matches after store round-trip");

    pw2.adopt_key("s:1234", "s:9999");
    CHECK(pw2.chronicle_by_key["s:9999"] == 100 && !pw2.chronicle_by_key.count("s:1234"), "adopt moves chronicle");
    CHECK(pw2.rel_by_key[rivals::PersistedWorld::norm_pair("host", "s:9999")] == rivals::Relation::War,
          "adopt moves relation");
}

static void test_wire_roundtrip_and_fuzz_rejection()
{
    printf("rivals: wire::rivals round-trips and malformed payload rejection\n");
    rivals::State st;
    st.rev = 42;
    st.config.set_stage_mode(rivals::Stage::Cell, rivals::StageMode::Rivals);
    st.config.set_stage_mode(rivals::Stage::Tribe, rivals::StageMode::Rivals);
    st.config.catchup_secs = 180;
    st.config.draft_picks = 2;
    st.config.base_distance = rivals::BaseDistance::Near;

    st.rel.apply_action(0, 1, rivals::RelAction::ProposeAlly, 10.0);
    st.rel.apply_action(1, 0, rivals::RelAction::AcceptAlly, 11.0);
    st.rel.apply_action(0, 1, rivals::RelAction::BreakAlly, 100.0);
    st.rel.apply_action(2, 3, rivals::RelAction::DeclareWar, 100.0);
    st.rel.apply_action(2, 3, rivals::RelAction::ProposePeace, 101.0);

    st.inf.earn(0, 80);
    st.inf.spend_protect(0, 0xCAFEu, 100.0);
    int32_t c_out = 0;
    st.inf.buy(1, c_out);

    st.race.reset(rivals::Stage::Tribe);
    rivals::PlayerProgress pp;
    pp.player_id = 0;
    pp.present = true;
    pp.primary = 3.5f;
    pp.secondary = 420.0f;
    pp.stat_a = 9;
    pp.stat_b = 2;
    pp.extra = 5.0f;
    st.race.set_progress(0, pp);
    st.race.report_complete(0, 2, 180.0, 100.0);

    uint8_t ranks[4] = {1, 0, 0, 0};
    bool pres[4] = {true, true, false, false};
    st.draft.start(rivals::Stage::Tribe, 0, {{7, 2, "Healer Staff"}, {8, 1, "Axe"}}, 2, ranks, pres, 100.0);
    st.draft.pick(1, 7);
    st.chronicle.add_points(0, rivals::Stage::Tribe, 100, "stage win");

    auto sn = wire::rivals::Snapshot::from_state(st, 110.0);
    net::Writer w;
    sn.write(w);

    net::Reader r(w.data.data(), w.data.size());
    wire::rivals::Snapshot sn2;
    CHECK(sn2.read(r) && r.left() == 0, "Snapshot round-trip exact read");

    rivals::State st2;
    sn2.apply_to(st2, 200.0);
    CHECK(st2.rev == 42 && st2.config == st.config, "Snapshot preserves rev and config");
    CHECK(st2.rel.get(0, 1) == rivals::Relation::Ally && st2.rel.get(2, 3) == rivals::Relation::War,
          "Snapshot preserves relations");
    CHECK(st2.rel.find_break_notice(0, 1) != nullptr &&
              std::fabs(st2.rel.find_break_notice(0, 1)->remaining_secs - 50.0) < 0.2,
          "Snapshot preserves break countdown in tenths");
    CHECK(st2.rel.has_proposal(2, 3, rivals::ProposalKind::Peace), "Snapshot preserves proposals");
    CHECK(st2.inf.balance[0] == 60 && st2.inf.buys[1] == 1 && st2.inf.is_protected(0, 0xCAFEu, 200.0),
          "Snapshot preserves influence and protection");
    CHECK(st2.race.winner == 0 && st2.race.catchup_active &&
              std::fabs(st2.race.catchup_remaining_secs - 170.0) < 0.2,
          "Snapshot preserves race winner and catch-up remaining");
    CHECK(st2.draft.active && st2.draft.items.size() == 2 && st2.draft.winner_payout == 100 &&
              st2.draft.pending_payout[0] == 100 && st2.draft.players[1].picks_left == 1,
          "Snapshot preserves draft state");
    CHECK(st2.chronicle.totals[0] == 100 && st2.chronicle.history.size() == 1, "Snapshot preserves chronicle");

    // Reject truncated prefixes of Snapshot
    for (size_t cut = 0; cut < w.data.size(); cut++) {
        net::Reader rc(w.data.data(), cut);
        wire::rivals::Snapshot bad;
        CHECK(!bad.read(rc), "truncated Snapshot rejected");
    }

    // Reject wrong version
    auto bad_ver = w.data;
    bad_ver[0] = 99;
    {
        net::Reader rv(bad_ver.data(), bad_ver.size());
        wire::rivals::Snapshot sbad;
        CHECK(!sbad.read(rv), "wrong Snapshot version rejected");
    }

    // Reject out-of-range stage mode
    auto bad_mode = w.data;
    bad_mode[5] = 2; // stage_modes[0]
    {
        net::Reader rm(bad_mode.data(), bad_mode.size());
        wire::rivals::Snapshot sbad;
        CHECK(!sbad.read(rm), "out-of-range stage mode rejected");
    }

    // Small request/event codecs round-trips & rejection
    {
        wire::rivals::RelReq req{(uint8_t)rivals::RelAction::BreakAlly, 2}, out;
        net::Writer rw;
        req.write(rw);
        net::Reader rr(rw.data.data(), rw.data.size());
        CHECK(out.read(rr) && out.action == 8 && out.target == 2, "RelReq round-trip");
        uint8_t inv[2] = {9, 0};
        net::Reader rinv(inv, 2);
        CHECK(!out.read(rinv), "invalid RelReq action rejected");
    }
    {
        wire::rivals::InfReq req{(uint8_t)rivals::InfluenceAction::TurnAgainst, 0x12345678u, 1}, out;
        net::Writer rw;
        req.write(rw);
        net::Reader rr(rw.data.data(), rw.data.size());
        CHECK(out.read(rr) && out.action == 2 && out.npc_id == 0x12345678u && out.rival_id == 1, "InfReq round-trip");
        uint8_t inv[6] = {2, 1, 2, 3, 4, 7}; // rival_id=7 invalid
        net::Reader rinv(inv, 6);
        CHECK(!out.read(rinv), "invalid InfReq rival_id rejected");
    }
    {
        wire::rivals::DraftReq req{0, 42u}, out;
        net::Writer rw;
        req.write(rw);
        net::Reader rr(rw.data.data(), rw.data.size());
        CHECK(out.read(rr) && out.skip == 0 && out.item_id == 42u, "DraftReq round-trip");
        uint8_t inv[5] = {2, 0, 0, 0, 0};
        net::Reader rinv(inv, 5);
        CHECK(!out.read(rinv), "invalid DraftReq skip rejected");
    }
    {
        wire::rivals::Event ev{wire::rivals::E_REL, 0, 1, 2, "War declared!"}, out;
        net::Writer rw;
        ev.write(rw);
        net::Reader rr(rw.data.data(), rw.data.size());
        CHECK(out.read(rr) && out.code == 2 && out.text == "War declared!", "Event round-trip");
    }

    // Session 1.4 peer_reads check
    std::vector<net::FamilyRev> old_fams = {{1, 1, 3}};
    std::vector<net::FamilyRev> new_fams = {{1, 1, 4}};
    CHECK(!wire::rivals::peer_reads(false, new_fams), "no CAP_FAMILIES -> false");
    CHECK(!wire::rivals::peer_reads(true, old_fams), "session 1.3 -> false");
    CHECK(wire::rivals::peer_reads(true, new_fams), "session 1.4 -> true");
}

int main()
{
    test_stage_modes_and_config();
    test_symmetric_relations_and_break_countdown();
    test_influence_ledger();
    test_race_and_catchup_timer();
    test_draft_and_escalating_payouts();
    test_chronicle_and_store_roundtrip();
    test_wire_roundtrip_and_fuzz_rejection();
    printf("rivals_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
