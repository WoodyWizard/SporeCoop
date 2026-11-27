// Civilization Rivals mode runtime module (WP-R5 of 0.8.0, BUG-1549, docs/11-rivals.md 11.3.4, 11.4-11.9).
// Handles:
//  - R2: Host NPC cCivilization takeover by base distance, AI disable, species/design sync, wealth ownership
//  - R1: Guest perspective swap (GetPlayerCivilization, GetPlayerPoliticalID, City_IsPlayerOwned, TerrainCursor)
//  - R4: Per-player NPC nation relations table + SK_NPC_RELS sync + TurnAgainst / Protect / SmoothOver influence
//  - R7: War PvP + ordinary city capture vs strict capital inviolability & superweapon protection
//  - Alliance money/spice gifts (SK_GIFT) & allied-only trade payouts
//  - R3/R6: Stage win (>50% NPC cities) -> catch-up timer -> Civ draft + Space readiness
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "civ_rivals_logic.h"

namespace civ_rivals {

// Master COOP_FIX("rivals.civ", "BUG-1549") state & active query (Civ mode + Civ stage set to Rivals + connected).
bool enabled();
bool active();
bool spec_convert_enabled();
bool sw_spend_enabled();

// Lifecycle wired from civ.cpp
void init();
void tick(double now);

// R1/R2 nation & capital lookups
void* civ_of_player(uint8_t pid);
void* my_civ();
int player_of_pol(uint32_t pol);
bool is_guest_nation(void* civ);
bool is_guest_city(void* city);
bool is_reserved_nation(void* civ);
bool is_reserved_city(void* city);
bool is_player_capital(void* city, uint8_t* owner_out = nullptr);
bool capital_softlock_fix_enabled();
int my_capital_specialty();
int foreign_city_count(void* civ);

// Host ownership sync (replaces Together mode's host_assign_cities while Civ Rivals is active)
void host_sync_owners();
// Host wealth deduction/credit for a player's own cCivilization
float civ_money(void* civ);
bool host_spend_civ_money(void* civ, float cost);
bool host_charge_player(uint8_t pid, float amount);
void host_credit_player(uint8_t pid, float amount);
void host_mark_assign_dirty();
void host_broadcast_assign_now();
void host_finalize_sw_spend(uint8_t from, void* civ, float before_wealth, float cost);
void record_sw_refused_funds();
void on_city_specialty_changed(void* city, void* civ, int new_spec);
void note_local_spend(void* civ, float amount);

// RAII guard: temporarily lends host player civ's superweapon tool (+0x470) vector and a per-civ isolated
// timer (+0x490) buffer to an NPC-origin guest civ (NPC civs have mIsPlayer==0 so 0x00BF7020 never populates
// +0x470, crashing at 0x00BF18CA; timers are isolated per civ so host and guest cooldowns never cross-contaminate).
struct SwToolsBorrow {
    void* civ = nullptr;
    uint32_t saved[3] = {};
    uint32_t saved_timers[3] = {};
    size_t mark = 0;
    size_t mark_timers = 0;
    bool active_borrow = false;
    bool active_timers_borrow = false;
    explicit SwToolsBorrow(void* target_civ);
    ~SwToolsBorrow();
    SwToolsBorrow(const SwToolsBorrow&) = delete;
    SwToolsBorrow& operator=(const SwToolsBorrow&) = delete;
};

// R7 & War/Neutral/Ally enforcement hooks called from civ.cpp / civ_shots.cpp / civ_host.cpp
civ_rivals_logic::CombatGate damage_gate(uint8_t attacker_pid, uint8_t victim_pid, bool is_capital, int dmg_type);
bool block_attack_order(uint8_t actor_pid, void* target);
bool block_capture(void* city, uint32_t new_pol);
bool block_capture_damage(void* city, uint8_t attacker_pid, uint32_t attacker_pol);
bool block_convert(void* city, uint32_t converter_pol);
bool block_superweapon(uint8_t attacker_pid, int sw_type, const float* target);
void on_city_captured(void* city, uint32_t new_pol);
void on_allied_trade(uint8_t a, uint8_t b, int gain);
void on_rel_apply(uint32_t polA, uint32_t polB, uint32_t event_id, float value);

// Vehicles across all player-assigned nations (for civ_own_designs.cpp)
std::vector<void*> all_player_vehicles();

// Underlying unswapped GetPlayerCivilization() pointer (keeps world_anchor stable across R1 swap)
const void* orig_player_civ_ptr();

// Console command handler (`coop.civ rivals ...`)
std::string debug(const std::vector<std::string>& args);

} // namespace civ_rivals
