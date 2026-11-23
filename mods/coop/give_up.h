// WP-R1 of 0.7.17 (BUG-1472; from PR #214's give_up.h by IltonBJSilva): a downed player can give up instead of waiting
// for a partner: holding G for one second (a hold, so a stray G does nothing) with the game window in front. The result
// is the same as its window running out (revive.cpp).
#pragma once

#include <windows.h>
#undef near // windef.h's empty 16-bit leftovers: code including this names variables `near` / `far`
#undef far

#include "sporekit.h"

namespace give_up {

constexpr double kHoldSeconds = 1.0;

// true once per hold, after G was held kHoldSeconds
inline bool held(const SkHost* H, double now)
{
    static double since = -1;
    static bool fired = false;
    HWND gw = H && H->game_window ? (HWND)H->game_window() : nullptr;
    const bool down = gw && GetForegroundWindow() == gw && (GetAsyncKeyState('G') & 0x8000) != 0;
    if (!down) {
        since = -1;
        fired = false;
        return false;
    }
    if (since < 0)
        since = now;
    if (fired || now - since < kHoldSeconds)
        return false;
    fired = true;
    return true;
}

} // namespace give_up
