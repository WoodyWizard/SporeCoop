// BUG-9118: a downed player can give up instead of waiting for a partner: holding G for one second
// (a hold, so a stray G does nothing) with the game window in front. Every stage's downed state
// (Cell, Creature, Adventure, Tribe, Space) asks this while its own player is down; the result is the
// same as its timer running out.
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
