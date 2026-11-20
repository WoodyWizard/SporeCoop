// BUG-9277 (step 4 part 2, docs/12-server-and-players.md#server-party-progress-design): on a galaxy planet each party
// has its own evolution bar (Creature: cPlayer +0x10F0, the earned progress that raises the brain level and ends the
// stage). The host's game bar is the host's party's; the host keeps every other party's bar, a guest's earned
// progress goes to its own party's bar, and each guest's MSG_DNA carries its party's bar. Saved next to the planet
// (<save>.spo.duo-progress, by player identity, as the relations are, BUG-9270). Switch `creature.party_bar`.
#pragma once

#include <cstdint>
#include <string>

#include "net/session.h"

struct SkHost;
struct SkMod;

namespace party_bar {

void init(const SkHost* host, SkMod* mod);
bool on();                                                   // switch, a galaxy planet's party rules, Creature
// host: a guest's earned progress (after its pace); true when it went to its own party's bar, not the host's
bool credit(const net::Session& s, uint8_t player, float amount);
// host: the bar a guest's MSG_DNA carries: its party's (the host's party: host_bar)
float bar_for(const net::Session& s, uint8_t player, float host_bar);
bool changed_since(uint32_t serial);                          // host: a party's bar moved since that serial
uint32_t serial();
void tick(const net::Session& s);                             // host: the planet's file, once per planet
void save();                                                  // host: after a native save of the planet
std::string status();
// BUG-9279: a guest set its party's bar in this world (MSG_DNA): promoted in place, the game's bar is its own party's
void note_guest(uint32_t world);

} // namespace party_bar
