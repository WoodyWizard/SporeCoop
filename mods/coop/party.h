// Roadmap 0b (BUG-9135): who is in this planet's campaign, and which seat each player holds.
//
// One place for every stage's "which player owns this": the Creature nests, the Tribe villages and the Civilization
// nations are indexed by these seats. The host keeps the party (founders::Party, saved next to the planet as
// .duoparty); a player is recognised by their persistent identity (identity.h), never by their network id. Seat 0 is
// the host's. A new player gets the next seat while fewer than founders::kPlaySeats are taken (owner decision 10).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "founders.h"
#include "net/session.h"
#include "sporekit.h"

namespace party {

constexpr uint8_t kNobody = 0xFF;

void init(const SkHost* host, SkMod* mod, net::Session* sess);
// host: seats the connected players of the current planet's campaign, keeps their species and names, saves
void tick(double now);
void on_world_change();
void on_session_end();

// this planet's party as the host knows it (empty on a guest and outside a planet)
const founders::Party& roster();
// the seat a connected player holds, -1 = none (not seated, an older partner without identity, a guest asking)
int seat_of_player(uint8_t player);
// the connected player in a seat, kNobody = offline or unknown
uint8_t player_of_seat(int seat);
// host: a seat without an identity (taken over from a two-founder save) learns the identity of `player` once
bool bind(int seat, uint8_t player);
// a party species: the host's own or a seat's own creature (owner decision 7: party members' species are best
// friends: nobody in the party hunts them, also while their player is offline)
bool party_species(uint32_t creature_instance);
std::string status();

// Founder seats (founders::Roster: Tribe villages, Civilization nations; seat 0 the host's). A seat with an identity
// (Founder::player) belongs to that player; a seat founded before identities existed (player 0, a DUO1 / CIV1 save)
// belongs to the connected player for whom `legacy(seat, player)` is true (the stage's creature / founder-model match).
// The caller then binds that identity into the roster once (founders::record keeps DUO1 only without identities).
using LegacyMatch = bool (*)(int seat, uint8_t player);
// the seat `player` commands, -1 = none
int founder_seat_of(const founders::Roster& r, uint8_t player, LegacyMatch legacy);
// the connected player commanding `seat`, kNobody = offline / unknown (seat 0: the host itself)
uint8_t founder_player_of(const founders::Roster& r, int seat, LegacyMatch legacy);
// host: the connected players (not the host) that no founder seat belongs to yet, in join order
std::vector<uint8_t> unseated_partners(const founders::Roster& r, LegacyMatch legacy);

} // namespace party
