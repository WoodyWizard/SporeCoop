// F2 of 0.7.12 "Find each other" (docs/08-ui.md#ui-find-each-other, BUG-0764): the player colours, the arrows at
// the screen edge to the partners off screen, the partners on the game's minimap. The portable rules are in
// find_core.h.
//
//   * colours: the host keeps the table (find_core.h Colors) and sends it (MSG_COLORS) on every join, leave and
//     change; every name tag, arrow, minimap triangle and chat name of a player is drawn in its colour. A player
//     picks one with request_color() (the lobby, `coop.find color <name>`); a guest's pick goes to the host,
//     which refuses one another player has;
//   * arrows: coop.cpp hands the partners' positions in this stage to draw_arrows() every overlay pass (Cell and
//     Creature: the partner's creature, Tribe: its chief, Civ: where it plays (its pointer) else its capital,
//     Space: its ship); gated by the setting all.partner_arrows (off in Rivals mode);
//   * minimap: Creature and Tribe (all.partner_map).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "find_core.h"
#include "net/session.h"
#include "sporekit.h"

namespace pfind {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;
};

void init(const SkHost* host, SkMod* mod, Ctx ctx);

// ---- colours (the API the lobby calls) ----
uint32_t color_of(uint8_t id);  // ARGB of player id (a player without one yet: by its id)
int color_index(uint8_t id);    // palette index, -1 none yet
uint32_t my_color();
// pick colour idx (findx::kPalette) for this player: the host (or alone) takes it now, a guest asks the host (its
// table answers); false and *why when the colour is not one or another player has it
bool request_color(int idx, std::string* why = nullptr);
const findx::Colors& colors();  // the table in force here
uint32_t colors_changes();      // grows on every change of the table (the lobby redraws)

void on_session_start();        // hosting or joining: a new table (the host takes its colour)
void on_player_join(uint8_t id);
void on_player_leave(uint8_t id);
void on_connected();            // guest: the wanted colour goes to the host
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();

// ---- arrows ----
struct Target {
    uint8_t id;
    std::string name;
    float pos[3];
    bool flat = false;      // Cell: the plane's world (no planet)
    std::string what;       // "" or a suffix of the label ("chief", "capital", ...)
};
// inside the overlay pass: one arrow per target off screen; me: the own avatar's position (the distance and the
// way to a partner behind the camera), nullptr when none. Returns the number of arrows drawn.
int draw_arrows(const std::vector<Target>& targets, const float* me);
// the overlay pass hands the same positions to the minimap (every frame); its update runs outside the frame
void set_map_targets(const std::vector<Target>& targets, uint32_t mode);
void tick(double now);          // every frame: the minimap update is scheduled outside the frame
bool arrows_on();               // the setting (with Rivals) and the twin switch
bool map_on();

// tests (BUG-0907): the name labels the overlay drew over the world in its last pass (`coop.find labels`): kind
// "player" (a partner's creature, cell, ship or pointer), "chief" / "member" (Tribe), "city" / "vehicle" (Civ), the
// player it names, the text, where on the screen and the world point it stands for
void labels_begin();
void note_label(const char* kind, uint8_t id, const std::string& text, const float sc[2], const float world[3]);

int command(void*, int argc, const char* const* argv, SkOut* out); // coop.find
std::string status();           // a line for coop.status

extern bool g_test_rivals;      // tests: this game reads as in the Rivals mode
extern bool g_arrows_fix;       // find.arrows (BUG-0764): off = no arrows (0.7.11)
extern bool g_map_fix;          // find.map (BUG-0764): off = no partners on the minimap (0.7.11)
extern bool g_colors_fix;       // find.colors (BUG-0764): off = each game picks colours by itself (0.7.11 had none)
extern bool g_placed_fix;       // find.placed (BUG-0939): off = a partner without a place in this world is a target too

} // namespace pfind
