// E2 (BUG-0864): the game glue of the host's autosave (autosave.h has the clock). docs/01-architecture.md#arch-autosave.
#pragma once

#include <functional>
#include <string>

#include "net/session.h"
#include "sporekit.h"

namespace autosave {

void init(const SkHost* host, SkMod* mod, net::Session* sess, std::function<void(const std::string&)> feed);
void tick();                         // every frame (main thread)
void milestone(const char* why);     // a milestone of the host's game (a colony founded, a city captured, ...)
std::string saved_line();            // the co-op window (host): "saved 3 min ago", "" when not hosting

} // namespace autosave
