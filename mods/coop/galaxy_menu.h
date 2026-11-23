// Decision 5X B (alpha): the galaxy menu shows the galaxy server's planets while connected (galaxy_menu.cpp).
#pragma once

#include <string>

#include "sporekit.h"

namespace galaxy_menu {

void init(const SkHost* host, SkMod* mod);
void tick(double now);
std::string command(int argc, const char* const* argv); // coop.gmenu [status | on | off | rebuild]

// 8M A (BUG-9340): Spore's Choose A Stage screen open for a server planet (BUG-9337), for the planet details panel
struct StageScreen {
    std::string planet;     // the planet's shown name
    std::string era[6];     // per stage (net::galaxy era 1 Cell .. 5 Space): the era key offered ("" not offered)
    bool fresh[6] = {};     // a new stage of the planet (started there, BUG-9336)
    bool reached[6] = {};   // reached in this player's own Spore
    void* button[6] = {};   // Spore's stage button on the screen (a UTFWin window)
};
bool stage_screen(StageScreen& out);
// 8M A: called when the screen goes (a stage pressed, Back, or the menu's state left): the panel goes at once
void on_screen_gone(void (*fn)());
// BUG-9350: true while a load from the menu should wait: the new-game screen (Choose A Stage) is open and its Back
// has been asked (outside the frame); false once it is closed, or 5 s on (load as before)
bool stage_screen_holds_load(double now);

} // namespace galaxy_menu
