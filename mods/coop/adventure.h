#pragma once

#include <string>
#include "net/protocol.h"
#include "net/session.h"
#include "sporekit.h"

namespace adventure {
void init(const SkHost*, SkMod*, net::Session*);
void tick();
void message(uint8_t from, net::Reader&);
void reset();
net::AdventureGate outgoing();
std::string status(); // BUG-9089: the shared mission's counters (coop.adventure)
std::string debug_kill(uint32_t scenario_class); // tests: coop.adventure kill <class>
// BUG-9114: the host simulates the scenario's creatures for everyone
void set_body_lookup(void* (*fn)(uint8_t player));           // a partner's body in this game, or nullptr
bool partners_targetable();                                    // host: creatures may pick a partner's body
bool guest_damage(void* c, float damage, int type, void* attacker); // guest: true = handled, not applied here
void actors_message(uint8_t from, net::Reader&);               // MSG_ADVENTURE_ACTORS
std::string actors(float radius, int around_player = -1);      // tests: coop.adventure actors [radius] [player]
std::string debug_hit(uint32_t scenario_class, float damage); // tests: coop.adventure hit <class> [damage]
}
