// Lineages adaptation of upstream BUG-0630: budget and earned progress differ.
// A host death penalty still changes both; editor spending only changes budget.
#pragma once
#include <cmath>
#include <limits>
#include "net/buffer.h"

namespace dna_sync {
constexpr uint32_t kProgressTag = 0x31414E44; // DNA1, optional MSG_DNA tail
struct Progress {
    uint32_t world = 0;
    float earned = std::numeric_limits<float>::quiet_NaN();
    bool valid(uint32_t current_world) const
    {
        return world && world == current_world && std::isfinite(earned);
    }
    void write(net::Writer& w) const
    {
        if (!world || !std::isfinite(earned)) return;
        w.u32(kProgressTag); w.u32(world); w.f32(earned);
    }
    static Progress read(net::Reader& r)
    {
        if (r.left() < 12 || r.u32() != kProgressTag) return {};
        Progress p; p.world = r.u32(); p.earned = r.f32();
        return r.ok() ? p : Progress{};
    }
};

// Use the native reward path for earned progress (including death setbacks),
// then the editor's setter for spendable DNA. Repeated totals are idempotent.
template<class Add, class Set>
void apply(float budget, float earned, float current_budget, float current_earned,
           bool enabled, Add add, Set set)
{
    if (!std::isfinite(budget) || budget < 0) return;
    if (enabled && std::isfinite(earned) && std::isfinite(current_earned)) {
        float delta = earned - current_earned;
        if (delta != 0) add(delta);
        set(budget);
    } else {
        float delta = budget - current_budget;
        if (delta != 0) add(delta); // legacy host / diagnostic switch
    }
}
} // namespace dna_sync
