// Portable ASan+UBSan unit tests for Space Rivals logic (WP-R6 of 0.8.0, BUG-1550).
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "space_rivals_logic.h"

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

static spacerivals::CandidatePlanet make_cand(uint32_t star, uint32_t planet, float dist, int32_t ptype = 4,
                                              bool moon = false)
{
    spacerivals::CandidatePlanet c;
    c.star_id = star;
    c.planet_id = planet;
    c.star_type = 3;
    c.planet_type = ptype;
    c.tech_level = 0;
    c.is_moon = moon;
    c.star_owned = false;
    c.star_is_home = false;
    c.dist_from_host = dist;
    c.atmo = 0.5f;
    c.temp = 0.5f;
    c.water = 0.5f;
    c.star_name = "Star-" + std::to_string(star);
    c.planet_name = "Planet-" + std::to_string(planet);
    return c;
}

static void test_r5_emigration_filtering_and_scoring()
{
    printf("space_rivals: R5 emigration filtering, scoring, and BaseDistance\n");
    using namespace spacerivals;

    HomeRef ref;
    ref.star_id = 100;
    ref.planet_id = 1001;
    ref.type = 5; // T3
    ref.is_moon = false;
    ref.atmo = 0.5f;
    ref.temp = 0.5f;
    ref.water = 0.5f;

    std::set<uint32_t> reserved{200};

    // Rejections
    {
        auto c = make_cand(100, 1002, 10.0f);
        CHECK(!is_suitable_emigration_planet(c, ref.star_id, reserved), "host home star rejected");
    }
    {
        auto c = make_cand(200, 2001, 10.0f);
        CHECK(!is_suitable_emigration_planet(c, ref.star_id, reserved), "reserved star rejected");
    }
    {
        auto c = make_cand(300, 3001, 10.0f);
        c.star_owned = true;
        CHECK(!is_suitable_emigration_planet(c, ref.star_id, reserved), "owned star rejected");
    }
    {
        auto c = make_cand(301, 3011, 10.0f);
        c.star_type = 1; // GalacticCore
        CHECK(!is_suitable_emigration_planet(c, ref.star_id, reserved), "galactic core rejected");
    }
    {
        auto c = make_cand(302, 3021, 10.0f, 1); // GasGiant
        CHECK(!is_suitable_emigration_planet(c, ref.star_id, reserved), "gas giant rejected");
    }
    {
        auto c = make_cand(303, 3031, 10.0f, 0); // Asteroid belt
        CHECK(!is_suitable_emigration_planet(c, ref.star_id, reserved), "asteroid belt rejected");
    }
    {
        auto c = make_cand(304, 3041, 10.0f);
        c.tech_level = 4; // existing Civ/Empire
        CHECK(!is_suitable_emigration_planet(c, ref.star_id, reserved), "existing civ/empire planet rejected");
    }
    {
        auto c = make_cand(305, 3051, std::numeric_limits<float>::quiet_NaN());
        CHECK(!is_suitable_emigration_planet(c, ref.star_id, reserved), "NaN distance rejected");
    }

    // Selection prefers full planet over moon, matching T-score & climate, and matches BaseDistance
    std::vector<CandidatePlanet> cands;
    cands.push_back(make_cand(401, 4011, 12.0f, 5, true));  // moon, exact Near dist, T3 -> penalized by is_moon
    cands.push_back(make_cand(402, 4021, 12.0f, 2, false)); // full planet, T0, Near
    cands.push_back(make_cand(403, 4031, 13.0f, 5, false)); // full planet, T3, Near
    cands.push_back(make_cand(404, 4041, 54.0f, 5, false)); // full planet, T3, Far

    int idx_near = pick_emigration_planet(cands, ref, ::rivals::BaseDistance::Near, reserved);
    CHECK(idx_near == 2, "Near prefers nearby full T3 planet (4031)");

    int idx_far = pick_emigration_planet(cands, ref, ::rivals::BaseDistance::Far, reserved);
    CHECK(idx_far == 3, "Far prefers distant full T3 planet (4041)");

    // Tie-breaking is deterministic by lowest planet_id
    std::vector<CandidatePlanet> tied;
    tied.push_back(make_cand(501, 9002, 28.0f, 4, false));
    tied.push_back(make_cand(502, 9001, 28.0f, 4, false));
    int idx_tie = pick_emigration_planet(tied, ref, ::rivals::BaseDistance::Middle, {});
    CHECK(idx_tie == 1, "deterministic tie-breaker picks lower planet_id");

    // Empty candidate list returns -1
    CHECK(pick_emigration_planet({}, ref, ::rivals::BaseDistance::Middle, {}) == -1, "empty candidates returns -1");

    // City count clamp
    CHECK(clamp_emig_cities(0) == kMinEmigCities, "clamp_emig_cities clamps low");
    CHECK(clamp_emig_cities(50) == kMaxEmigCities, "clamp_emig_cities clamps high");
    CHECK(clamp_emig_cities(4) == 4, "clamp_emig_cities preserves valid count");
}

