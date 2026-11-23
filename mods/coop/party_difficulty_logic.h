// The difficulty for 3-4 players (WP-D1 of docs/plan-0.7.17.md item 5, BUG-1473): the session setting
// all.party_difficulty scales the fights between players and NPCs on the host, the game that simulates every NPC.
// Portable (no game calls): the rules are unit tested in tests/party_tests.cpp.
//
// Why our hit paths and not the game's difficulty tunings (docs/re/creature.md#creature-difficulty-tunings): the
// game's per-mode difficulty lists (cGameModeManager +0x3C, three per mode) hold AI attitudes, timers and Space's economy
// multipliers, no NPC health or NPC damage key; most readers copy their keys once into globals or objects at a mode's
// start. A factor on the fights themselves acts in every stage at once and at every moment, and leaves the economy alone.
//
//   * a player's hit on an NPC deals damage x 100 / percent (an NPC takes percent / 100 times the hits to fall);
//   * an NPC's hit on a player deals damage x percent / 100;
//   * every other hit (NPC on NPC, player on player, no attacker: a fall, the sea) is the game's own.
#pragma once

#include <algorithm>
#include <cmath>

namespace party {

// the setting's values (all.party_difficulty, settings::kPartyDifficulty): a fixed percentage or "auto"
enum Choice : int { k100 = 0, k125 = 1, k150 = 2, k200 = 3, kAuto = 4 };
constexpr const char* kChoiceNames = "100 %|125 %|150 %|200 %|auto";

// The default of a new world. Owner question 5 of docs/plan-0.7.17.md is open: 100 % (today's balance, proposed) or
// auto (+25 % per player over two). The one line to change: kAuto here (tests/party_tests.cpp checks both values).
constexpr int kDefaultChoice = k100;

constexpr int kAutoStepPercent = 25; // auto: per player over two
constexpr int kMaxPercent = 200;

// the percentage in force: choice as set by the host, players = the session's players, the host counted
inline int percent(int choice, int players)
{
    switch (choice) {
    case k100: return 100;
    case k125: return 125;
    case k150: return 150;
    case k200: return 200;
    case kAuto: return std::min(kMaxPercent, 100 + kAutoStepPercent * std::max(0, players - 2));
    default: return 100; // an unknown value (a newer host's table read by an older rule): today's balance
    }
}

// who is on each side of a hit
enum class Side { Other, Player, Npc };

// the damage dealt with the factor: a player's hit on an NPC is divided, an NPC's hit on a player multiplied
inline float scale(float damage, Side victim, Side attacker, int pct)
{
    if (!std::isfinite(damage) || damage <= 0 || pct == 100 || pct <= 0)
        return damage;
    if (victim == Side::Npc && attacker == Side::Player)
        return damage * 100.f / (float)pct;
    if (victim == Side::Player && attacker == Side::Npc)
        return damage * (float)pct / 100.f;
    return damage;
}

// tests and the status line: how many hits of `damage` an NPC of `health` takes to fall at `pct`
inline int hits_to_kill(float health, float damage, int pct)
{
    float d = scale(damage, Side::Npc, Side::Player, pct);
    if (!(health > 0) || !(d > 0))
        return 0;
    return (int)std::ceil(health / d - 1e-4f);
}

} // namespace party
