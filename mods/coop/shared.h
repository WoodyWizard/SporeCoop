// Shared resources of the pack: one total for everybody, owned by the host.
// Tribe food, civilization money and space money (DNA has its own hook-based path in coop.cpp).
// Guests send their local changes to the host and take the host total back, so earning and
// spending works on every machine and nothing is counted twice.
#pragma once

#include <cstdint>
#include <string>

#include "net/session.h"
#include "sporekit.h"

namespace shared {

void init(const SkHost* host, SkMod* mod);
void tick(net::Session& s, uint32_t mode, double now);
void on_message(net::Session& s, uint8_t type, uint8_t from, net::Reader& r);
void on_session_change(); // new session: resend totals
void on_player_join(net::Role role); // a player joined: the host resends its totals (BUG-0220)
void on_session_end();    // the sync state ends with the session (BUG-0049)
void on_player_leave(uint8_t id);
std::string fix(const std::string& name, int on); // coop.res fix reset|floor [0|1] (tests)
std::string status();
bool debug_add(const std::string& name, double v, std::string& err); // local change, as if earned

} // namespace shared