static void test_r5_notice_and_store()
{
    printf("space_rivals: R5 notice EN/RU and EmigrationStore roundtrip\n");
    using namespace spacerivals;

    std::string en = format_emigration_notice("Terra", "Nova", false);
    CHECK(en == "Your people left Terra for Nova", "EN emigration notice matches spec");
    std::string ru = format_emigration_notice("Земля", "Нова", true);
    CHECK(ru.find("Земля") != std::string::npos && ru.find("Нова") != std::string::npos, "RU emigration notice");
    CHECK(!format_emigration_notice("", "", false).empty(), "empty planet fallback EN");
    CHECK(!format_emigration_notice("", "", true).empty(), "empty planet fallback RU");

    EmigrationStore st;
    EmigrationRecord rec;
    rec.player_id = 1;
    rec.orig_home_star = 111;
    rec.orig_home_planet = 1110;
    rec.new_home_star = 222;
    rec.new_home_planet = 2220;
    rec.target_cities = 5;
    rec.old_planet_name = "Old|World\n1";
    rec.new_planet_name = "New\tHome";
    st.by_key["token_abc"] = rec;

    // Invalid record (same planet) must not be encoded
    EmigrationRecord bad;
    bad.orig_home_star = 111;
    bad.orig_home_planet = 1110;
    bad.new_home_star = 111;
    bad.new_home_planet = 1110;
    st.by_key["bad_key"] = bad;

    auto blob = st.encode();
    CHECK(!blob.empty(), "encode produces non-empty blob");

    EmigrationStore decoded;
    CHECK(decoded.decode(blob), "decode returns true");
    CHECK(decoded.by_key.size() == 1, "only valid record decoded");
    const auto& r2 = decoded.by_key["token_abc"];
    CHECK(r2.orig_home_star == 111 && r2.orig_home_planet == 1110, "orig home preserved");
    CHECK(r2.new_home_star == 222 && r2.new_home_planet == 2220, "new home preserved");
    CHECK(r2.target_cities == 5, "target cities preserved");
    CHECK(r2.old_planet_name == "Old World 1", "pipe/newline sanitized in old name");
    CHECK(r2.new_planet_name == "New Home", "tab sanitized in new name");

    decoded.adopt_key("token_abc", "token_xyz");
    CHECK(decoded.by_key.count("token_abc") == 0 && decoded.by_key.count("token_xyz") == 1, "adopt_key rekeys entry");

    // Malformed blob must not crash or yield invalid records
    std::string garbage = "emig nope\nemig k 1 2 0 0 3|a|b\nemig k2 1 2 1 2 3|a|b\n";
    EmigrationStore bad_dec;
    CHECK(!bad_dec.decode(std::vector<uint8_t>(garbage.begin(), garbage.end())), "malformed blob yields false");
    CHECK(bad_dec.by_key.empty(), "malformed blob yields empty map");
}

