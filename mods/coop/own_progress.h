// Own progress, the game side (WP-P1 / P2 of 0.7.17, BUG-1469): the rules and the formats are in own_progress_logic.h,
// the design in docs/03-gameplay.md#gameplay-own-progress and docs/01-architecture.md#arch-player-store.
//
// all.shared_dna 2: every player's DNA and evolution bar are its own in Cell and Creature; the shared-total code of
// dna.cpp, cellsync, social and wsync steps aside while active(). The host keeps every guest's values (live reports and
// the store record `progress`) and gives them back after a load of its world; the stage moves on when every bar is
// full (the gates in vote.cpp ask hold_stage_end()).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "net/buffer.h"
#include "net/session.h"
#include "own_progress_logic.h"
#include "sporekit.h"

namespace own_progress {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed; // this player's notification line
    std::function<bool()> host_creature_mode;     // creature.mode host: one creature, one progress (the host's)
    std::function<bool()> vote_active;            // an evolution / switch vote runs
    std::function<void(bool at_end)> resume_end;  // host, Cell: run the kept click now (at_end: History's check)
    std::function<uint32_t()> world;              // the world of the stage played (an editor keeps it)
};

void init(const SkHost* host, SkMod* mod, const Ctx& ctx);
void tick(double now); // every frame (main thread)
void on_message(uint8_t from, net::Reader& r); // MSG_PROGRESS
void on_player_join(uint8_t id);  // host: a newcomer (session_glue)
void on_player_leave(uint8_t id); // a player left

// this game plays own progress now (the host: the setting, the fix, every peer reads it; a guest: the host's word)
bool active();
// the session setting as played (own_progress_logic.h Mode): value 2 is read as own wallet when off or refused
int dna_mode();
// this game's own values now (false: not in Cell / Creature with a cell or an avatar)
bool local_values(Values& out);
// every player of the host's world is full in the stage played now; `waiting` names the others
bool all_full(std::string* waiting = nullptr);

// cell.end_click: the Cell gate sits before the game's end sequence (on) or at the switch (off: 0.7.15's white screen)
bool end_click_fix();
// host: a stage-end click now (Cell: History's check, `at_end` before the end sequence; Creature: the stage-end request).
// True: held (told; Cell keeps the click and runs it by itself once every bar is full). False: go on (all full, or a
// second click within 10 s: without the others)
bool hold_stage_end(bool cell, bool at_end);
// guest: its own stage-end click in the host's world while no vote runs: told who it waits for
void tell_wait(bool cell);

// host: a gain its game paid for a guest's deed (the hunt credit, the Actor scope, a social win): sent to that guest
void give_gain(uint8_t player, float dna, uint8_t cause);
// guest: a gain the host's game paid for our deed (kGain, a social result)
void take_gain(float dna, const char* why);

// Cell (cell.level_together): an own meal of `amount` at food `cur`; the part the game takes now (the rest is banked)
int clip_meal(int amount, int cur);

std::string status(); // coop.progress
void resync();        // tests: a guest asks the host for its values again

} // namespace own_progress
