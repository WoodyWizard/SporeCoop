// Every player's own start in a new Cell game (BUG-9273, the owner's rule of 2026-11-20: "every player starts together
// on the same planet with their own creature and chooses for themselves its diet, name and colour; the host alone
// names the planet"). The portable rules and formats: start_choice_logic.h; docs/01-architecture.md#architecture-start-choice.
//
//   * The host's own choice is the game's Cell wizard (unchanged). A guest seated in the host's Cell world with no
//     choice recorded for this planet is asked (MSG_START_CHOICE kAsk): the co-op window opens on its "Your own cell"
//     page (coop_ui.cpp): the diet (herbivore, carnivore, omnivore), the creature's name, the colour (the player
//     colours of F2, find.h).
//   * The answer becomes the guest's own cell (creations::choose of the diet's game cell: shared to every game as its
//     creation, its puppet on the host has that model and diet), its colour (pfind::request_color: name tag, arrows,
//     minimap, chat) and a fresh start of its own food (own progress, BUG-9265). The host records it by the player's
//     identity next to the planet (<planet>.spo.duostart) and sends every guest the table of the choices: the
//     partners' name tags carry the creature's name, and a rejoining guest plays its choice again without a question.
//   * The diet's Cell card (cards.cpp) is the seat's card when the guest's own game reported none.
//   * The session setting cell.start_choice (default on); off, or creature.mode host: nobody is asked (0.7.15: a guest
//     plays the host's cell or picks one of its own creations in F9).
#pragma once

#include <cstdint>
#include <string>

#include "net/buffer.h"
#include "net/session.h"
#include "sporekit.h"

namespace start_choice {

void init(const SkHost* host, SkMod* mod, net::Session* sess);
void tick(double now);
void on_message(uint8_t from, net::Reader& r);

// guest: the host asked and nothing was answered yet (the co-op window shows the "Your own cell" page)
bool prompt();
// guest: the answer from the window (or `coop.start pick`); false and *why when it is refused (no name, ...)
bool answer(int diet, const std::string& name, int color, std::string* why);
// guest: the name the window starts with
std::string default_name();

// the creature name a player chose for this planet ("" = none), for the name tags
std::string name_of(uint8_t player);
// host: the Cell card of a seat's diet (0 = no choice recorded), cards.cpp's fallback for the Cell card
uint32_t cell_card(uint64_t identity);

bool enabled(); // the setting is on and every player plays their own creature
std::string status();
int command(int argc, const char* const* argv, std::string& out); // coop.start

} // namespace start_choice
