// Native tests of party_difficulty_logic.h (WP-D1 of 0.7.17, BUG-1473): the percentage of all.party_difficulty (the
// fixed values and auto by the players), the scaling of a hit by its sides, the hits to kill, and the setting's row
// (make test, under ASan / UBSan).
#include <cmath>
#include <cstdio>
#include <string>

#include "party_difficulty_logic.h"
#include "settings_core.h"

static int g_fail = 0;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                             \
        }                                                         \
    } while (0)

using namespace party;

static bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

static void percents()
{
    for (int n = 1; n <= 4; n++) {
        CHECK(percent(k100, n) == 100);
        CHECK(percent(k125, n) == 125);
        CHECK(percent(k150, n) == 150);
        CHECK(percent(k200, n) == 200);
    }
    // auto: +25 % per player over two (owner question 5's alternative)
    CHECK(percent(kAuto, 1) == 100 && percent(kAuto, 2) == 100);
    CHECK(percent(kAuto, 3) == 125 && percent(kAuto, 4) == 150);
    CHECK(percent(kAuto, 8) == kMaxPercent); // (more players than the session takes: capped)
    CHECK(percent(-1, 3) == 100 && percent(9, 3) == 100); // an unknown value: today's balance
}

static void scaling()
{
    // 150 %: an NPC takes 1.5 times the hits, its hits deal 1.5 times
    CHECK(near(scale(30.f, Side::Npc, Side::Player, 150), 20.f));
    CHECK(near(scale(10.f, Side::Player, Side::Npc, 150), 15.f));
    // the other hits stay the game's
    CHECK(scale(10.f, Side::Npc, Side::Npc, 150) == 10.f);
    CHECK(scale(10.f, Side::Player, Side::Player, 150) == 10.f); // friendly fire, the Rivals' fights
    CHECK(scale(10.f, Side::Player, Side::Other, 150) == 10.f);  // no attacker: a fall, the sea
    CHECK(scale(10.f, Side::Other, Side::Player, 200) == 10.f);
    // 100 % (and the switch off, which reads 100) is exactly the game's damage
    for (float d : {0.1f, 5.f, 1e6f})
        CHECK(scale(d, Side::Npc, Side::Player, 100) == d && scale(d, Side::Player, Side::Npc, 100) == d);
    // nothing odd passes through changed
    CHECK(scale(0.f, Side::Npc, Side::Player, 200) == 0.f);
    CHECK(scale(-3.f, Side::Player, Side::Npc, 200) == -3.f);
    CHECK(std::isnan(scale(NAN, Side::Player, Side::Npc, 200)));
    CHECK(scale(10.f, Side::Player, Side::Npc, 0) == 10.f);
    // the two directions are inverse: a player's hit and an NPC's answer at the same setting
    CHECK(near(scale(scale(12.f, Side::Npc, Side::Player, 125), Side::Player, Side::Npc, 125), 12.f));
}

static void kills()
{
    CHECK(hits_to_kill(100.f, 25.f, 100) == 4);
    CHECK(hits_to_kill(100.f, 25.f, 150) == 6);
    CHECK(hits_to_kill(100.f, 25.f, 200) == 8);
    CHECK(hits_to_kill(100.f, 30.f, 125) == 5); // 24 per hit
    CHECK(hits_to_kill(0.f, 25.f, 150) == 0 && hits_to_kill(100.f, 0.f, 150) == 0);
}

static void setting_row()
{
    using namespace settings;
    const Def& d = kDefs[kPartyDifficulty];
    CHECK(std::string(d.key) == "all.party_difficulty" && std::string(d.group) == "All stages");
    CHECK(d.scope == Scope::Session && d.min == 0 && d.max == kAuto && !d.ini && !d.off_in_rivals);
    CHECK(d.def == kPartyDifficultyDefault && kPartyDifficultyDefault == kDefaultChoice);
    CHECK(kDefaultChoice == k100); // provisional (owner question 5): flipping it to kAuto is the one-line change
    CHECK(value_text(kPartyDifficulty, k100) == "100 %" && value_text(kPartyDifficulty, k150) == "150 %" &&
          value_text(kPartyDifficulty, kAuto) == "auto");
    int v = -1;
    CHECK(parse_value(kPartyDifficulty, "auto", v) && v == kAuto);
    CHECK(parse_value(kPartyDifficulty, "150_%", v) && v == k150);
    CHECK(parse_value(kPartyDifficulty, "2", v) && v == k150);
    CHECK(!parse_value(kPartyDifficulty, "150", v)); // a percentage is not a value: the names or 0..4
    CHECK(parse_value(kPartyDifficulty, "default", v) && v == kPartyDifficultyDefault);
    // both possible defaults give today's balance with two players
    CHECK(percent(k100, 2) == 100 && percent(kAuto, 2) == 100);
}

int main()
{
    percents();
    scaling();
    kills();
    setting_row();
    printf("party_tests: %s (%d failures)\n", g_fail ? "FAIL" : "ok", g_fail);
    return g_fail ? 1 : 0;
}
