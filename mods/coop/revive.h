// Revive a downed player in the Creature stage (WP-R1 of 0.7.17, BUG-1472; idea, rules and message flow from PR #214
// by IltonBJSilva). Rules and wire: revive_logic.h; design: docs/03-gameplay.md#gameplay-revive.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/buffer.h"
#include "net/protocol.h"
#include "net/session.h"
#include "sporekit.h"

namespace revive {

struct Ctx {
    net::Session* sess = nullptr;
    // every partner's latest state in this world (its creature in it, fresh): who may revive, who is down
    std::function<std::vector<net::PlayerState>()> peers;
    std::function<void*(uint8_t)> puppet_of;        // a partner's creature in this game (null: none)
    std::function<void(const std::string&)> notice; // shown to everybody
    std::function<void(const std::string&)> feed;   // shown here only
    // host: the price (revive_logic.h price_for)
    std::function<int()> dna_mode;                        // own_progress::dna_mode(): 0 own wallet, 1 shared, 2 own progress
    std::function<bool(uint8_t)> has_wallet;              // guest `id` has its own wallet now (dna.cpp dna_own_wallet)
    std::function<float(uint8_t)> wallet;                 // its budget (dna_own_budget)
    std::function<void(uint8_t, float)> charge_wallet;    // its spend (dna_own_spend, d < 0)
};
void init(const SkHost* host, SkMod* mod, Ctx ctx);

// creature.revive on (the session setting) and the switch: a lethal hit may down the own creature
bool on();
// teamplay::on_own_damage, before the pack rescue: the own creature is down or was just revived: true = the hit is
// dropped (damage set to 0) (creature.revive_hold)
bool guard(void* avatar, float& damage);
// teamplay::on_own_damage, after the pack rescue did not take it: true = this lethal hit downed the creature (damage 0)
bool down_now(void* avatar, float& damage);
void tick(double now);                      // every frame
void on_message(uint8_t from, net::Reader& r); // MSG_RESCUE
void on_player_leave(uint8_t id);           // a player left (session_glue: its window ends without a price)
bool down();                                // this game's own creature is down
bool pending();                             // a window is open (no world save then)
// a partner's creature is down here: the seconds it has left (0: not down)
float seconds_left_of(uint8_t player);
// every downed creature this game shows (its own and the partners' puppets): its player, the seconds left, the revive
void for_each_downed(void (*fn)(void* body, uint8_t player, float seconds, float revive, void* user), void* user);
std::string command(const std::vector<std::string>& args); // coop.revive [down | giveup]

} // namespace revive
