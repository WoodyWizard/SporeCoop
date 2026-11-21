// Co-op notifications: small native panels (the game's own UI system) that slide in on the right
// side of the screen, stack, and slide out after a few seconds. The kind (join, knocked out,
// revived, vote, chat ...), given by the caller or guessed from a system text, picks the panel colour and icon (data/ui/toast_<kind>.png, drawn by
// tools/make_icons.py). Works the same in every stage and in the menus; the full history stays in
// the co-op window (F9).
#pragma once

#include <functional>
#include <string>

#include "sporekit.h"

namespace notify {

// font: the dialog text font of the co-op window (0 until it is known)
void init(const SkHost* host, SkMod* mod, std::function<uint32_t()> font);
// queue a notification (any thread, inside or outside the frame); kind: "chat", "join", "leave",
// "danger", "good", "vote", "info", or null: guessed from the (English, system) text
void show(const std::string& text, const char* kind = nullptr);
// every frame: slide panels in / out
void tick(double now);
// remove every panel (session end is not needed: they expire; unload is)
void shutdown();
// the panels start at this screen y or lower (the co-op status block's bottom; 0 = no block)
void set_top_min(float y);
// BUG-9322: a game window covers the screen (the Spore Guide): the panels slide out and their time stands still until it
// closes (they come back for the rest of their time)
void set_covered(bool covered);
std::string status();

} // namespace notify
