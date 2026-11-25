// Tribe Rivals runtime module (WP-R4 of 0.8.0, BUG-1548, docs/11-rivals.md 11.3.3, 11.4-11.9).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace tribe_rivals {

void init(const SkHost* host, SkMod* mod, net::Session* sess);
void tick(double now);

// True when COOP_FIX("rivals.tribe") and rivals.core are enabled, session is connected,
// we are in Tribe stage, and Tribe stage mode is Rivals.
bool enabled();
bool active();

// R2: Host ownership of cTribe objects (0 = host, 1..3 = guest, 0xFE = NPC tribe).
uint8_t owner_of_tribe(void* tribe);
void* tribe_of_player(uint8_t pid);
bool is_player_owned_tribe(void* tribe);

// R1: Guest perspective swap state (used by tribe_mirrors.cpp to map host tribes cleanly).
bool perspective_swapped();
void* orig_player_tribe();

// War PvP & neutral/ally damage gate for cCreatureCitizen::TakeDamage (host).
// Returns false to block damage completely; returns true to allow (possibly untouched).
bool filter_citizen_damage(void* victim_citizen, void* attacker_citizen);

// War right-click action override for cTribeInputStrategy::ChooseInputActionID.
// Returns >0 action override (0x0A attack citizen, 0x0C raid hut/tool), -1 to force block (0),
// or 0 to leave vanilla result unchanged.
int override_choose_action(void* target_obj, int vanilla_action);

// Guest baby purchase in Tribe Rivals (spawns in guest's own tribe and charges guest's own food).
// Returns true if handled by Tribe Rivals.
bool handle_guest_baby(uint8_t from, float x, float y, float z);

// Guest tool purchase food helpers in Tribe Rivals (BUG-1554).
int32_t player_food(uint8_t pid);
bool spend_player_food(uint8_t pid, int32_t cost);
void sync_player_food(uint8_t pid);

// Diagnostics and console subcommands (`coop.tribe rivals ...`).
std::string status_summary();
std::string debug_cmd(const std::vector<std::string>& args);

} // namespace tribe_rivals
