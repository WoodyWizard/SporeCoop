#pragma once

#include <cstdint>

namespace world {

struct AutosaveClock {
    static constexpr uint64_t interval_ms = 600000;
    uint64_t since = 0;
    bool event_pending = false;

    bool due(uint64_t now) const { return event_pending || (since && now - since >= interval_ms); }

    void saved(uint64_t now) { since = now; }
};

} // namespace world
