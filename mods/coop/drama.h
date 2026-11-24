// The game's random world events (Drama Manager: the visiting UFO, meteors, the sea monster, ...)
// in a session: a guest's own game does not start the ones that belong to the host's world
// (BUG-0486, BUG-0465; docs/re/creature.md#creature-drama-events).
#pragma once

#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace drama {

void init(const SkHost* host, SkMod* mod, net::Session* sess);
void on_message(uint8_t from, net::Reader& r); // MSG_DRAMA (BUG-9131)
bool host_ufo_here(); // guest: a UFO here is the host's, shown only (it takes no creature)
// coop.drama [start <kind> | mask <hex>]: the events seen, started and refused
std::string debug(const std::vector<std::string>& args);
// BUG-0736: a guest's own Summon Flock (event kind 8) is the host's to make
extern bool g_flock_relay;
// host: the game's summon event started with a partner's puppet as the avatar (the flock comes to it); false: none
bool summon_for(void* puppet, uint8_t owner = 0xFF);
// BUG-9362: an extra summon event for a party whose flock is not out while the world's own runs (host; puppet null:
// the host's own avatar leads it); flock_tick runs them each frame; flock_owners: the summoners of the flocks out now
// (the world's event first)
bool summon_extra(void* puppet, uint8_t owner);
bool end_extra_flock(uint8_t owner); // BUG-9362: as end_flock_event, for that player's extra event
void flock_tick(double now);
std::vector<uint8_t> flock_owners();
bool extra_on();
bool world_flock_busy();
// BUG-9233: the summon event started by summon_for ends at its next update (its time left, +0x2C, to 0): the game takes
// its flock out of the host's pack then; false when there is none
bool end_flock_event();
// a summon event (anyone's) ran its update within the last 2 s (the game updates an active one every tick)
bool summon_running();
void* summon_puppet(); // decision 5FA: the partner's puppet whose summon runs (null: none or the host's own)

} // namespace drama
