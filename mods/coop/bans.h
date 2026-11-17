// Decision 5I A (owner 2026-11-17: "5IA - with kick and ban"): the host removes a player from its game (kick) or keeps
// them out of its games for good (ban). A ban keys on the player's identity (identity.h: a random id per Spore profile),
// so a new Spore profile gets round it: it stops casual trouble, not a determined cheater. The ban list is the host's own
// (SporeCoop\bans.txt next to identity.bin in the host's Spore profile). The host uses chat commands (/kick, /ban,
// /unban, /bans) or the console (coop.kick, coop.ban, coop.unban, coop.bans).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "net/session.h"
#include "sporekit.h"

namespace bans {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;   // a line in this player's own feed
    std::function<void(const std::string&)> notice; // a line in every player's feed
};

void init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick(double now);                     // host: a banned player whose identity arrived is removed
bool chat_command(const std::string& t);   // "/kick <player>", "/ban <player>", "/unban <name>", "/bans": true when handled
std::string kick(const std::string& who, bool ban); // host: a player by id or name; the result line
std::string unban(const std::string& who); // a name, an identity (hex) or a list number
std::string list();
std::string status(); // tests: the counters
bool banned(uint64_t identity);

} // namespace bans
