// Creature visual effects, shown on the partner's screen (BUG-0451,
// docs/re/creature.md#creature-effects-relay).
//
// A player's own creature makes effects the game creates for the local avatar only: mating-call
// hearts, the social abilities (sing, dance, charm, pose), attack and ability effects, eating,
// level-up, ... Every one of them is created by the game's two effect pools of cCreatureBase
// (game.cpp hooks the creators and the stop). effects.cpp sends the ones made for the local
// avatar to the partners and plays the received ones on that partner's puppet with the same game
// functions, so nothing here is a per-effect list.
//
// `coop.fix creature.effects_relay off`: the old behaviour (nothing sent or played).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace effects {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void*(uint8_t id)> puppet_of; // the creature of player `id` on this game
    std::function<std::vector<std::pair<uint8_t, void*>>()> all_puppets; // every partner's puppet
    // true when a world position is on the local player's own controlled object (the avatar in
    // Creature / Cell, the tribe members in Tribe, the vehicles in Civ, the UFO in Space)
    std::function<bool(const net::Vec3&)> near_local;
    // where that object is now (the perf workload `coop.effects load` creates effects there)
    std::function<net::Vec3()> local_pos;
    // the world being played (0 = none): a change means the game freed the relayed effects
    std::function<uint32_t()> world;
    // BUG-0471: player `id` plays in this game's place right now (its state: same stage and world,
    // in the world); `anyone_here`: some partner does (an effect nobody can see is not sent)
    std::function<bool(uint8_t id)> here;
    std::function<bool()> anyone_here;
};

void init(const SkHost* host, SkMod* mod, const Ctx& ctx);
void tick(double now); // main thread: send the effects the hooks queued
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();
std::string status(); // counters and the last sent / received effect (coop.effects, coop.status)
bool set_log(bool on);
// the frame-time workload (`coop.effects load`): `n` effects every `period_ms` at the local player,
// created through the game's effect manager like an effect of the game's own code. 0 stops it.
void set_load(uint32_t n, uint32_t period_ms, uint32_t id);
// tests: the game's own effect pools of `key` on the local avatar and every partner's puppet:
// `game=<pointer> active=<0|1>` for each (empty when the game symbols are missing)
std::string probe(uint32_t key);
std::string map_dump(); // tests: the effect pool maps of the avatar and the puppets (their keys and effect objects)
// tests: feed a crafted MSG_EFFECT through on_message as if it came from peer `from` (spoofing and
// validation checks: the transport cannot make a guest claim another player)
void inject(uint8_t from, uint8_t player, uint8_t stop, uint8_t pool, uint32_t id, uint32_t key,
            const net::Vec3& pos);

} // namespace effects
