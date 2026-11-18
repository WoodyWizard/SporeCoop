// BUG-9119: the pure part of the progress debt (host unit tests: tests/progress_debt_tests.cpp)
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace progress_debt {

// What a death costs (owner, 2026-11-11: progression debt, not DNA): 10% of the progress earned in this
// stage so far (Creature: cPlayer +0x10F0, the bar's DNA earned; Cell: the food), whole points, at least
// 1 once there is any. Spendable DNA (the editors') never changes.
inline float progress_cost(float done)
{
    if (!std::isfinite(done) || done < 1)
        return 0;
    return std::max(1.f, std::round(done * 0.10f));
}

// Pure ledger, one stage and world at a time (host tests use it directly)
struct Ledger {
    uint32_t mode = 0, world = 0;
    float owed = 0;
    // a new place clears what was owed in the old one
    bool place(uint32_t m, uint32_t w)
    {
        if (m == mode && w == world) return false;
        mode = m; world = w; owed = 0;
        return true;
    }
    void add(float amount) { if (amount > 0) owed += amount; }
    // a gain repays the debt first; what is left of it goes to the party
    float absorb(float gain)
    {
        if (gain <= 0 || owed <= 0) return gain;
        float repaid = gain < owed ? gain : owed;
        owed -= repaid;
        if (owed < 0.001f) owed = 0;
        return gain - repaid;
    }
};

// Tribe: where step k (0-based) of `total` lies on the bar's sections (x1s[i]..x2s[i]; x2 <= x1: missing):
// each section holds steps in proportion to its width (Tribe: 1, 2 and 2 of 5), split evenly inside it
inline bool step_slot(const float* x1s, const float* x2s, int sections, int k, float total, float& x1, float& x2)
{
    float width = 0;
    for (int i = 0; i < sections; ++i)
        width += x2s[i] > x1s[i] ? x2s[i] - x1s[i] : 0.f;
    if (width <= 0 || total <= 0 || k < 0)
        return false;
    const float per = width / total;
    int first = 0;
    for (int i = 0; i < sections; ++i) {
        const float w = x2s[i] - x1s[i];
        if (w <= 0)
            continue;
        const int n = std::max(1, (int)std::lround(w / per));
        if (k < first + n) {
            x1 = x1s[i] + w * (k - first) / n;
            x2 = x1s[i] + w * (k - first + 1) / n;
            return true;
        }
        first += n;
    }
    return false;
}

} // namespace progress_debt