static void test_r7_homeworld_protection()
{
    printf("space_rivals: R7 homeworld inviolability rules\n");
    using namespace spacerivals;

    HomeProtectionTable tbl;
    tbl.set(0, 100, 1001);
    tbl.set(1, 200, 2001);

    uint8_t owner = 255;
    CHECK(tbl.is_protected_planet(1001, &owner) && owner == 0, "host homeworld planet protected");
    CHECK(tbl.is_protected_star(200, &owner) && owner == 1, "guest homeworld star protected");
    CHECK(!tbl.is_protected_planet(3001), "ordinary colony planet not protected");
    CHECK(!tbl.is_protected_star(300), "ordinary colony star not protected");

    // Banned superweapons blocked on player homeworld, allowed on ordinary colonies
    CHECK(tbl.should_block_tool(kToolPlanetBuster, 1001, &owner) && owner == 0, "Planet Buster blocked on host home");
    CHECK(tbl.should_block_tool(kToolFanaticalFrenzy, 2001, &owner) && owner == 1, "Fanatical Frenzy blocked on guest home");
    CHECK(tbl.should_block_tool(kToolGravitationWave, 2001), "Gravitation Wave blocked on guest home");
    CHECK(!tbl.should_block_tool(kToolPlanetBuster, 3001), "Planet Buster allowed on ordinary colony");
    CHECK(!tbl.should_block_tool(0x12345678u, 1001), "regular laser/bomb tool not banned on homeworld");

    // Capture guard respects emigrating_guard
    CHECK(tbl.should_block_capture(200, 0, false, &owner) && owner == 1, "capture blocked by protected star");
    CHECK(tbl.should_block_capture(0, 1001, false, &owner) && owner == 0, "capture blocked by protected planet");
    CHECK(!tbl.should_block_capture(200, 2001, true), "emigrating_guard temporarily permits own setup");
    CHECK(!tbl.should_block_capture(300, 3001, false), "ordinary colony capture allowed");

    // Notices EN + RU
    CHECK(format_protection_notice(kToolPlanetBuster, false, false).find("Planet Buster") != std::string::npos,
          "Planet Buster EN notice");
    CHECK(format_protection_notice(kToolPlanetBuster, false, true).find("Планетодробитель") != std::string::npos,
          "Planet Buster RU notice");
    CHECK(format_protection_notice(0, true, false).find("captured") != std::string::npos, "Capture EN notice");
    CHECK(format_protection_notice(0, true, true).find("захватить") != std::string::npos, "Capture RU notice");
}

static void test_npc_influence_and_progress_and_finale()
{
    printf("space_rivals: NPC influence, race progress, and Galaxy Core finale\n");
    using namespace spacerivals;

    std::string reason;
    CHECK(influence_gain_for_event(0x0526E519u, &reason) == kInfMissionGain && reason == "mission", "mission gain");
    CHECK(influence_gain_for_event(0x0526E527u, &reason) == kInfTradeGain && reason == "trade", "trade gain");
    CHECK(influence_gain_for_event(0x0526E51Fu, &reason) == kInfGiftGain && reason == "gift", "gift gain");
    CHECK(influence_gain_for_event(0xDEADBEEFu) == 0, "unrelated event gives 0");

    CHECK(hostile_influence_event(0) != hostile_influence_event(1), "hostile events rotate");
    CHECK(friendly_influence_event(0) != friendly_influence_event(1), "friendly events rotate");

    SpaceRaceStats st;
    st.money = 123456;
    st.colonies = 7;
    st.stars = 4;
    st.allied_npcs = 3;
    st.dist_to_core = 42.5f;
    auto pp = build_player_progress(1, st);
    CHECK(pp.present && pp.player_id == 1, "progress player id set");
    CHECK(pp.primary == 7.0f && pp.secondary == 123456.0f, "colonies and money mapped");
    CHECK(pp.stat_a == 4 && pp.stat_b == 3, "stars and allied NPCs mapped");
    CHECK(std::fabs(pp.extra - 42.5f) < 1e-3f, "distance to core mapped");

    st.dist_to_core = std::numeric_limits<float>::quiet_NaN();
    CHECK(build_player_progress(0, st).extra == 0.0f, "NaN distance sanitized to 0");

    // Chronicle Galaxy Core finale: only first player claims bonus; overall winner can be anyone with max total
    FinaleState fin;
    CHECK(!fin.claim_first(99), "invalid player rejected");
    CHECK(fin.claim_first(1), "first valid player claims core");
    CHECK(!fin.claim_first(0), "second player cannot claim core again");
    CHECK(fin.first_player == 1 && fin.awarded, "first_player recorded");

    ::rivals::Chronicle chr;
    chr.add_points(0, ::rivals::Stage::Civ, 400, "prior");
    chr.add_points(1, ::rivals::Stage::Space, kPtsCoreFinale, "core");
    fin.snapshot_chronicle(chr);
    CHECK(fin.totals[0] == 400 && fin.totals[1] == kPtsCoreFinale, "chronicle totals snapshotted");
    CHECK(fin.winner == 0, "overall winner is Chronicle leader (Host 400 > Guest 250)");

    std::string names[::rivals::kMaxPlayers] = {"Host", "Guest", "", ""};
    bool present[::rivals::kMaxPlayers] = {true, true, false, false};
    std::string sum_en = fin.format_summary(names, present, false);
    std::string sum_ru = fin.format_summary(names, present, true);
    CHECK(sum_en.find("Guest") != std::string::npos && sum_en.find("Winner: Host") != std::string::npos &&
              sum_en.find("continues") != std::string::npos,
          "EN finale summary mentions first player, winner, and continues");
    CHECK(sum_ru.find("Guest") != std::string::npos && sum_ru.find("Победитель: Host") != std::string::npos &&
              sum_ru.find("продолжается") != std::string::npos,
          "RU finale summary mentions first player, winner, and continues");
}

