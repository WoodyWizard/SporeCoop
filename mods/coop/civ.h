// Civilization stage co-op, "Governors" (docs/09-coop-design.md, RE notes docs/re/civ.md):
//  - the host simulates every vehicle and city; guests show mirrors (their behaviour trees
//    are skipped, poses follow host snapshots) and do not spawn vehicles or grow cities alone
//  - cities of the player civilization are split between the players (capital = host);
//    a player buys vehicles and builds only in their own cities
//  - vehicles belong to the owner of the city that built them; only the owner can select
//    and order them; guest orders, purchases and buildings are executed by the host
//  - city captures and new buildings on the host are repeated on every guest
#pragma once

#include "syncdbg.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace civ {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;
    std::function<void(const std::string&)> notice; // shown to every player
    std::function<bool()> host_away; // guest: the host left the world (main menu): we play on locally
    std::function<uint8_t(uint8_t player)> role_of;  // 0 none, 1 military, 2 economic, 3 religious
    // host: a vote of all players; `action` runs when it passes (false: another vote is running)
    std::function<bool(const std::string& text, const std::string& done, std::function<void()> action)> vote;
    // a world beacon (label + edge arrow) on this machine
    std::function<void(const net::Vec3& pos, const std::string& text, double seconds, uint32_t argb)> beacon;
};
// own vehicles selected now, else 1 if an own city's menu was opened last (the "give" button)
int own_selected(bool* city = nullptr);
// give them to another player (the host applies it); returns how many
int give_selected(uint8_t to);

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick(double now);
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();
void on_world_change(); // a load: the tables of game pointers are cleared (BUG-0059)
// BUG-0223: once per frame at the top of on_frame (after game::stage_epoch_tick, before messages):
// calls on_world_change when the Civ world changed; with civ.world_epoch off it does nothing and
// coop.cpp's avatar_change_check keeps the old trigger
void world_check();
bool world_epoch_fix();
std::string status();
std::vector<syncdbg::Entry> sync_entries(); // coop.sync (tests)
std::string list();
std::string debug(const std::vector<std::string>& args);
bool active();
// owners for overlay tags
void for_each_owned(void (*fn)(void* obj, const net::Vec3& pos, uint8_t owner, bool city, void* user), void* user);
// test bots (coop.targets): every city ("city", host index, owner: player id, 253 shared, -1 other nation)
// and every player vehicle ("vehicle", net id, owner)
void for_each_target(void (*fn)(const char* kind, int id, int owner, const net::Vec3& pos, void* user), void* user);

} // namespace civ
