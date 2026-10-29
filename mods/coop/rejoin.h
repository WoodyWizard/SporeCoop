// The rejoin token (BUG-0235): the host replaces a silent entry with a new HELLO only when both carry
// the same token, i.e. the same game after a crash. A player who is only busy (loading, saving) keeps
// its entry when somebody else of the same name joins.
//
// The token lives in <AppData>\SporeCoop\rejoin-<instance>-<slot>.txt (profile-aware AppData, outside
// the game folder and every save). At start-up the game takes the first slot it can open exclusively
// and keeps that handle; a crashed game's handle is closed by the system, so its restart takes the
// same slot and finds the token the crash left behind. A clean end (leave, session end, unload)
// empties the file, so no stable id follows a player from host to host. When the file cannot be used
// the token is random for this process: a crash rejoin then gets "name 2", nothing worse.
#pragma once

#include <cstdint>

namespace rejoin {

uint64_t token_for_join(int instance); // the leftover token of a crash, else a fresh one (written to the slot)
void clear();              // a clean end: the next join gets a new token
void shutdown();           // mod unload: clear and close the slot (the reloaded DLL takes it again)

} // namespace rejoin
