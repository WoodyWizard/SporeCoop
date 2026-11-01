// The game is busy: one gate for the actions of the mod that must not start now (BUG-0404, the
// "busy states" class of the ticket audit: BUG-0404, BUG-0331, BUG-0336, BUG-0319).
//
// What makes the game busy (read by coop.cpp game_busy(), every frame, cheap):
//   kCinematic  a cinematic or a caption plays (cCinematicManager +0x2C). An editor opened then came
//               up without its buttons and the cinematic never ended in the editor mode (BUG-0404).
//   kInputLock  the game's input lock (cGameInputManager +0x110 > 0): clicks are dropped.
//   kPaused     the game time stands (cGameTimeManager +0x48 bit 0): the pause menu (0x03), the
//               Sporepedia (0x01), My Collections (0x03), planners. Not only the pause menu's
//               "Resume Game" button (BUG-0331, BUG-0336).
//   kEditor     an editor is open (the stage's world stands).
//   kLoading    a load or a mode switch is under way or queued.
// Each action waits for its own set of reasons (the k*Gate masks) with hold(); what happens when a
// wait runs out is the action's choice (the vote's editor ends the cinematic, a share goes out).
// Portable (no game, no SporeKit): tests/npc_tests.cpp checks it with `make test`.
#pragma once
#include <string>

namespace busy {

enum : unsigned {
    kCinematic = 1u << 0,
    kInputLock = 1u << 1,
    kPaused = 1u << 2,
    kEditor = 1u << 3,
    kLoading = 1u << 4,
};

// an editor opening (the host's after a vote, a guest's own): loading and the editor itself are the
// callers' own checks (they drop or re-decide then)
constexpr unsigned kEditorGate = kCinematic | kInputLock | kPaused;
// a world share after a mode change (BUG-0019: the cinematic; the save itself checks editors and
// loads, coop.cpp save_blocker)
constexpr unsigned kShareGate = kCinematic | kInputLock | kPaused;
// the result of an action vote (a superweapon ...)
constexpr unsigned kActionGate = kCinematic | kInputLock | kPaused | kEditor | kLoading;
// the game's simulation stands: a state compared now is stale (the sync check, the census)
constexpr unsigned kStillGate = kPaused | kEditor | kLoading;

inline unsigned bits(bool cinematic, int input_lock, bool paused, bool editor, bool loading)
{
    return (cinematic ? kCinematic : 0u) | (input_lock > 0 ? kInputLock : 0u) | (paused ? kPaused : 0u) |
           (editor ? kEditor : 0u) | (loading ? kLoading : 0u);
}

// "cinematic,paused" ("-" when not busy): coop.status and the trace (busy=...)
inline std::string names(unsigned b)
{
    static const char* const n[] = {"cinematic", "input_lock", "paused", "editor", "loading"};
    std::string s;
    for (unsigned i = 0; i < 5; i++)
        if (b & (1u << i))
            s += (s.empty() ? "" : ",") + std::string(n[i]);
    return s.empty() ? "-" : s;
}

// One wait at a gate. `b` = the game's busy bits masked with the action's gate. Returns true while
// the action must wait this frame. `since` (0 = not waiting) starts at the first busy frame; the
// reasons in `endless` (a player's pause: the player comes back) never run out, the others do after
// `limit` seconds of waiting; then *expired is set and the action goes on (its own fallback).
inline bool hold(double& since, unsigned b, double now, double limit, unsigned endless = 0, bool* expired = nullptr)
{
    if (expired)
        *expired = false;
    if (!b) {
        since = 0;
        return false;
    }
    if (b & endless)
        return true; // (the limit counts only the time busy for other reasons)
    if (since == 0)
        since = now;
    if (now - since < limit)
        return true;
    if (expired)
        *expired = true;
    return false;
}

} // namespace busy
