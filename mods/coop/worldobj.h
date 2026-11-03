// World objects owned by the host (docs/sync-architecture-plan.md L1 + L2, class G4 / BUG-0421;
// docs/re/creature.md#creature-world-objects).
//
// One mechanism for every kind of world object the players can take or use in Creature and Tribe
// (fruit, the fruit on the trees, bones / skeletons that give parts, ...): the host's game owns them.
// A guest never keeps a copy of its own: the host sends, per kind, the objects near each guest with
// their replicated state (L2), the guest binds them to its own copies by position, applies the host's
// state with the game's own function and removes the copies the host does not have. When a player
// takes or uses an object in a guest's game, the guest asks the host (L1: one request path for every
// kind), the host runs the game's own function on its object and the result reaches every guest with
// the next snapshot. A kind is one row of the table in worldobj.cpp (enumerate, read, done, finish,
// act), not a message of its own.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "net/session.h"
#include "sporekit.h"

namespace wobj {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<bool(uint8_t id, net::Vec3& pos)> player_pos; // host: where guest `id` is (false: not in the world)
    std::function<bool(uint8_t id)> in_place;                   // host: guest `id` plays our world
    std::function<bool()> host_away;                            // guest: its own game runs its world
    std::function<bool(net::Vec3& pos)> stage_center;           // Tribe: the player tribe's place
};

void init(const SkHost* host, SkMod* mod, const Ctx& ctx);
void tick(double now); // every frame
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();
void on_world_change();
std::string status();

} // namespace wobj
