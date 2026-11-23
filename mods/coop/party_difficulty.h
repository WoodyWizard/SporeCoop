// The difficulty for 3-4 players (WP-D1 of docs/plan-0.7.17.md item 5, BUG-1473): all.party_difficulty scales the
// fights between players and NPCs on the host (party_difficulty_logic.h has the rules and why). Host side only: a guest
// mirrors the host's NPCs and takes the hits the host sends it, already scaled; a guest's game never scales.
// `coop.fix core.party_tuning off`: every hit is the game's own (0.7.16.1), whatever the setting says.
#pragma once

#include <cstdint>
#include <string>

#include "net/session.h"
#include "party_difficulty_logic.h"
#include "sporekit.h"

namespace party {

void init(const SkHost* host, SkMod* mod, net::Session* sess);

// the percentage in force on this game: the host's setting (auto: by the session's players); 100 on a guest, alone,
// with the switch off
int percent_now();

// host: a player's hit on an NPC (an NPC victim, a player's attacker): the damage to apply
float player_hits_npc(float damage);
// host: an NPC's hit on a player (the host's own creature, ship, vehicle, member, or a guest's, forwarded to it)
float npc_hits_player(float damage);
// host: either, by the sides (the stages that know both sides at one place)
float hit(float damage, Side victim, Side attacker);

std::string status(); // `coop.party`, the support report

} // namespace party
