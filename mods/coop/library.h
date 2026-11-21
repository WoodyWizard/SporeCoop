#pragma once
#include "net/dbpf.h"
#include "net/session.h"
#include "sporekit.h"
#include <string>
#include <vector>

namespace library {
void init(const SkHost *host, SkMod *mod, net::Session *session);
void tick(double now, bool safe);
void on_message(uint8_t from, net::Reader &reader);
void on_session_end();
void on_player_leave(uint8_t peer);
std::string debug(const std::vector<std::string> &args); // outside Present
// BUG-9105: this player's choice to let hosts pass their creations on (coop.ini [library])
bool foreign_creations(); // BUG-9112: receive the partners' foreign creations (replaces Share Onward)
struct CacheCell {
    dbpf::Key key;
    std::string source, name, author;
    bool local = false;
};
std::vector<CacheCell> pool_cells(); // BUG-9111: cells the host may use as Cell NPC species
void set_foreign_creations(bool on);
// BUG-9328: a Lineages Cache creation is imported under another key in each game; its PNG hash names it everywhere.
// hash_of: the cache hash of this game's creation with that instance and group ("" when it is not a cached one);
// key_of: this game's key of the cached creation with that hash (false when it is not here, failed or banned)
std::string hash_of(uint32_t instance, uint32_t group);
bool key_of(const std::string &hash, dbpf::Key &out);
} // namespace library
