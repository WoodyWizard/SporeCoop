// BUG-1336 (vote.cpp): whether the host's first Creature editor vote (the arrival vote after Cell, BUG-0315) waits
// for a guest still on its way to the host's Creature world. Pure, for core_tests.
#pragma once

namespace arrival_rule {

constexpr double kSendWait = 8.0;  // s of the vote a share not out yet (or still being sent) is waited for
constexpr double kLoadWait = 40.0; // s after the share was asked for that a guest with its world (loading) is waited for

// in_editor: the guest is in its own editor (BUG-1239: not waited for); in_flight: its share waits for the host's save,
// is being sent or not acknowledged yet; vote_age: s since the vote started; since_share: s since the share was asked
// for (a large number: none)
inline bool waits_for(bool in_editor, bool in_flight, double vote_age, double since_share)
{
    if (in_editor)
        return false;
    return in_flight ? vote_age < kSendWait : since_share < kLoadWait;
}

} // namespace arrival_rule