static void test_civ_to_space_liftoff_state_machine()
{
    printf("space_rivals: Civ->Space liftoff emigration state machine (BUG-1555)\n");
    using namespace spacerivals;

    EmigrationRecord rec;
    rec.player_id = 1;
    rec.orig_home_star = 100;
    rec.orig_home_planet = 1001;
    rec.new_home_star = 200;
    rec.new_home_planet = 2001;
    rec.target_cities = 2;

    GuestEmigMachine sm;
    // 1. Natural Civ->Space arrival: guest starts landed on host's planet (ctx == 0).
    CHECK(sm.needs_task(1, rec, 1001, 0, 1001, 100), "initial arrival schedules CreateHome");
    CHECK(sm.next_step(1, rec, 1001, 0, 1001, 100) == EmigStep::CreateHome, "phase 1 is CreateHome");

    sm.mark_home_created(1, 2001);
    // Still landed on host planet: RelinquishHost and TeleportHome are deferred cleanly.
    CHECK(!sm.needs_task(1, rec, 2001, 0, 1001, 100), "no task spam while landed on host surface");
    CHECK(sm.next_step(1, rec, 2001, 0, 1001, 100) == EmigStep::None, "relinquish deferred while ctx==0 on host planet");

    // 2. Guest lifts off into solar system view (ctx == 1) still at host star 100:
    // MUST run RelinquishHost AND THEN TeleportHome (never stranded at host star!).
    CHECK(sm.needs_task(1, rec, 2001, 1, 0, 100), "liftoff schedules deferred relinquish+teleport");
    CHECK(sm.next_step(1, rec, 2001, 1, 0, 100) == EmigStep::RelinquishHost, "phase 2 after liftoff is RelinquishHost");

    sm.host_relinquished = true;
    CHECK(sm.needs_task(1, rec, 2001, 1, 0, 100), "after relinquish still at host star schedules TeleportHome");
    CHECK(sm.next_step(1, rec, 2001, 1, 0, 100) == EmigStep::TeleportHome, "phase 3 after relinquish is TeleportHome");

    sm.teleported = true;
    CHECK(!sm.needs_task(1, rec, 2001, 1, 0, 200), "complete after arriving at new home star");
    CHECK(sm.next_step(1, rec, 2001, 1, 0, 200) == EmigStep::None, "no further step when complete");

    // 3. Rejoin / save reload (world epoch increments or empire home reverts to host planet) triggers full re-apply.
    CHECK(sm.needs_reapply(2, 2001, 2001), "new epoch triggers reapply");
    CHECK(sm.needs_reapply(1, 2001, 1001), "reverted empire home planet triggers reapply");
    CHECK(sm.needs_task(2, rec, 1001, 2, 0, 100), "rejoin in galaxy view schedules CreateHome");
    CHECK(sm.next_step(2, rec, 1001, 2, 0, 100) == EmigStep::CreateHome, "rejoin starts at CreateHome");
}

