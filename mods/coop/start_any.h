// Every player's own start in a new game at any stage after Cell (BUG-9293; the Cell start is start_choice.h). The
// rules and formats: start_any_logic.h; docs/01-architecture.md#architecture-start-any.
//
//   * The host's own start is the game's wizard. A guest seated in the host's Creature / Tribe / Civ / Space world of a
//     planet whose party began in this session (a new game) and with no choice recorded is asked (MSG_START_ANY kAsk):
//     its own creature (one of its My Creations creatures, or keep the one it plays) and its consequence card of every
//     stage before the start (Cell diet, Creature, Tribe, Civ; the Space archetype follows from the four).
//   * The answer: the guest's game takes the creature as its own creation (creations::choose: the party seat's
//     species, so its nest, tribe village and nation are of its creature); the host writes the cards into the guest's
//     party seat as its own history (party::set_history, cards.cpp sends it to the guest's cPlayer: its History, its
//     powers), records it next to the planet (<planet>.spo.duostartany) and answers kTaken.
//   * `coop.fix party.start_any off`: nobody is asked (the guest's seat starts from the host's cards, 0.7.15.95).
#pragma once

#include <cstdint>
#include <string>

#include "net/buffer.h"
#include "net/session.h"
#include "sporekit.h"

namespace start_any {

void init(const SkHost* host, SkMod* mod, net::Session* sess);
void tick(double now);
void on_message(uint8_t from, net::Reader& r);

// guest: asked, not answered (the start stage, 0 = no question open)
int prompt();
// guest: the answer. creature: an instance of one of our My Creations creatures, 0 = keep; cards: option index per
// card stage (start_any_logic.h). false and *why when refused
bool answer(uint32_t creature, const int8_t (&cards)[4], std::string* why);

bool enabled();
// host: the card a seat (by its player identity) chose for stage `stage` (0 Cell .. 3 Civ) at its start on this planet,
// 0 = none (cards.cpp keeps it for a stage the host's world has not done)
uint32_t seat_card(uint64_t identity, int stage);
std::string status();
int command(int argc, const char* const* argv, std::string& out); // coop.startany

} // namespace start_any
