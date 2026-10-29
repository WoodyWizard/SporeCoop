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
using Key = dbpf::Key;

struct Option {
    Key key;
    std::string name;
};

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;
    std::function<std::string(uint8_t)> name_of;
    std::function<void(uint8_t player)> appearance_changed; // rebuild that player's puppet
    std::function<void()> cell_rebuilt; // the own cell hatched again: move it next to a partner
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
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