static void test_space_influence_scaling_and_draft_payout()
{
    printf("space_rivals: Space influence buy price scaling & Civ->Space draft payout\n");
    using namespace spacerivals;

    CHECK(influence_buy_space_cost(0) == 0, "0 base price -> 0");
    CHECK(influence_buy_space_cost(-10) == 0, "negative base price -> 0");
    CHECK(influence_buy_space_cost(50) == 10000, "base 50 -> 10,000 Space Sporebucks");
    CHECK(influence_buy_space_cost(100) == 20000, "2nd buy 100 -> 20,000 Space Sporebucks");
    CHECK(influence_buy_space_cost(150) == 30000, "3rd buy 150 -> 30,000 Space Sporebucks");
    CHECK(influence_buy_space_cost(10000000) == 99999999, "overflow clamped safely");

    CHECK(space_draft_payout_money(0) == 0, "0 draft payout -> 0");
    CHECK(space_draft_payout_money(-5) == 0, "negative draft payout -> 0");
    CHECK(space_draft_payout_money(100) == 10000, "1st Civ draft pick 100 -> 10,000 Space Sporebucks");
    CHECK(space_draft_payout_money(150) == 15000, "2nd Civ draft pick 150 -> 15,000 Space Sporebucks");
    CHECK(space_draft_payout_money(250) == 25000, "total 250 -> 25,000 Space Sporebucks");
    CHECK(space_draft_payout_money(10000000) == 99999999, "draft payout overflow clamped safely");
}

static void test_space_npc_ui_list()
{
    printf("space_rivals: Space F9 NPC empire UI list\n");
    using namespace spacerivals;

    std::vector<SpaceNpcSnapshot> snaps = {
        {0x80000003u, "Grox", -80.0f, false, true},
        {0x80000001u, "Zealot Empire", 85.0f, true, false},
        {0x80000002u, "Trader League", 15.0f, false, false},
        {0x80000004u, "", 98.0f, true, false}, // unnamed allied maxed empire -> fallback label + can_smooth=false
    };

    auto list = build_space_npc_ui_list(snaps, true);
    CHECK(list.size() == 4, "all 4 NPC empires included");
    // Allied first (sorted by name: "" before "Zealot Empire"), then neutral, then hostile
    CHECK(list[0].npc_id == 0x80000004u && list[0].allied && !list[0].hostile && list[0].can_protect &&
              !list[0].can_smooth && list[0].can_turn,
          "maxed allied empire has can_protect=true and can_smooth=false");
    CHECK(list[0].label.find("Empire #") != std::string::npos && list[0].label.find("0x") == std::string::npos,
          "unnamed empire uses readable fallback label");

    CHECK(list[1].npc_id == 0x80000001u && list[1].allied && list[1].can_protect && list[1].can_smooth,
          "allied Zealot Empire supports Protect and Smooth");
    CHECK(list[1].label.find("Zealot Empire") != std::string::npos && list[1].label.find("Ally") != std::string::npos,
          "allied label shows readable name and Ally");

    CHECK(list[2].npc_id == 0x80000002u && !list[2].allied && !list[2].hostile && !list[2].can_protect &&
              list[2].can_smooth && list[2].can_turn,
          "neutral Trader League flags correct");

    CHECK(list[3].npc_id == 0x80000003u && !list[3].allied && list[3].hostile && !list[3].can_protect &&
              list[3].can_smooth && list[3].can_turn,
          "hostile Grox sorted last with hostile=true");
    CHECK(list[3].label.find("Grox") != std::string::npos && list[3].label.find("War") != std::string::npos,
          "hostile label shows readable name and War");

    // Without rival: can_turn is false
    auto solo = build_space_npc_ui_list(snaps, false);
    CHECK(!solo[0].can_turn && !solo[2].can_turn, "can_turn disabled when no rival present");

    // Cap at kMaxSpaceNpcUiEntries (16)
    std::vector<SpaceNpcSnapshot> many;
    for (uint32_t i = 1; i <= 25; i++)
        many.push_back({0x80001000u + i, "Emp-" + std::to_string(i), 0.0f, false, false});
    auto capped = build_space_npc_ui_list(many, true);
    CHECK(capped.size() == kMaxSpaceNpcUiEntries, "UI list capped at kMaxSpaceNpcUiEntries");
}

