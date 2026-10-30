// "Is the host's world the one I play?" (BUG-0231, GitHub #165): the one rule every module asks
// before it applies anything of the host's world on a guest, or sends anything of its own world to
// the host. Kept apart from the game so `make test` can check it (tests/validate_tests.cpp).
//
// The perspective of one machine: each guest classifies the host's latest state against its own
// game (a 3-4 player session can have one guest elsewhere and another in the host's world). The
// same question is the base of the per-player perspective of a rivals session (docs/11-rivals.md
// 11.8): "whose world is this machine in" is answered here, the modules decide what to share.
//
// World identity is the CRC of the save name (`CurrentGameName`). Two different planets saved
// under the same name count as one world: a known limit, fixing it would change the protocol.
//
// Callers (coop.cpp `host_world()`, `guest_elsewhere()`, `host_absent_for()`):
//   * the message gate (world_state_msg): the host's Cell / Creature / Tribe / Civ state is dropped
//     while Other;
//   * the npc, tribe and civ contexts: their "host away" is true while Other (host_absent_for);
//   * the DNA routes (dna_route.h rows 2 and 6);
//   * the shared resources: held while Other (shared.cpp, res.elsewhere).
#pragma once

#include <cstdint>

namespace host_world {

// the game mode ids (modes.h; static_assert there keeps them equal)
constexpr uint32_t kCell = 0x1654C00, kCreature = 0x1654C01, kTribe = 0x1654C02, kCiv = 0x1654C04;

enum class HostWorld {
    NotGated, // not a connected guest, a stage that decides per place (Space, adventures, menus,
              // editors, loads), or the host plays another stage (follow_phase pulls the guest)
    Same,     // the host's world is the one this guest plays
    Other,    // this guest plays another world of the host's stage (or cannot tell yet)
};

// the stages whose world is one save: Space (star systems) and adventures decide per place
// (squadron with_host, spacecoop my_place), they are never gated here
inline bool gated(uint32_t mode) { return mode == kCell || mode == kCreature || mode == kTribe || mode == kCiv; }

// my_mode / my_world: this frame's g_mode / g_world (save CRC; 0 = unknown);
// has_state / st_mode / st_world: the host's latest PlayerState, if any;
// welcome_world: Welcome::world of the host (0 = unknown or a 0.7.3.0 or older host).
// Recomputed on every call, never cached. Semantics callers rely on:
//   * NotGated when not a connected guest, when my_mode is not Cell, Creature, Tribe or Civ, or
//     when the host's state is in another mode;
//   * Same when the host's state (before the first one: welcome_world) names my_world != 0;
//   * Other otherwise in a gated stage: no state and no welcome world (any older host: one round
//     trip after a join), and my_world == 0.
inline HostWorld classify(bool connected_guest, uint32_t my_mode, uint32_t my_world, bool has_state, uint32_t st_mode,
                          uint32_t st_world, uint32_t welcome_world)
{
    if (!connected_guest || !gated(my_mode))
        return HostWorld::NotGated;
    if (has_state && st_mode != my_mode)
        return HostWorld::NotGated;
    uint32_t host = has_state ? st_world : welcome_world;
    return my_world != 0 && host == my_world ? HostWorld::Same : HostWorld::Other;
}

// the world id the host announces in WELCOME: its save in a gated stage, else 0 (unknown)
inline uint32_t welcome_world(uint32_t host_mode, uint32_t host_world) { return gated(host_mode) ? host_world : 0; }

} // namespace host_world
