// The host's wait before it sends its world after an evolution (BUG-0019, BUG-0232), kept apart
// from the game so `make test` can check it (tests/npc_tests.cpp).
//
// A share waits until `due_at` (3 s after the editor: the evolution cinematic starts first) and
// while a cinematic plays, at most `limit` seconds counted from `since`. `since` is per guest
// (Remote::cine_wait_since, reset whenever the guest is marked as needing the world): with one
// global timer the waits of several guests added up (120 / 240 / 360 s) and a timer left over
// from a guest who left, a trip to the galaxy menu or an old session let the next share go out at
// once during a cinematic (the hatchling world of BUG-0019).
#pragma once

#include <cstdint>

namespace share_wait {

// true: hold the share this frame. `since` 0 = not waiting for a cinematic.
inline bool hold(double& since, bool cinematic, double now, double due_at, double limit = 120.0)
{
    if (now < due_at)
        return true;
    if (cinematic) {
        if (since == 0)
            since = now;
        if (now - since < limit)
            return true;
    }
    since = 0;
    return false;
}

// BUG-1294 (found by the Lineages fork, PR #196, its BUG-9166): a share that waits for a save may go out with another
// share's save in flight (BUG-0112: one save for shares waiting at the same time), but only with one posted when or
// after this share was asked for: an older save lacks what changed since (the reason of the share). `fresh` false: any
// save in flight (before the fix). Ticks of GetTickCount64.
enum class Merge { None, Join, Wait };
inline Merge merge(bool fresh, uint64_t asked, bool other_in_flight, uint64_t other_posted)
{
    if (!other_in_flight)
        return Merge::None;
    if (!fresh || other_posted >= asked)
        return Merge::Join;
    return Merge::Wait; // its own save once that one is written (never two saves at once, BUG-0112)
}

} // namespace share_wait
