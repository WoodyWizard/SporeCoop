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
// BUG-0588, BUG-0594: this game is in the end of Tribe (docs/re/tribe.md#tribe-civ-transition): back from the City Hall editor
// (the player tribe has no food mat any more, +0x260 = 0). From the City Hall on the game takes the player village and
// the animals out of the world (totem pole and food mat destroyed, every animal at (0, 0, 0)) for the Civ stage; a
// guest's game, which runs no such phase, keeps them until the Civ world comes
bool stage_end();
bool center(net::Vec3& pos); // position of the player tribe
// members of the player tribe with their owner (for name tags)
// the epic creature was hit by a creature (host; from the creature damage filter)
void note_animal_hit(void* animal, void* attacker_creature);
// BUG-0569 (tribe.mirror_anims): host, a behaviour animation the game started on a creature (game.cpp's listener,
// npc.cpp passes it on); guest, MSG_NPC_ANIM kinds 2 / 3 after the kind byte
void on_behaviour_anim(void* c, uint32_t anim, int how, int a, int b);
void on_member_anims(uint8_t kind, net::Reader& r);
// a chief of a player (bigger, "★" tag)
// the member id (as in coop.tribe and coop.sync t<id>), 0 when not a tracked member
uint16_t id_of(void* citizen);
// guest: the mirror of the host's member #id (null: none)
void* mirror_of(uint16_t id);
// tests: "hut<tribe index>[p]" for a tribe's hut, "" for anything else
std::string object_key(void* obj);
// tests: the position of every planet tree (cTreeData, fruit trees to gather from)
// tests: every tribe's centre ("tribe<i>[p]", i as in coop.tribe)
void for_each_tribe_center(void (*fn)(const std::string& key, const net::Vec3& pos, void* user), void* user);
// (fruit: the tree has fruit slots, ~ docs/re/tribe.md Trees)
void for_each_tree_pos(void (*fn)(const net::Vec3& pos, bool fruit, void* user), void* user);
bool is_chief(void* citizen);
// W2 (BUG-0564): a guest removes a village of its own the host has not (realign.cpp): 1 removed, 0 not this
// one (the player tribe, mapped to a host tribe, not a guest in Tribe), -1 not now (a cinematic plays)
int remove_village(void* tribe);
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

// BUG-0514: the player tribe's tools (cTribeTool, tribe +0x36C: weapon racks, the fire pit, ...) with their
// type (tool vt 0x58) and position, for the host-owned world objects (worldobj.cpp kind "tribe_tool")
void for_each_player_tool(void (*fn)(void* tool, int type, const net::Vec3& pos, void* user), void* user);
// a tool of `type` for this game's player tribe at `pos` (upright on the planet), as the palette's placement
// makes it (cTribe::CreateTool, SetTribe, the model, the purchased bit); nullptr: failed
void* make_player_tool(int type, const net::Vec3& pos);

// BUG-0523: the tools of every tribe of this game (`all` false: the player tribe's only). `tribe` is the host's
// index of the tool's tribe (the MSG_TRIBE_STATE table order; on a guest its local tribe mapped back, -1 when no
// host tribe maps to it), `player` the player tribe's. A tool the palette is dragging (its spatial +0x72, set by
// the placement's drag 0xD09FA0) is left out: it is not placed yet.
using ToolFn = void (*)(void* tool, int type, int tribe, bool player, const net::Vec3& pos, void* user);
void for_each_tool(bool all, ToolFn fn, void* user);
// a tool of `type` for the tribe the host's index `tribe` names (on a guest its mapped local tribe) at `pos`;
// nullptr: no such tribe here, the tribe is not near `pos` or the game failed
void* make_tool(int tribe, int type, const net::Vec3& pos);
// this game's tribe of host index `tribe` has a tool of `type` (also one being dragged); -1: no such tribe here
int has_tool_type(int tribe, int type);
// the game's cTribe::RemoveTool (vt 0xB8: out of the village's layout slots and tool lists, destroyed), as the
// palette does with a tool that is sold or placed again
bool remove_tool(void* tool);
// BUG-0524 (host): a guest bought a tool of `type` for the player tribe and placed it at `pos`: the same here (a
// tool of that type elsewhere goes, as the palette's placement of a type the tribe has); false: nothing done
bool buy_player_tool(int type, const net::Vec3& pos);

} // namespace tribe
