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

int main()
{
    test_r5_emigration_filtering_and_scoring();
    test_r5_notice_and_store();
    test_r7_homeworld_protection();
    test_npc_influence_and_progress_and_finale();
    printf("space_rivals_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
