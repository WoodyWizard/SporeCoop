// Space stage co-op, "Squadron" (docs/09-coop-design.md, RE notes docs/re/space.md):
//  - in the host's star system / planet the host simulates the NPC ships; guests that are
//    in the same place see mirrors of them (their own NPC ships there are removed)
//  - guest shots on mirrors are applied by the host; NPC shots that hit a guest's ship on
//    the host are applied to the guest's own ship; mirrors never fire on their own
#pragma once

#include "syncdbg.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "founders.h"
#include "net/session.h"
#include "sporekit.h"

namespace squadron {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<bool()> with_host;            // guest: the host is in our place
    std::function<int(void* ufo)> puppet_owner; // host: player id of a puppet ship, -1 if none
    std::function<void*(uint8_t id)> puppet_of; // host: the puppet ship of a player
    // BUG-9197: a player's place in Space (its state's star system or planet; this game's own for my id), 0 = not in
    // Space or unknown
    std::function<uint32_t(uint8_t id)> place_of;
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick(double now);
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();
void before_native_save();
void after_native_save();
void clear_enemy_stats();
std::string status();
// BUG-9199 (decision 2B A): the campaign rules with `seats` = the party that a Space fight scales for: `here` = the
// players in this game's place (system or planet), else the players online (a raid on the party's colony). Outside a
// session: 1 (the game's own values). `coop.fix space.party_difficulty off`: the reserved seats, as before.
founders::CampaignRules party_rules(bool here);
std::vector<syncdbg::Entry> sync_entries(); // coop.sync (tests)
std::string debug(const std::vector<std::string>& args);
// BUG-0378 (tests, coop.fleet track): every ship as this game drew it, sampled at the start of the
// mod's frame (after the game's update, before any write of ours), on the session clock
void frame_start(double session_t, bool clock_ok);
// BUG-0677: a guest's hit whose attacker (a combatant) is a mirror of a host ship: a copied shot, it hurts nothing
bool shot_is_cosmetic(void* attacker_combatant);

} // namespace squadron