static void test_relinquish_clean_and_color_fallback()
{
    printf("space_rivals: clean relinquish tech-5 clearing and UpdateAndGetColor fallback (BUG-1567)\n");
    using namespace spacerivals;

    // Without clean_fix: star & planets remain tech 5 while empire_id == 0xFFFFFFFF (orphaned tech-5 state).
    RelinquishedStarState dirty;
    dirty.empire_id = 0x80000001u;
    dirty.star_tech = 5u;
    dirty.star_flags = 0x15u;
    dirty.planet_techs = {5u, 1u, 5u, 0u};
    apply_clean_relinquish(dirty, false);
    CHECK(dirty.empire_id == 0xFFFFFFFFu && (dirty.star_flags & 7u) == 0u, "legacy relinquish clears owner and home bits");
    CHECK(is_orphaned_tech5_star(dirty.empire_id, dirty.star_tech), "legacy relinquish leaves orphaned tech-5 star");
    CHECK(dirty.planet_techs[0] == 5u && dirty.planet_techs[2] == 5u, "legacy relinquish leaves tech-5 planets");

    // With clean_fix: star & tech-5 planets drop to kRelinquishedTechLevel (1); lower-tech planets untouched.
    RelinquishedStarState clean;
    clean.empire_id = 0x80000001u;
    clean.star_tech = 5u;
    clean.star_flags = 0x15u;
    clean.planet_techs = {5u, 2u, 5u, 0u};
    apply_clean_relinquish(clean, true);
    CHECK(clean.empire_id == 0xFFFFFFFFu && clean.star_flags == 0x10u, "clean relinquish clears owner and home bits only");
    CHECK(!is_orphaned_tech5_star(clean.empire_id, clean.star_tech), "clean relinquish never leaves orphaned tech-5 star");
    CHECK(clean.star_tech == kRelinquishedTechLevel, "star tech lowered to Creature (1)");
    CHECK(clean.planet_techs[0] == kRelinquishedTechLevel && clean.planet_techs[1] == 2u &&
              clean.planet_techs[2] == kRelinquishedTechLevel && clean.planet_techs[3] == 0u,
          "only tech-5 planets lowered to 1; non-empire planets preserved");

    // UpdateAndGetColor fallback home star selection
    CHECK(pick_color_fallback_home_star(9093181u, true, {9351184u}) == 9093181u,
          "valid current mHomeStarID preserved");
    CHECK(pick_color_fallback_home_star(0xFFFFFFFFu, false, {0u, 0xFFFFFFFFu, 9093181u, 9351184u}) == 9093181u,
          "unset mHomeStarID falls back to first valid owned star");
    CHECK(pick_color_fallback_home_star(9093181u, false, {9351184u}) == 9351184u,
          "dangling mHomeStarID falls back to owned star");
    CHECK(pick_color_fallback_home_star(0xFFFFFFFFu, false, {}) == 0xFFFFFFFFu,
          "no owned stars yields 0xFFFFFFFF (palette fallback)");

    // Palette fallback stays within Spore's 12 valid palette IDs (0x53DBCF3..0x53DBCFE)
    for (uint32_t i = 0; i < 24; i++) {
        uint32_t pal = fallback_empire_palette_id(0x80000000u + i);
        CHECK(pal >= kEmpirePaletteBase && pal < kEmpirePaletteBase + kEmpirePaletteCount,
              "fallback palette id in Spore 12-color range");
    }
}

