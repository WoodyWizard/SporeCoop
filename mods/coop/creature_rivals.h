// Creature Rivals runtime module (WP-R3 of 0.8.0, BUG-1547, docs/11-rivals.md 11.3.2, 11.4-11.9).
// Implements R1 (guest nest/herd perspective swap & minimap), R2 (host nest & family herd takeover by base distance),
// R4 (per-player NPC species relation tables & Creature influence), War-gated PvP + R7 home nest extinction guard,
// and R3/R6 (own DNA/brain/parts, Race progress, brain-level win -> catch-up -> Creature part draft & transition).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "creature_rivals_logic.h"
#include "net/session.h"
#include "sporekit.h"

namespace creature_rivals {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void*(uint8_t id)> puppet_of;
    std::function<void(const std::string&)> feed;
    std::function<void(const std::string&)> notice;
    std::function<bool()> russian;
};

void init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick(double now);

// Master switch (COOP_FIX "rivals.creature") and active state (Creature stage + Rivals mode + connected)
bool enabled();
bool active();

// Ownership & R7 home nest protection queries
uint8_t herd_owner(void* herd);                  // 0..3 or 255 (wild)
uint8_t creature_family_owner(void* creature);   // 0..3 if creature belongs to a player's home family herd, else 255
bool is_player_home_species(uint32_t species_inst);
void* player_herd(uint8_t player_id);            // live cHerd* for player's assigned nest (never avatar_herd for rival)
void note_proxy_family_owner(uint16_t proxy_id, uint8_t owner);
void forget_proxy_family_owner(uint16_t proxy_id);

// Per-player wild species relation & home species queries (used by social.cpp)
bool player_allied_with_species(uint8_t player_id, uint32_t species_inst);
uint32_t player_species_id(uint8_t player_id);
int32_t player_species_relation(uint8_t player_id, uint32_t species_inst);
bool is_species_extinct(uint32_t species_inst);

// Surgical hooks consumed by social.cpp, npc.cpp, nest.cpp, parts.cpp, own_progress.cpp, tribe.cpp
bool handle_host_rel_event(int event, const uint32_t key[3], void* creature, uint8_t actor);
bool on_guest_impress(uint8_t player, const uint32_t key[3], void* npc, float dna);
void on_host_impress(const uint32_t key[3], bool became_allied);
bool override_relationship(void* a, void* b, int& rel_out);
bool allow_pvp_damage(uint8_t attacker, uint8_t victim, bool is_family);
bool block_shared_parts();
bool use_own_progress();
bool bypass_nest_family_match();

// R3 native Creature -> Tribe transition helper (host, outside the frame)
bool trigger_native_tribe_transition();

// Diagnostics & test console command: coop.crivals
int command(void*, int argc, const char* const* argv, SkOut* out);
std::string status_summary();

} // namespace creature_rivals
