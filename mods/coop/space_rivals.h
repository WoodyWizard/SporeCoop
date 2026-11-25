// Space Rivals runtime module (WP-R6 of 0.8.0, BUG-1550, docs/11-rivals.md sections 11.3.5, 11.4, 11.7, 11.9).
// Guest homeworld emigration (R5), homeworld inviolability (R7), unified rivals.h integration,
// Space NPC empire influence, Race progress reporting, and Chronicle Galaxy Core finale.
#pragma once

#include <cstdint>
#include <string>

namespace spacerivals {

// True when COOP_FIX("rivals.space") is enabled, rivals.core is enabled, and Space stage mode is Rivals.
bool enabled();
bool stage_rivals_wanted();
bool active();

// R5 (space_allies.cpp hook): true on a guest after it emigrated when `planet` is the host's original homeworld,
// so space_allies mirrors it as the host's rival empire while keeping its existing cities intact.
bool is_relinquished_host_home(uint32_t planet_id);

// R7: true when `planet_id` or `star_id` is any player's protected homeworld in Space Rivals mode.
bool is_protected_homeworld_planet(uint32_t planet_id);
bool is_protected_homeworld_star(uint32_t star_id);

// NPC empire diplomacy hook (called by space_diplomacy.cpp h_ApplyEv in Space Rivals mode to earn influence).
void on_npc_diplomacy_event(uint32_t npc_pol, uint32_t ev_id, float scale);

// Galaxy Core finale hook (called by space_finale.cpp when a player enters the galactic core).
void on_core_entered(uint8_t player_id);

// Optional override of a guest's Civ city count (from Civ -> Space transition or test command).
void set_guest_civ_cities(uint8_t player_id, uint16_t cities);

} // namespace spacerivals