static void test_r7_hp_floor_and_cinematic_guards()
{
    printf("space_rivals: R7 DestroyCity HP floor and intro cinematic guards (BUG-1582)\n");
    using namespace spacerivals;

    // HP floor keeps protected homeworld cities above 0 HP so DestroyCity never loops forever
    CHECK(std::fabs(r7_city_hp_floor(1000.0f) - 200.0f) < 0.01f, "20% floor for 1000 max HP");
    CHECK(std::fabs(r7_city_hp_floor(500.0f) - 100.0f) < 0.01f, "100 HP floor for 500 max HP");
    CHECK(std::fabs(r7_city_hp_floor(200.0f) - 100.0f) < 0.01f, "min 100 HP floor for 200 max HP");
    CHECK(std::fabs(r7_city_hp_floor(60.0f) - 60.0f) < 0.01f, "floor never exceeds max HP for small buildings");
    CHECK(std::fabs(r7_city_hp_floor(0.0f) - kR7CityHpFloorMin) < 0.01f, "zero max HP falls back to 100 HP");
    CHECK(std::fabs(r7_city_hp_floor(-10.0f) - kR7CityHpFloorMin) < 0.01f, "negative max HP falls back to 100 HP");
    CHECK(std::fabs(r7_city_hp_floor(NAN) - kR7CityHpFloorMin) < 0.01f, "NaN max HP falls back to 100 HP");

    // Star transfer / CaptureSystem guards
    CHECK(!r7_should_block_star_transfer(true, false, true, 0x80000001u, 0x80000002u),
          "intro cinematic suppresses false-positive star transfer block");
    CHECK(!r7_should_block_star_transfer(true, false, false, 0x80000001u, 0x80000001u),
          "idempotent same-owner refresh is allowed");
    CHECK(!r7_should_block_star_transfer(true, true, false, 0x80000001u, 0x80000002u),
          "emigrating_guard bypasses star transfer block");
    CHECK(r7_should_block_star_transfer(true, false, false, 0x80000001u, 0x80000002u),
          "hostile empire takeover on protected home star is blocked");
    CHECK(r7_should_block_star_transfer(true, false, false, 0x80000001u, 0xFFFFFFFFu),
          "clearing protected home star to unowned is blocked");
    CHECK(!r7_should_block_star_transfer(false, false, false, 0x80000001u, 0x80000002u),
          "unprotected star transfer allowed");

    // City capture progress guards
    CHECK(!r7_should_block_city_capture(true, true, 0x80000001u, 0x80000002u, 0.5f),
          "cinematic suppresses city capture block");
    CHECK(!r7_should_block_city_capture(true, false, 0x80000001u, 0x80000001u, 0.5f),
          "self-pol capture progress is allowed");
    CHECK(!r7_should_block_city_capture(true, false, 0x80000001u, 0x80000002u, 0.0f),
          "zero capture progress is allowed");
    CHECK(!r7_should_block_city_capture(true, false, 0x80000001u, 0xFFFFFFFFu, 0.5f),
          "clear-captor sentinel 0xFFFFFFFF is allowed");
    CHECK(r7_should_block_city_capture(true, false, 0x80000001u, 0x80000002u, 0.25f),
          "rival capture progress on protected homeworld city is blocked");

    // Notice suppression
    CHECK(!r7_should_emit_notice(true, true), "no R7 notice during cinematic");
    CHECK(!r7_should_emit_notice(false, false), "no R7 notice without rival attacker");
    CHECK(r7_should_emit_notice(false, true), "R7 notice emitted on genuine rival attack");
}

static void test_city_surface_orientation_and_civ_cleanup()
{
    printf("space_rivals: spherical city orientation and Civ->Space homeworld cleanup (BUG-1582)\n");
    using namespace spacerivals;

    // Equatorial city positions used by sanitize_city_pose / ensure_city_count
    for (size_t idx = 0; idx < 6; idx++) {
        float angle = (float)idx * 2.0943951f;
        float lat = (idx % 2 == 0) ? 0.35f : -0.35f;
        float rad = 160.0f;
        float x = rad * std::cos(lat) * std::cos(angle);
        float y = rad * std::cos(lat) * std::sin(angle);
        float z = rad * std::sin(lat);

        // Old buggy identity quaternion is tilted (~70 deg) and must be flagged as NOT upright
        CHECK(!city_quat_upright(x, y, z, 0.0f, 0.0f, 0.0f, 1.0f),
              "identity quaternion at equatorial latitude is detected as tilted");

        float q[4];
        city_surface_quat(x, y, z, q);
        float qn = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        CHECK(std::fabs(qn - 1.0f) < 1e-4f, "surface quaternion is unit length");
        CHECK(city_quat_upright(x, y, z, q[0], q[1], q[2], q[3]),
              "computed surface quaternion is upright along planet normal");
    }

    // Pole edge cases (+Z and -Z)
    float q_north[4], q_south[4];
    city_surface_quat(0.0f, 0.0f, 160.0f, q_north);
    city_surface_quat(0.0f, 0.0f, -160.0f, q_south);
    CHECK(city_quat_upright(0.0f, 0.0f, 160.0f, q_north[0], q_north[1], q_north[2], q_north[3]),
          "north pole surface quaternion is upright");
    CHECK(city_quat_upright(0.0f, 0.0f, -160.0f, q_south[0], q_south[1], q_south[2], q_south[3]),
          "south pole surface quaternion is upright");
    CHECK(!city_quat_upright(0.0f, 0.0f, -160.0f, 0.0f, 0.0f, 0.0f, 1.0f),
          "identity quaternion at south pole is upside-down");

    // Civ->Space homeworld cleanup predicates
    constexpr uint32_t kHostPol = 0x10000001u;
    constexpr uint32_t kGuestPol = 0x10000002u;
    CHECK(!should_cleanup_civ_vehicle(kHostPol, kHostPol), "host own vehicle kept");
    CHECK(should_cleanup_civ_vehicle(kGuestPol, kHostPol), "foreign Civ tank swept");
    CHECK(should_cleanup_civ_vehicle(0xFFFFFFFFu, kHostPol), "rogue/unowned Civ vehicle swept");
    CHECK(!should_cleanup_civ_vehicle(kGuestPol, 0u), "invalid host pol never sweeps");

    CHECK(!should_unify_civ_city(kHostPol, kHostPol), "host own city already unified");
    CHECK(should_unify_civ_city(kGuestPol, kHostPol), "rival Civ city unified to host");
    CHECK(!should_unify_civ_city(0xFFFFFFFFu, kHostPol), "unowned sentinel not unified");
    CHECK(!should_unify_civ_city(kGuestPol, 0xFFFFFFFFu), "invalid host pol never unifies");
}

