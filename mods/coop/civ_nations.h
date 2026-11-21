// Every player's own nation in Civilization (BUG-9274; the Lineages port phase 5 for Civ, from PR #196's civ.cpp
// founders parts: founders_multi, refresh_founder_seats, founder_owner, the team campaign). civ_nations.cpp,
// pure rules civ_seats.h, docs/re/civ.md#civ-own-nations.
//
// Host: a partner whose party seat has a species of its own (its Tribe village's, else its own creature) gets a capital
// of that species: the game's next free city site (SpawnNextCity, the guests found it too), the new city's fresh nation
// gets the seat's species (cCivilization +0x268) and is recorded by party seat in <planet>.spo.duonations. That
// nation's cities and vehicles are that player's (nobody's while the player is away: the nation waits), its money is
// the nation's own (+0x98, kept in the save; the guest's HUD shows it through civ_wallet's MSG_CIV_WALLET), it runs the
// game's player branch of the nation AI (no NPC orders or purchases), it is allied with the other players' nations
// (no damage, no capture between them). The team's cities count for every player nation's level and the HUD
// progress; when only the players' nations are left the host's game shows the planet won (the Space vote follows).
// Guest: its own nation (the political id of the cities the host says are its own) counts as the player's for its
// game's IsPlayerOwned and the terrain cursor, so the game's own clicks, box and city menus command it (the orders go
// to the host as before).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace civ::nations {

void init();
void host_tick(double now); // from civ_host.cpp host_tick
void guest_tick(double now);
void forget(); // a world change or a session end

// host: a partner nation's seat (>= 1) by political id, -1: none
int seat_of_faction(uint32_t pol);
// a player's nation (seat >= 1) is that faction: the owner of its cities and vehicles (host: the player or kNoOwner;
// guest: whatever the host sent); 0xFF: no player's partner nation
uint8_t owner_of_faction(uint32_t pol);
// the players' nations (the host's and every partner's)
bool team_faction(uint32_t pol);
// host: the players whose own nation lives in this world (they get none of the host nation's cities)
std::vector<uint8_t> nation_players();
// host: the nation a player commands (null: none); its political id
void* nation_of_player(uint8_t player);
// the nation a city's purchases and buildings are paid from: its own for a partner nation's city, else the player civ
void* paying_nation(void* city);
// MSG_CIV_STATE's city owner byte for a city (civ_seats::wire_owner)
uint8_t wire_owner(void* city, uint8_t owner);
// guest: its own nation's political id (0: none: it plays the host's nation)
uint32_t own_faction();
// host: the SpawnNextCity of anybody else waits while a capital is being founded
bool hold_foundings();
// host: the city a guest built in its own nation's planner is paid from that nation (it paid in its own planner)
void charge_build(void* city, float paid);
// host: a refund to a player with a nation goes to its nation; true when done
bool refund(uint8_t player, double amount);
// civ_wallet: the nation's money of a player (host); false: the player has no nation
bool player_money(uint8_t player, double& money);
// civ_wallet::active(): a player's nation money is shown through the wallet (host: a partner nation lives; guest: it
// has its own nation)
bool wallets();
// guest: the cursor's candidate list of its own nation (cCivilization +0xD4), null: none
void* cursor_list();
// friendly fire between the players' nations: true = the hit or capture is dropped
bool friendly(uint32_t target_pol, uint32_t attacker_pol);
// host: a partner nation captured a city (the feed, the autosave)
void captured(void* city, uint32_t pol);

std::string status();
std::string debug(const std::vector<std::string>& args);

} // namespace civ::nations
