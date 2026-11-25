// Every player's own stage history (WP-H1 of docs/plan-0.7.17.md, BUG-1470; BUG-1471 the trait progress credit): the
// game glue of history_logic.h (the rules are there). docs/re/creations.md#creations-own-history,
// docs/01-architecture.md#arch-player-store (record `history`), docs/04-netcode.md (MSG_PLAYER_HISTORY 75).
//
// * Host: the hook on cPlayer::AddTraitProgress 0xC75840 credits a guest's kill / social win (an actor::Scope naming
//   the guest, opened by npc.cpp around its hit and by social.cpp around its social win) to the guest's record instead
//   of the host's cPlayer; every 0.5 s the guests' records follow the host's cards (history::update) and go to each
//   guest whose mod reads them (session 1.3); the records live in the per-player store beside the host's save.
// * Guest: its record is written into its own cPlayer while it plays the host's world (the game's setter), the wsync
//   row "stages" leaves it alone (guest_owns()); in Cell its game reports its own Cell card; in Space its empire gets
//   its own archetype (SpaceGame_SetArchetype, once per world).
// Switches: core.own_history (BUG-1470, off: the shared history of 0.7.16.1), core.trait_credit (BUG-1471, off: a
// guest's kill raises the host's progress, a social win nobody's), space.own_archetype (BUG-1470, off: the guest's
// empire keeps the host's archetype).
#pragma once

#include <cstdint>

#include "net/buffer.h"
#include "net/session.h"
#include "sporekit.h"

namespace history {

void init(const SkHost* host, SkMod* mod, net::Session* sess);
void tick(double now);                       // every frame, main thread
void on_message(uint8_t from, net::Reader& r); // MSG_PLAYER_HISTORY
// guest: its own history is in force (the wsync row "stages" leaves this game's cPlayer alone)
bool guest_owns();
// host: a guest's won social game (social.cpp's replica of the game's social win 0xD9BC40, which adds the creature's
// DNA worth to the winner's social progress at 0xD9BD36): the same credit, through the game's AddTraitProgress inside
// an actor::Scope naming the guest
void social_win(uint8_t player, float worth);

// BUG-1557: Rivals per-player Tribe/Civ consequence tracking and Space philosophy (host only)
bool rivals_own_history_enabled();
void record_tribe_outcome(uint8_t player, bool allied);
void set_civ_start_specialty(uint8_t player, uint8_t spec_1_based); // 1=military, 2=economic, 3=religious
void record_civ_outcome(uint8_t player, uint8_t spec_1_based);      // 1=military, 2=economic, 3=religious
uint32_t player_card(uint8_t player, int stage);                    // player 0: live cPlayer card; guest: recorded card
bool player_has_own_tribe(uint8_t player);
void host_sync_now();

} // namespace history
