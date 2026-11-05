// The guest as a player in the game's rules (P1 of 0.7.7, BUG-0418 / BUG-0420, docs/re/creature.md#creature-player-rules).
//
// In co-op one game decides what happens to the other player's creature: the host's game runs the NPCs, so an
// NPC's charge stuns the guest's puppet there; the guest's game runs its own avatar, so its charge stuns a proxy
// (an AI-less mirror of a host NPC). The game's rule lands on the wrong object and the player whose creature it is
// never gets it. This module is the one place where such a rule is carried to the creature it belongs to:
//
//   * stun (creature vt 0x94 = SporeCoop::CreatureStun 0xC02C70, the ability hit's effect kind 1 and the
//     counter of a charge): host: on a partner's puppet -> MSG_PLAYER_RULE kind 0 to that partner, its game
//     stuns its own avatar (BUG-0554); guest: on a proxy -> kind 1 to the host, the host stuns its NPC.
//   * flight (the attack node 0xD666B0 picks no melee ability against a creature in the air): host: a melee hit
//     (CreatureAttackTarget vt 0xB8 = 0xC02B40) on a puppet whose player is airborne carries no damage / stun to
//     the partner (BUG-0555).
//
// The predicates "is the player / the player's species" are answered for the partners in creature_hp.cpp
// (IsPlayerSpecies vt 0xD0, scoped to GetRelationship / CreatureSocialMode, BUG-0462); the list of the game's
// player checks and how the mod answers each is docs/re/creature.md#creature-player-rules.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "net/protocol.h"
#include "net/session.h"
#include "sporekit.h"

namespace player_rules {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void*(uint8_t id)> puppet_of; // the creature of player id in this game (host: a guest's puppet)
    std::function<bool(uint8_t id)> airborne;   // player id's latest state says its creature is in the air (PF_JUMPING)
};

void init(const SkHost* host, SkMod* mod, Ctx ctx);
void on_message(uint8_t type, uint8_t from, net::Reader& r); // MSG_PLAYER_RULE
std::string status();
bool air_hit_now(); // host: inside a melee hit on an airborne partner's puppet: carry no damage to the partner (BUG-0555)
uint32_t hit_ability_type(); // the ability type (+8) of the creature ability hit running now, 0 none (BUG-0555)
// guest, MSG_DAMAGE: the hit of a melee ability from an NPC 1 m or more away while the own creature is in the air (BUG-0555)
bool guest_drops_air_hit(void* avatar, void* attacker, uint32_t ability);

} // namespace player_rules
