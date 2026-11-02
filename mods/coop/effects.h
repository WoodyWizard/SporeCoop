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
};

void init(const SkHost* host, SkMod* mod, const Ctx& ctx);
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();
std::string status(); // counters and the last sent / received effect (coop.effects, coop.status)
bool set_log(bool on);
// tests: the game's own effect pools of `key` on the local avatar and every partner's puppet:
// `game=<pointer> active=<0|1>` for each (empty when the game symbols are missing)
std::string probe(uint32_t key);
std::string map_dump(); // tests: the effect pool maps of the avatar and the puppets (their keys and effect objects)

} // namespace effects
