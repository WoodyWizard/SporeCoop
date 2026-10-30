// The view centres of all players (BUG-0280, docs/re/creature.md#creature-view-centres).
//
// The game simulates its world only around one point: the local camera. In the Creature stage the
// relevance pass (SporeCoop::Relevance_Update, every 5 s) enables the herds and spawn points within
// 100 m of that point and sends creatures farther than 250 m back to the creature pool; a guest far
// from the host had no NPCs around it. The host keeps a list of the partners' view centres (their
// last positions in the host's own place) and the stage hooks add them to the game's own list, so
// the world is simulated around every player, at the game's own radii (a bounded cost: one more
// sphere per partner, 2-4 players).
//
// The registry is stage-neutral: coop.cpp notes a partner's position whenever its puppet is shown
// in the host's place (any stage; in Tribe the partner's pointer), the stage code asks for the
// centres of its mode. The hook of the Creature and Tribe stages lives in view_centres.cpp; the Cell stage builds the same idea for its tiles. For the
// 0.8 Rivals mode (docs/11-rivals.md) every player plays its own nest far from the others: the
// same list is what keeps their areas alive on the host.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "net/protocol.h"
#include "sporekit.h"

namespace views {

struct Centre {
    net::Vec3 pos;
    uint8_t player = 0;
    uint32_t mode = 0; // the game mode the position was seen in
    double at = 0;     // when (H->time_seconds)
};

bool init(const SkHost* host, SkMod* mod);
// host: player `id` is at `pos` in our place, in game mode `mode`
void note(uint8_t id, const net::Vec3& pos, uint32_t mode, double now);
void forget(uint8_t id);
void clear(); // the session ended
// the partners' centres seen in `mode` within the last `max_age` seconds; returns how many were
// written to `out` (at most `max`)
size_t partners(uint32_t mode, double now, Centre* out, size_t max, double max_age = 3.0);
bool keep_alive_on(); // switch npc.view_centres
std::string status(); // coop.views

} // namespace views
