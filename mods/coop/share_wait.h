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

} // namespace share_wait
