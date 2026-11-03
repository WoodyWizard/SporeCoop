// Tribe stage co-op, "Chiefs" (docs/09-coop-design.md, RE notes docs/re/tribe.md):
//  - the host simulates every tribe on the planet; guests show mirrors of all citizens
//    (their own behaviour tree ticks are skipped, positions follow host snapshots)
//  - members of the player tribe are split between the players; a player can select and
//    order only their own members; guest orders are sent to the host and executed there
#pragma once

#include "syncdbg.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace tribe {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;
    std::function<void(const std::string&)> notice; // shown to every player
    std::function<bool()> host_away; // guest: the host left the world (main menu): we play on locally
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx); // false: symbols missing
// own members selected now (the "give" button of the co-op window)
int own_selected();
// give the selected own members to another player (the host applies it); returns how many
int give_selected(uint8_t to);
void tick(double now);                               // every frame
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();
void on_world_change();
// BUG-0217: once per frame at the top of on_frame (after game::stage_epoch_tick, before messages):
// calls on_world_change when the Tribe world changed; with tribe.editor_world off it does nothing
// and coop.cpp's avatar_change_check keeps the old trigger
void world_check();
bool editor_world_fix();
void before_save(); // host: the co-op epic leaves before the game saves (BUG-0121)
std::string status();
std::vector<syncdbg::Entry> sync_entries(); // coop.sync (tests)
std::string list();
std::string debug(const std::vector<std::string>& args);
// owner of a player tribe member (player id), 0xFF if unknown
uint8_t owner_of(void* citizen);
bool active(); // tribe sync running (tribe stage, connected)
bool center(net::Vec3& pos); // position of the player tribe
// members of the player tribe with their owner (for name tags)
// the epic creature was hit by a creature (host; from the creature damage filter)
void note_animal_hit(void* animal, void* attacker_creature);
// a chief of a player (bigger, "★" tag)
// the member id (as in coop.tribe and coop.sync t<id>), 0 when not a tracked member
uint16_t id_of(void* citizen);
// tests: "hut<tribe index>[p]" for a tribe's hut, "" for anything else
std::string object_key(void* obj);
// tests: the position of every planet tree (cTreeData, fruit trees to gather from)
// tests: every tribe's centre ("tribe<i>[p]", i as in coop.tribe)
void for_each_tribe_center(void (*fn)(const std::string& key, const net::Vec3& pos, void* user), void* user);
// (fruit: the tree has fruit slots, ~ docs/re/tribe.md Trees)
void for_each_tree_pos(void (*fn)(const net::Vec3& pos, bool fruit, void* user), void* user);
bool is_chief(void* citizen);
void for_each_owned(void (*fn)(void* citizen, uint8_t owner, void* user), void* user);
// the local player's own tribe members (positions), for the effect relay
std::vector<net::Vec3> local_member_positions();
// allocation-free test for the effect relay: is p on one of the local player's members?
bool near_member(const net::Vec3& p, float radius);
// tests (coop.track, BUG-0365): every member of the player tribe as this game shows it. Host: its own
// game's members (tgt null, drawn = its position). Guest: the mirrors, with the target the driving
// aims at (the host's state moved on by its age), the host's reported speed, the driver's teleports
// and where the game drew it this frame (its position before the mod's driving).
struct TrackMember {
    uint16_t id;
    uint8_t owner;
    void* c;
    const net::Vec3* tgt; // null on the host
    float rv;        // the speed of the host's state the driving aims at (guest), its own game speed (host)
    uint16_t teleports;
    const net::Vec3* drawn; // null: its position now
};
void for_each_tracked(void (*fn)(const TrackMember& m, void* user), void* user);

} // namespace tribe
