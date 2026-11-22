// BUG-9306 / owner decision 7Q A ("Own powers"): every player keeps the stage cards its own play earned.
//
// The game keeps one history per game: cPlayer +0x10FC.. (cell, creature, tribe, civ, space cards). wsync's "stages" row
// (BUG-0577) writes the host's into every guest, so a partner's own cards were lost and its powers followed the host's
// (Tribe chief powers 0xD1F400, Civ unlocks RefreshUnlocks 0xBF2510). Now a guest's game reads its own new card when its
// game writes one (a graduation: the field differs from what wsync last wrote) before wsync overwrites it, and sends it
// to the host (MSG_CARDS kind 0). The host keeps a record per player identity (identity.h) in the world's side file
// `<save>.duo-cards` and sends each guest its record back (kind 1). A founder without a record (graduated before this,
// or its card equal to the host's) keeps the shared cards. The users: tribe.partner_cards, civ.partner_unlocks.
#pragma once

#include <cstdint>
#include <string>

#include "net/buffer.h"
#include "net/session.h"
#include "sporekit.h"

namespace cards {

constexpr int kStages = 5; // 0 cell, 1 creature, 2 tribe, 3 civ, 4 space (cPlayer +0x10FC + 4 * i)
struct Own {
    uint32_t v[kStages] = {};
    uint8_t mask = 0; // bit i: stage i's own card is known
};

void init(const SkHost* host, SkMod* mod, net::Session* sess);
void tick(double now);
void on_message(uint8_t from, net::Reader& r);
void on_session_end();
// guest, wsync's "stages" row before it writes the host's card: this game's field for stage i and the host's value
void stage_check(int i, uint32_t mine, uint32_t want);
// this game's player's own cards (guest: caught or sent back by the host; host: none, its cPlayer is its own)
Own own();
// host: the record of the founder with this identity (mask 0: none)
Own of_identity(uint64_t id);
// stage by stage the own card where known, else this game's cPlayer card (the shared history)
void effective(const Own& o, uint32_t out[kStages]);
// the chief-power unlock word (player property 0x5FF7BAF) the game's 0xD1F400 derives from the cell and creature cards:
// bit (type - 0x20) for the chief power types 0x34..0x39
uint32_t tribe_power_bits(const uint32_t c[kStages]);
std::string line(const Own& o);
uint32_t generation(); // host: changes with every record change

} // namespace cards
