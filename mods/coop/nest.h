// Eggs at the creature stage's nests between players (BUG-0384, docs/re/creature.md#creature-nest-eggs).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "net/session.h"
#include "sporekit.h"

namespace nest {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<bool(uint8_t id, net::Vec3& pos)> player_pos; // host: where guest `id` is (false: not in the world)
    std::function<bool(uint8_t id)> in_place;                   // host: guest `id` plays our world
    std::function<bool()> host_away;                            // guest: its own game runs its NPCs
};

void init(const SkHost* host, SkMod* mod, const Ctx& ctx);
void tick(double now);                                // every frame
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();
void on_world_change();
std::string status();

} // namespace nest
