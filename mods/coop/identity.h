// BUG-9135: a persistent identity per player, the key of a founder seat across rejoins, reloads and new network ids.
//
// Network player ids are reused (the lowest free one) and a custom creature's shared key contains that id, so with
// more than one partner neither tells the host which founder village is whose after a rejoin. Every game keeps one
// random 64-bit id per Spore profile (SporeCoop\identity.bin next to EditorSaves.package); a guest sends it to the
// host right after each join (MSG_IDENTITY, u64). A partner of an older version sends none: 0, the founders fall back
// to the creature match they always used.
#pragma once

#include <cstdint>
#include <string>

#include "net/buffer.h"
#include "net/session.h"
#include "sporekit.h"

namespace identity {

void init(const SkHost* host, SkMod* mod, net::Session* sess);
// this profile's identity (made and saved the first time); 0 while the profile's folder is not known yet
uint64_t own();
// host: the identity player `player` sent (own() for the host itself), 0 = not known
uint64_t of(uint8_t player);
// host: the player with this identity in the session now, 0xFF = none
uint8_t player_of(uint64_t id);
// BUG-9198: any machine: the identity of `player` as the host shared it (MSG_IDENTITY from the host: the table of the
// players here), own() for the own game; 0 = not known (an older host, or not arrived yet). of() stays host-only.
uint64_t known(uint8_t player);
// BUG-9198: a key for per-player lists that survives a rejoin under another network id or name: "id:<identity>",
// "name:<name>" while the identity is not known (or with `coop.fix identity.keys off`: always the name)
std::string key(uint8_t player);
void tick(double now);
void on_message(uint8_t from, net::Reader& r);
void on_player_left(uint8_t player);
void on_session_end();
std::string status();

} // namespace identity
