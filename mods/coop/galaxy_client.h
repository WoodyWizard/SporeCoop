// Decision 5O A (docs/12-server-and-players.md#server-front-door): the game's side of the galaxy server
// (server/galaxy, net/galaxy.h). The player connects to the galaxy server's address, lists its planets and enters
// one: the server makes it that planet's host (it hosts the world it has loaded) or sends it to the planet's host.
// Either way the planet's session is an ordinary SporeCoop session whose packets go through the galaxy server
// (the relay transport), so the rest of the mod does not know the difference.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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
    std::function<bool()> ready; // the player plays a stage now (no load waits): a downloaded world is loaded
    // this game hosts a planet it loaded from the galaxy (the first creature): called until it returns true (done or not
    // needed), 10 minutes at most
    std::function<bool()> hosted_stored;
};

// a planet of the server's list (LIST), for the galaxy menu (5X B)
struct PlanetInfo {
    std::string name;
    uint16_t players = 0, host = 0;
    uint32_t stored = 0;
    uint8_t tech = 0;          // the menu's tech level: 0 Cell, 1 Creature, 2 Tribe, 4 Civ, 5 Space
    uint32_t species[3]{};     // its creature (the planet's last host's)
};

void init(const SkHost* host, SkMod* mod, Ctx ctx);
bool online();                              // connected to a galaxy server (welcomed)
std::vector<PlanetInfo> planets_known();    // the last list: the planets with players first
uint32_t planets_version();                 // changes with the list
std::string current_planet();               // the planet this player is on ("" none)
std::string galaxy_name();                  // the connected galaxy's name ("" when not connected)
bool hosting();                             // this game hosts a planet of the galaxy now
bool close_request();                       // Alt+F4 / the window's X (mouse.cpp): true = held, saved first (6A A)
void tick(double now);
std::string connect(const std::string& address); // ip or name, [:port] (47900)
std::string disconnect();
std::string list();                    // asks for the planets; the answer comes in the feed and status()
std::string enter(const std::string& planet);
std::string leave();
std::string save(); // host: save the planet's world to the galaxy server now (also every few minutes and when leaving)
std::string status();
void set_upload_every(double seconds); // tests (300 by default)

} // namespace galaxy
