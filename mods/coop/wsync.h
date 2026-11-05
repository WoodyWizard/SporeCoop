// Host-owned world state (docs/plan-0.7.7.md W1, class BUG-0419; docs/sync-architecture-plan.md#sync-wsync).
//
// Scalars and small tables of the world that are not objects: the time of day, the day's length, how
// every village feels about the player tribe, the stages done, the current goal. Before, each was either
// not shared (each game kept its own: BUG-0565, BUG-0577) or one ad-hoc message applied once
// (MSG_WORLD_TIME: lost when the guest's game changed its sky after a transition, BUG-0579).
//
// One table of rows. A row says in which stages it is in play, how the host's game is read (a small table
// of {u32 key, up to 6 u32 values}) and how a guest's game is brought to the host's values through the
// game's own setters (so the HUD, the AI and the visuals follow). The engine is stage-free:
//   * the host reads every row of its stage and sends the whole table every second and at once when a
//     row changed (MSG_WSTATE, unreliable: the next send repairs a lost one);
//   * a guest keeps the newest table per row and, every half second while it plays the host's world
//     (the message gate drops it while elsewhere, coop.cpp world_state_msg) and the host's stage, compares
//     its own game's values with it and applies the rows that differ. This is the re-alignment after
//     every transition: a load, a rejoin, a death, the editor, the host's game over and reload all leave
//     the guest's game with other values, the next check applies the host's again.
//
// Wire (MSG_WSTATE = 62, host -> guest): u8 kind 0, u32 host mode, u8 rows, rows x {u8 row id, u8 nv,
// u8 n, n x {u32 key, nv x u32}}. Older SporeCoop ignores the type.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "net/session.h"
#include "sporekit.h"

namespace wsync {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<bool()> playing; // the local player has its entity in the world (not loading, not an editor)
};

void init(const SkHost* host, SkMod* mod, const Ctx& ctx);
void tick(double now); // every frame
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();
void on_player_join(); // host: the newcomer gets every row at once
// a guest with the time-of-day row on takes the host's sky from MSG_WSTATE, not MSG_WORLD_TIME
bool owns_time_of_day();
std::string status();

} // namespace wsync
