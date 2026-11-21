// BUG-9283 (Lineages port, PR #196's creature_rescue.{h,cpp}): a player's downed creature in the Creature stage, its
// revive by a partner and the party wipe. Rules: creature_rescue_logic.h. docs/re/creature.md#creature-pack-rescue-downed.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/protocol.h"
#include "net/session.h"
#include "sporekit.h"

namespace creature_rescue {

struct Ctx {
    net::Session* sess = nullptr;
    // every partner's latest state in this world (its creature in it, fresh): who may revive, who is down
    std::function<std::vector<net::PlayerState>()> peers;
    std::function<void*(uint8_t)> puppet_of;        // a partner's creature in this game (null: none)
    std::function<void(const std::string&)> notice; // shown to everybody
    std::function<void(const std::string&)> feed;   // shown here only
};
void init(const SkHost* host, SkMod* mod, Ctx ctx);

// the own creature is about to take `damage` (teamplay::on_own_damage): true = the hit is dropped (damage set to 0):
// the creature went down now, is down, or was just revived
bool protect(void* avatar, float& damage);
void tick(double now);                       // every frame
void message(uint8_t from, net::Reader& r);  // MSG_CREATURE_RESCUE
bool down();                                 // this game's own creature is down
bool pending();                              // a window is open (no world save then)
// a partner's creature is down here: the seconds it has left (0: not down)
float seconds_left_of(uint8_t player);
// every downed creature this game shows (its own and the partners' puppets): its player, the seconds left, the revive
void for_each_downed(void (*fn)(void* body, uint8_t player, float seconds, float revive, void* user), void* user);
std::string debug(const std::vector<std::string>& args); // coop.rescue [down | giveup]

} // namespace creature_rescue
