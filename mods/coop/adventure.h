// Co-op Galactic Adventures runtime module (WP-A of 0.7.99, BUG-1474 / BUG-0793):
//   - E-A (adventure.shared): host-authoritative run journal (goals, act changes, outcome, object deaths) + late-join replay
//   - E-B (adventure.host_actors, adventure.hit_forward, adventure.guest_extras, adventure.body_side):
//         host-simulated adventure creatures paired by (marker +0x18, class +0x1C) within 12 m on guest,
//         NPCTickAI suppressed only on paired guest creatures, kinetic driving + Die mirroring, hit forwarding,
//         5 s unpaired leftover removal, and partner captain puppet political ID +0x30 on host.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/buffer.h"
#include "net/session.h"
#include "sporekit.h"

namespace adventure {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void*(uint8_t)> puppet_of;        // live partner captain puppet on this game, or null
    std::function<void(const std::string&)> notice; // broadcast notice
    std::function<void(const std::string&)> feed;   // local feed line
};

void init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick(double now);
void on_message(uint8_t from, net::Reader& r);

// Called by game.cpp h_NPCTickAI: true suppresses NPCTickAI on paired guest adventure creatures while host stream runs.
bool should_skip_ai(void* creature);

// Called by npc.cpp ai_target: true allows host NPC AI to target partner captain puppets in kScenario.
bool host_ai_targets_partners();

// Called by npc.cpp damage_filter when in kScenario: returns false if damage was consumed/forwarded by adventure sync.
bool damage_filter(void* creature, float& damage, int type, void* attacker);

// Console command handler for `coop.adventure` / `coop.adv`
std::string command(const std::vector<std::string>& args);

} // namespace adventure
