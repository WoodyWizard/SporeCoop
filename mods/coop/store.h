// The per-player store on the host's disk (E1, BUG-1149): the game glue of player_store.h. docs/01-architecture.md
// #arch-player-store.
//
// * The store of the world the host plays (its save slot and planet, world::current_game_key) is read from
//   `<Spore>/Games-sporecoop-store/<slot>/<planet>.txt` when that world opens (tick(), every frame on the main thread)
//   and written after every save of the game (after_save(): the save hook of space_allies.cpp, the one hook on
//   cGamePersistenceManager_SaveGame; the write itself runs outside the frame). A guest of a session keeps no store.
// * The consumers (features) register with add(): `save` puts their records into the store at every save (only the
//   keys and world records they know: a record of a player they never saw in this process stays as it was read),
//   `adopt` moves one player's things to another key in their memory (the host's word, `coop.store adopt`). They read
//   their records themselves from world() when their world opens (serial(): +1 for every store opened).
// * Keys: key_of(id) is the player's stable key (player_store.h stable_key: a salted digest of
//   net::Session::player_key under this host profile's salt, `salt.txt` in the store folder); the host itself is
//   "host". The Space kept stores (space_kept.h) are keyed by it too (spaceext::kept_key).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/session.h"
#include "player_store.h"
#include "sporekit.h"

namespace store {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;   // the host's feed line
    std::function<void(const std::string&)> notice; // a toast + chat line
};

struct Feature {
    const char* name = "";
    // the game saves now: put this feature's records (stable keys) into `s`; now = unix time
    void (*save)(pstore::Store& s, int64_t now) = nullptr;
    // the host's word: what this feature keeps in memory under `from` is `to`'s now (`to` is a present player)
    void (*adopt)(const std::string& from, const std::string& to) = nullptr;
    // NET-3 (BUG-1499): a save opened the world's store itself (the first save after the world got its name, a save
    // within a second of a load): take the store opened now as this world's, as the next tick would, keeping what
    // this game holds now (the save's records come from it right after). nullptr: nothing to do
    void (*opened)() = nullptr;
};
bool add(Feature f); // static initializers of the feature files

void init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick();       // every frame (main thread): the world's store opens / closes
void after_save(); // the game wrote a save (any stage): the store follows it (written outside the frame)
void on_player_join(uint8_t id); // host: a newcomer the store does not know under a stored name: the host is told

// the store of the world played now; nullptr: no world open (menu, load), or a guest of a session
pstore::Store* world();
uint32_t serial();            // +1 for every world store opened (0: none yet)
std::string world_name();     // "<slot>/<planet>" of world(), "" none
std::string key_of(uint8_t id); // the stable key of a session player ("host" for this host), "" unknown / not the host
int64_t now_unix();
// NET-4 (BUG-1500): the players a world record of `marker`'s feature names (their stable keys) keep their names in the
// file (pstore::Store::mark_owners; the present players' session names). Called from the feature's save
void mark_owners(pstore::Store& s, const char* marker, const std::vector<std::string>& keys, int64_t now);

// the game's own saves (any stage, this machine): count and how long ago the last one was written (-1: none yet)
uint32_t saves();
double since_save();

int command(void*, int argc, const char* const* argv, SkOut* out); // coop.store

} // namespace store