static void test_emigration_starter_and_colony_unlock()
{
    printf("space_rivals: emigrated guest tutorial/starter/Colony Pack unlock (BUG-1582)\n");
    using namespace spacerivals;

    // Host never gets emigration unlocks
    auto host_p = emigration_unlock_plan(true, true, true, true, false, true, true,
                                         false, false, false, false, false,
                                         false, false, false, false, false, false, false, false);
    CHECK(!host_p.any(), "host gets no emigration unlock");

    // Pre-emigration guest gets nothing
    auto pre_p = emigration_unlock_plan(true, true, false, false, false, true, true,
                                        false, false, false, false, false,
                                        false, false, false, false, false, false, false, false);
    CHECK(!pre_p.any(), "guest before home creation gets no emigration unlock");

    // Fresh emigrated guest gets travel flags, drives, Flight School tools, and 1x Colony Pack
    auto fresh_p = emigration_unlock_plan(true, true, false, true, false, true, true,
                                          false, false, false, false, false,
                                          false, false, false, false, false, false, false, false);
    CHECK(fresh_p.set_first_comm && fresh_p.set_flight_school && fresh_p.set_mission201,
          "emigrated guest unlocks tutorial travel flags");
    CHECK(fresh_p.grant_interplanetary && fresh_p.grant_interstellar && fresh_p.grant_scan && fresh_p.grant_laser &&
              fresh_p.grant_abduct && fresh_p.grant_drop_cargo && fresh_p.grant_health && fresh_p.grant_energy &&
              fresh_p.grant_place_colony,
          "emigrated guest gets starter drives, tools, and Colony Pack");

    // Once Colony Pack was granted (and consumed), it is NOT re-granted endlessly
    auto after_pack = emigration_unlock_plan(true, true, false, true, false, true, true,
                                             true, true, true, true, true,
                                             true, true, true, true, true, true, false, true);
    CHECK(!after_pack.any(), "consumed Colony Pack is not re-granted after initial grant");

    // While hold_grants is true (kept cargo restore), flags unlock immediately while tool grants wait
    auto held_p = emigration_unlock_plan(true, true, false, true, true, true, true,
                                         false, false, false, false, false,
                                         false, false, false, false, false, false, false, false);
    CHECK(held_p.any_flag() && !held_p.any_tool(), "hold_grants defers tool grants but allows flags");
}

int main()
{
    test_r5_emigration_filtering_and_scoring();
    test_r5_notice_and_store();
    test_r7_homeworld_protection();
    test_npc_influence_and_progress_and_finale();
    test_civ_to_space_liftoff_state_machine();
    test_space_influence_scaling_and_draft_payout();
    test_space_npc_ui_list();
    test_relinquish_clean_and_color_fallback();
    test_r7_hp_floor_and_cinematic_guards();
    test_city_surface_orientation_and_civ_cleanup();
    test_emigration_starter_and_colony_unlock();
    printf("space_rivals_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
