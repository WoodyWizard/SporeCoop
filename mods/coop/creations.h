// Every player keeps their own creations (creature, cell, spaceship): docs/re/creations.md.
//  - own creation: picked from the player's My Creations (EditorSaves.package); a guest defaults to
//    its newest creation, the host to the species of its world
//  - its records (bem + metadata) go to everybody (the host relays them and sends them to players
//    joining later); a receiver stores them under a collision-free instance in its own small
//    package and registers it with the game's resource manager at runtime
//  - puppets of a player are built from that player's creation; the local avatar takes the own one
//    (abilities follow the parts)
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/dbpf.h"
#include "net/session.h"
#include "sporekit.h"

namespace creations {

enum Kind : uint8_t { kCreature = 0, kCell = 1, kUfo = 2, kKinds = 3 };
// BUG-0469: every kind of model this game can be asked to build for the mod, not only a player's
// creation: a civ mirror vehicle or a building of a city, a host ship. (Kind stays the creations'
// own: its numbers are on the wire.)
enum ModelKind : uint8_t { kMkCreature = 0, kMkCell, kMkUfo, kMkVehicle, kMkBuilding, kMkKinds };
using Key = dbpf::Key;

struct Option {
    Key key;
    std::string name;
};

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;
    std::function<void(const std::string&)> notice; // shown to every player (BUG-0345)
    std::function<std::string(uint8_t)> name_of;
    std::function<void(uint8_t player)> appearance_changed; // rebuild that player's puppet
    std::function<void()> cell_rebuilt; // the own cell hatched again: move it next to a partner
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
bool key_known(const Key& k); // some registered database has this resource (RM FindRecord)
void tick(double now);
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_player_join(uint8_t id); // host: everybody's creations to the newcomer
void on_session_end();
// BUG-0076: a player left: forget its creations (ids are reused: the next player of the id was shown
// with them, and the host handed them to later joiners). coop.corefix creations_leave 0 = keep them
void on_player_leave(uint8_t id);
extern bool g_leave_fix;

// the creation of a player, usable on this machine ({} = none / not received yet)
Key player_key(uint8_t player, Kind k);
// cSpeciesProfile* of a player's creature, nullptr if none
void* player_species(uint8_t player);

// Species of the host's world that a guest's game does not have (creatures of the host's own My
// Creations or of its Sporepedia downloads live on its planet): the guest asks, the host sends the
// creation's records, the guest registers them under the same key. Without them the guest could
// not build these NPCs at all (an ally in the host's pack was invisible, player report 2026-10-25).
void want_species(const Key& k); // guest: this NPC species is unknown here
bool species_pending(const Key& k); // guest: asked, no answer yet
bool avatar_swap_settling(double now); // BUG-0318: the own avatar got a new body moments ago (editors wait)

// Species of the host's world that are the host's own creations (its My Creations). Creation ids
// are per profile and sequential, so a guest often owns another creation under the same key, and its
// game (its own records, its caches of creaturedata and baked models, all by key) showed that one
// at the host's nests: the player report of 2026-10-25, "Woopsies" at the host's nest were the
// guest's own "Datzu" on the guest. The host sends these creations to each guest before its world
// and before an NPC of one; the guest registers each under a key of its own (0x7D......, from the
// content) and builds the host's NPCs of it from that key; the host's species table reaches it
// under both keys (docs/re/creature.md 15).
void host_species_out(uint8_t peer, const Key& k); // host: before an NPC of species k goes to peer
void world_species_out(uint8_t peer, const std::vector<const std::vector<uint8_t>*>& files); // host: before a world share
Key local_species(const Key& k); // guest: the host's key of a species -> the key its NPCs use here
Key host_species(const Key& k);  // guest: ours -> the host's

// BUG-0469: the model a caller may build from `model` (docs/re/creations.md#creations-content-all-kinds):
//  - kSame: this game has the model and every part / paint it names, build it from that key
//  - kSubstituted: some ids this game does not have were shown as substitutes in a copy of the model
//    records, registered under the model's own key above the game's packages: build it from the
//    returned key (the key itself, unchanged: what the game reads for it is our copy)
//  - kRefused: do not build the model here (the caller uses the game's own default or waits)
// allow_copy stays false when the caller cannot pass another key to the game (a building: the game
// reads the civ's design key itself), then a model with unknown ids is refused.
struct Model {
    enum Status : uint8_t { kSame = 0, kSubstituted, kRefused } status = kRefused;
    Key key; // {} = do not build it here
};
Model ensure_model(const Key& model, ModelKind kind, bool allow_copy = true);
std::string content_status(); // the counters of ensure_model, for the tests and the support trace
bool content_all_kinds(); // the switch content.all_kinds (off: 0.7.3.5 for the new call sites)

// BUG-0341: creatures with parts of the "Creepy & Cute" pack on a game without it (the game faulted
// at 0x441F3B building them): not built here; the host's world is not loaded while one of its own
// creations needs such parts (the names, "" = none)
bool have_creepy_cute();
std::string world_missing_parts();
std::vector<uint32_t> model_parts(const std::vector<uint8_t>& bem); // part ids a creature model names

// My Creations of this kind, newest first
std::vector<Option> options(Kind k);
Option mine(Kind k);
void choose(Kind k, const Key& key); // {} = keep the world's own
// UI: the next / previous own creation
void cycle(Kind k, int dir);

std::string status();
// support trace (trace.h): the own creation of the stage, where the choice came from, what the avatar has
std::string trace_line(Kind k);
std::string debug(const std::vector<std::string>& args);

} // namespace creations
