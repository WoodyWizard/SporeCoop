// The party evolves at the single-player pace (BUG-0510, player reports 2026-11-03/04: "3-4x too
// much DNA" in Cell and Creature). The party's DNA (Creature) and food (Cell, the cell editor's DNA)
// are one shared total, and every player's kills, meals and friends fed it whole: a party of N grew
// N times faster than the game is tuned for. Now every GAIN that enters the shared total is scaled by
// 1/N, N = the players playing the host's world now (the host and the guests in the host's stage and
// world, min 1); spends are never scaled. Each gain is scaled once, on the machine whose game made it
// (the host for its own game, a guest for its own forwarded gain or meal, with the divisor the host
// announced), so nothing is scaled twice. Rewards the party gets once (a species goal card) are not
// scaled: they do not come N times faster. coop.ini [coop] dna_pace=1 (0: the full sum, as before
// 0.7.4) on the host decides for the party; `coop.fix core.dna_pace off` is the test switch.
// Portable (no game calls): tests/validate_tests.cpp.
#pragma once

#include <algorithm>
#include <cmath>

namespace dna_pace {

// tests: the debug "give N" commands (coop.dna add, coop.pool food) give exactly N (setups of other
// tests); `coop.dna gain` / `coop.pool meal` are the paced game reward
inline bool g_bypass = false;
// coop.ini [coop] dna_pace (the host's: 1 the party's pace, 0 the full sum); `coop.dna pace 0|1`
inline int g_ini = 1;

// the divisor the host announces: N when the pace is on, 1 for the full sum
inline int divisor(bool on, int playing) { return on ? std::clamp(playing, 1, 255) : 1; }

// the share of a gain that enters the shared total; a spend (<= 0) stays whole
inline float share(float amount, int div) { return amount > 0 && div > 1 ? amount / (float)div : amount; }

// integer gains (Cell food points): the remainder is kept for the next one, so N meals of 1 point
// give the shared total 1 point, not 0
struct Carry {
    float rest = 0;
    int take(int amount, int div)
    {
        if (amount <= 0 || div <= 1)
            return amount;
        rest += (float)amount / (float)div;
        int n = (int)std::floor(rest + 1e-4f);
        rest = std::max(0.f, rest - (float)n);
        return n;
    }
    void reset() { rest = 0; }
};

} // namespace dna_pace
