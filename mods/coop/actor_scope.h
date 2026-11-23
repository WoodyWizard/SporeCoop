// The Actor scope (WP-0 of docs/plan-0.7.17.md, for WP-H1 and WP-P1 / P2): the player on whose behalf the host's game
// runs the current call.
//
// The host's game does some things for a guest as if they were the host's own: a guest's hit on an NPC lands as the
// host's (npc.cpp), a guest's social win is played on the host's game (social.cpp). The game then credits the host's
// player: cPlayer::AddTraitProgress 0xC75840 (the trait progress behind the consequence cards; today a guest's kills
// raise the host's combat progress) and the DNA / food of the act. Code that lands such an act opens a Scope naming
// the guest around the game call; a hook inside that call asks current() and credits that player instead of the host.
//
// Owners: WP-H1 (feat/w70-history) owns the hook on AddTraitProgress and opens the scopes in npc.cpp and social.cpp;
// WP-P1 / P2 (feat/w69-progress) reads current() for the own-progress credit (a guest's kill and social win). In WP-0
// this is the interface only: nothing opens a scope, current() is always kNone.
//
// Rules:
// * A Scope is opened on the game thread around one game call, never across frames. Scopes nest: the inner one wins,
//   the outer one is back after it.
// * kNone: no scope, the call is the game's own (credited to the local player, as without the mod).
// * A fault inside the game call that the game's own handler catches unwinds past the Scope without its destructor
//   (call_guard.h, BUG-1047): WP-H1 calls reset() once a frame outside every game call (game.cpp reset_call_guards),
//   so a caught fault never leaves a guest credited for the host's own acts.
// Portable (no Windows, no SporeKit): tests/validate_tests.cpp test_actor_scope.
#pragma once

#include <cstdint>

namespace actor {

constexpr uint8_t kNone = 0xFF; // no scope (player ids are the session's: 0 the host, guests 1..)

inline uint8_t g_current = kNone; // the game thread's; only Scope and reset() write it

inline uint8_t current() { return g_current; }
inline bool active() { return g_current != kNone; }

class Scope {
public:
    explicit Scope(uint8_t player) : prev_(g_current) { g_current = player; }
    ~Scope() { g_current = prev_; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    uint8_t prev_;
};

// once a frame outside every game call: a Scope left open by a caught fault ends here (returns true when one was open)
inline bool reset()
{
    bool was = g_current != kNone;
    g_current = kNone;
    return was;
}

} // namespace actor
