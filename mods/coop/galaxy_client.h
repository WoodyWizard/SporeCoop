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
    std::function<uint64_t()> key; // 6S: the identity's secret key (identity::galaxy_key)
    std::string version;
    std::function<bool(std::string& err)> host;                     // host the loaded world (the relay is set)
    std::function<bool(const std::string& addr, std::string& err)> join; // join that address (the relay is set)
    std::function<bool()> ready; // the player plays a stage now (no load waits): a downloaded world is loaded
    // this game hosts a planet it loaded from the galaxy (the first creature): called until it returns true (done or not
    // needed), 10 minutes at most
    std::function<bool()> hosted_stored;
    std::function<void()> promote; // 6B C: this guest becomes the host in place (its proxies go on as its NPCs)
    // 6F B: a message of the planet's host copied by the galaxy server (from the host: dispatched as the session's)
    std::function<void(uint8_t type, net::Reader& r)> fan_in;
    // 6K: the galaxy server's party messages (PARTYVIEW, PARTYNOTE, PLAYERS) for parties.cpp
    std::function<void(uint8_t type, net::Reader& r)> party_in;
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
uint8_t era_of_mode(uint32_t mode);         // step 4: the era (net::galaxy::Era) of a game mode, 0 not a stage
uint32_t planets_version();                 // changes with the list
std::string current_planet();               // the planet this player is on ("" none)
std::string galaxy_name();                  // the connected galaxy's name ("" when not connected)
bool hosting();                             // this game hosts a planet of the galaxy now
bool relayed();
uint32_t key_refusals(); // tests (6S): HELLOs refused for the identity's key                             // the session (if any) runs through the galaxy server: a planet's session
// decision 6F B: the planet's host sends one unreliable message the galaxy server copies to these players' slots
// (net/galaxy.h kFan); false: not hosting through the galaxy, or too big (the caller sends it to each instead)
bool fan_out(const std::vector<uint16_t>& slots, uint8_t type, const net::Writer& body);
uint16_t slot_of(const net::Endpoint& peer);  // a session peer's galaxy slot (0: not a galaxy peer)
void upload_soon(double secs);              // 6I A: host: store the planet within secs (the share saved anew)
// 6K: the parties kept by the galaxy server
bool welcomed();                             // connected to a galaxy server now (it keeps this game's party)
uint16_t my_slot();
bool party_send(uint8_t kind, uint64_t other_identity, uint8_t flag, uint32_t stage);
void ask_players();                          // WHO: the server answers PLAYERS (to C.party_in)
bool whisper(uint64_t to_identity, const std::string& text); // 6L A: through the galaxy server (WHISPER)
unsigned planet_players();                  // 6E B: the players a planet of this galaxy takes (16 by default)
bool close_request();                       // Alt+F4 / the window's X (mouse.cpp): true = held, saved first (6A A)
// decision 6B C (the warm hand-over): a player still in the planet's world rejoins its new host with the hand-over's
// token (the session's resume receipt), and the new host sends it no world
uint64_t warm_join_token();                 // for this join (0 none)
bool accept_warm(uint64_t receipt);         // host: a joiner's receipt is this planet's hand-over token, same world
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
