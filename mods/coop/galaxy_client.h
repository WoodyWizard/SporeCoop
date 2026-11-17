// Decision 5O A (docs/12-server-and-players.md#server-front-door): the game's side of the galaxy server
// (server/galaxy, net/galaxy.h). The player connects to the galaxy server's address, lists its planets and enters
// one: the server makes it that planet's host (it hosts the world it has loaded) or sends it to the planet's host.
// Either way the planet's session is an ordinary SporeCoop session whose packets go through the galaxy server
// (the relay transport), so the rest of the mod does not know the difference.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "net/session.h"
#include "sporekit.h"

namespace galaxy {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;
    std::function<std::string()> name;
    std::function<uint64_t()> identity;
    std::string version;
    std::function<bool(std::string& err)> host;                     // host the loaded world (the relay is set)
    std::function<bool(const std::string& addr, std::string& err)> join; // join that address (the relay is set)
};

void init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick(double now);
std::string connect(const std::string& address); // ip or name, [:port] (47900)
std::string disconnect();
std::string list();                    // asks for the planets; the answer comes in the feed and status()
std::string enter(const std::string& planet);
std::string leave();
std::string status();

} // namespace galaxy
