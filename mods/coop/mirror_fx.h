// The sounds and effects a mirror's own game never makes (BUG-0600, docs/re/tribe.md#tribe-mirror-fx).
//
// The host's game runs the NPCs and the tribe members: their behaviour trees start the fight's effects on the
// creature (the game's effect pools of cCreatureBase: a tool's swing, the chief's aura, the sprint's dust, ...;
// each effect plays its own sounds), switch the creature's sprint on and off (its Update then makes the sprint
// effect), and its combat voice (the growl when it goes into a fight, the sigh when it leaves). A guest's mirrors
// (tribe members, npc.cpp proxies) run no tree: their game never made any of them, so a guest heard none of the
// fight's sounds. This module is the one relay of those calls: the host sends what the game did to a mirrored
// creature, a guest's game makes the same call on its mirror (the game's own function: the guest's sound
// settings, the mirror's position). MSG_MIRROR_FX; `coop.fix mirror.fx off`: nothing sent or played (0.7.7).
#pragma once

#include <cstdint>
#include <string>

#include "game.h"
#include "net/protocol.h"
#include "net/session.h"
#include "sporekit.h"

namespace mirror_fx {

struct Ctx {
    net::Session* sess = nullptr;
};

void init(const SkHost* host, SkMod* mod, Ctx ctx);
// effects.cpp (main thread, its tick): a creature's pool effect start / stop the game made, not the own avatar's
void on_creature_effect(void* creature, const game::EffectEvent& ev);
void on_message(uint8_t type, uint8_t from, net::Reader& r); // MSG_MIRROR_FX
std::string status();

} // namespace mirror_fx
