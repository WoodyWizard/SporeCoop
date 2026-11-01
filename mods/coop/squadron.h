// Space stage co-op, "Squadron" (docs/09-coop-design.md, RE notes docs/re/space.md):
//  - in the host's star system / planet the host simulates the NPC ships; guests that are
//    in the same place see mirrors of them (their own NPC ships there are removed)
//  - guest shots on mirrors are applied by the host; NPC shots that hit a guest's ship on
//    the host are applied to the guest's own ship; mirrors never fire on their own
#pragma once

#include "syncdbg.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace squadron {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<bool()> with_host;            // guest: the host is in our place
    std::function<int(void* ufo)> puppet_owner; // host: player id of a puppet ship, -1 if none
    std::function<void*(uint8_t id)> puppet_of; // host: the puppet ship of a player
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick(double now);
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();
std::string status();
std::vector<syncdbg::Entry> sync_entries(); // coop.sync (tests)
std::string debug(const std::vector<std::string>& args);
// BUG-0378 (tests, coop.fleet track): every ship as this game drew it, sampled at the start of the
// mod's frame (after the game's update, before any write of ours), on the session clock
void frame_start(double session_t, bool clock_ok);

} // namespace squadron
