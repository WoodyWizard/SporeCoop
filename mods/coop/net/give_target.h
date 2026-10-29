// The co-op window's gift recipient (BUG-0075): with 3 or 4 players the Give button always went
// to the first other player (a guest could only give to the host). The player picks one of the
// other present players (the button next to Give cycles through them); a pick that left falls
// back to the first other player. Portable: `make test` checks it (tests/validate_tests.cpp).
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace net {

constexpr uint8_t kNoGiveTarget = 0xFF;

// players: the ids of the session (including me), any order. fix false: the pick is ignored (the
// behaviour before BUG-0075, the first other player in the session's order)
inline uint8_t give_target(std::vector<uint8_t> players, uint8_t me, uint8_t pick, bool fix = true)
{
    if (fix)
        std::sort(players.begin(), players.end());
    if (fix && pick != me && std::find(players.begin(), players.end(), pick) != players.end())
        return pick;
    for (uint8_t p : players)
        if (p != me)
            return p;
    return kNoGiveTarget;
}

// the next other player after the current target (wraps around); kNoGiveTarget when alone
inline uint8_t give_target_next(std::vector<uint8_t> players, uint8_t me, uint8_t pick)
{
    std::sort(players.begin(), players.end());
    uint8_t cur = give_target(players, me, pick);
    if (cur == kNoGiveTarget)
        return kNoGiveTarget;
    auto it = std::find(players.begin(), players.end(), cur);
    for (size_t i = 1; i <= players.size(); i++) {
        uint8_t p = players[(size_t)(it - players.begin() + i) % players.size()];
        if (p != me)
            return p;
    }
    return cur;
}

// the pick after player `id` left (BUG-0234): a pick of the leaving player is cleared, so the next
// player who gets that id is not the recipient. fix false: kept (0.7.3.0)
inline uint8_t give_pick_after_leave(uint8_t pick, uint8_t id, bool fix = true)
{
    return fix && pick == id ? kNoGiveTarget : pick;
}

} // namespace net
