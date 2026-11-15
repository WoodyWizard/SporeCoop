// The rejoin token (BUG-0235): the host replaces a silent entry with a new HELLO only when both carry
// the same token, i.e. the same game after a crash. A player who is only busy (loading, saving) keeps
// its entry when somebody else of the same name joins.
//
// The token lives in <AppData>\SporeCoop\rejoin-<instance>-<slot>.txt (profile-aware AppData, outside
// the game folder and every save). At start-up the game takes the first slot it can open exclusively
// and keeps that handle; a crashed game's handle is closed by the system, so its restart takes the
// same slot and finds the token the crash left behind. A clean end (leave, session end, unload)
// empties the file, so no stable id follows a player from host to host: a clean leave keeps the token only for the
// same host address and the same name (BUG-1001), in memory and (BUG-1135, net.rejoin_disk) in
// rejoin-<instance>-<slot>-kept.txt next to the slot (rejoin_kept.h: under a salted hash of address and name, so the
// file holds neither; 30 days (net.rejoin_month, BUG-1150; a day without), at most 16 pairs), so the same game after a restart is the same player to that host and
// gets its kept Space state back (BUG-1107); another host or another name never gets it. When the file cannot be used
// the token is random for this process: a crash rejoin then gets "name 2", nothing worse.
#pragma once

#include <cstdint>
#include <string>

namespace rejoin {

// the leftover token of a crash, else the token kept by a clean leave from this host under this name (BUG-1001; in
// memory, else on disk from before a restart, BUG-1135), else a fresh one (written to the slot)
uint64_t token_for_join(int instance, const std::string& host, const std::string& name);
// a clean end: the next join gets a new token. keep (BUG-1001: a leave, a lost connection): the token stays in memory
// for a join to the same host address under the same name, so the player comes back as itself while the host takes
// no new players, and in the kept file for a restart (BUG-1135); never in the slot, never sent to another host or
// under another name
void clear(bool keep = false);
// mod unload (also the game closed in a session): clear (keep: as a leave) and close the slot (the reloaded DLL takes
// it again)
void shutdown(bool keep = false);
// tests (BUG-1107): while on, every join of this game sends a fresh token that is neither written to the slot nor kept
// by the leave, as another game under the same name would; off: the joins take the kept token again. Memory only
void test_other(bool on);
bool test_other_on();

} // namespace rejoin
