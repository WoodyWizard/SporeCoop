// Who is in this planet's campaign, and which seat each player holds (Lineages port phase 2, BUG-9260; Lineages
// roadmap 0b, PR #196).
//
// One place for every stage's "which player owns this": the Creature nests, the Tribe villages and the Civilization
// nations of the later phases are indexed by these seats. The host keeps the party (founders::Party, saved next to
// the planet as <planet>.spo.duoparty); a player is recognised by their persistent identity (identity.h), never by
// their network id, so a player who leaves and joins again, or comes back another day, gets the same seat. Seat 0 is
// the host's. A new player gets the next seat while fewer than founders::kPlaySeats are taken.
#pragma once

#include <cstdint>
#include <string>

#include "founders.h"
#include "net/session.h"
#include "sporekit.h"

namespace party {

constexpr uint8_t kNobody = 0xFF;

void init(const SkHost* host, SkMod* mod, net::Session* sess);
// host: seats the connected players of the current planet's campaign, keeps their species and names, saves
void tick(double now);

// this planet's party as the host knows it (empty on a guest and outside a planet)
const founders::Party& roster();
// host: the seat a connected player holds, -1 = none (not seated yet, an older partner without identity, a guest)
int seat_of_player(uint8_t player);
// host: the connected player in a seat, kNobody = offline or unknown
uint8_t player_of_seat(int seat);
// a party species: the host's own or a seat's own creature (the later phases: party members do not hunt each other's
// species, also while their player is offline)
bool party_species(uint32_t creature_instance);
std::string status();
// host (BUG-9263): a seat's own stage history (consequence cards, Creature trait progress); saved with the party.
// false: no such seat (or not the host)
bool set_history(int seat, bool own, const uint32_t (&cards)[founders::kCards], float social, float combat);
// the planet the roster belongs to ("" = none)
const std::string& planet();

} // namespace party
