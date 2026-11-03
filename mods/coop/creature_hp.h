// Creature stage health between players (docs/re/creature.md#creature-max-hit-points):
//  - BUG-0373: a partner's puppet shows the partner's health and max health (it was pinned to full)
//  - BUG-0374: the host's pack and family never hurt a partner, a partner's hits never hurt them
//  - BUG-0375: an own creature that is not the world's player species gets the brain level's health
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "sporekit.h"

namespace creature_hp {

struct Ctx {
    std::function<void*(uint8_t id)> puppet_of;
    std::function<bool()> is_host;
};

void init(const SkHost* host, SkMod* mod, Ctx ctx);

// modes.cpp (creature puppets, every frame): the partner's health and max health as its own game
// has them. Returns false when the fix is off (the caller pins the puppet to full health as before).
bool drive_puppet(void* puppet, float health, float max_health);
void forget_puppet(void* puppet); // the puppet goes

// host (npc.cpp): a pack member or a member of the avatar's herd (the family)
bool host_party(void* c);
// host, MSG_DAMAGE_REQ: true = the partner's hit on `npc` is dropped (it is the host's party)
bool drop_partner_hit(void* npc);
// host, a hit on a partner's puppet: true = not forwarded (the attacker is the host's party)
bool drop_party_hit(void* attacker);

std::string status();
void tick(); // every frame: the players' species that count as the player species (BUG-0462)

} // namespace creature_hp
