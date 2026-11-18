// Decision 6J: parties of 4, the game side (parties.cpp). MSG_PARTY (reliable), u8 kind:
//   kTable  host -> guests: u8 n, n x {u16 party id, u8 leader, u8 k, k x u8 member (the longest standing first)}
//   kInvite / kAnswer / kLeave / kLead  guest -> host: u8 the other player, u8 flag (kAnswer: 1 yes)
//   kOffer  host -> the invited player: u8 who invites
//   kResult host -> the asking player: u8 the request's kind, u8 the other player, u8 parties::Result
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/session.h"
#include "parties.h"
#include "sporekit.h"

namespace parties {

enum Kind : uint8_t { kTable = 0, kInvite = 1, kAnswer = 2, kLeave = 3, kLead = 4, kOffer = 5, kResult = 6 };

struct Ctx {
    net::Session* sess = nullptr;
    std::function<uint32_t(uint8_t id)> stage_of;      // host: the game mode the player plays (0 unknown)
    std::function<bool()> galaxy;                      // host: a galaxy planet (players start alone)
    std::function<std::string(uint8_t id)> name_of;
    std::function<void(const std::string&)> feed;
    std::function<void()> changed;                     // the book or the invite changed (the UI)
};

void init(const SkHost* host, SkMod* mod, Ctx ctx);
void on_session_start();
void on_session_end();
void on_player_join(uint8_t id);
void on_player_leave(uint8_t id);
void on_message(uint8_t from, net::Reader& r);
void tick(double now);
// requests (the host's own are applied at once; a guest's go to the host): "" sent, else why not
std::string invite(uint8_t to);
std::string answer(bool yes); // the pending invite
std::string leave();
std::string make_leader(uint8_t to);

int pending_invite(); // who invited this game (-1 none)
const Book& book();
bool same_party(uint8_t a, uint8_t b);
std::vector<uint8_t> my_party(); // this game's party (the longest standing first)
int leader_of(uint8_t p);
bool i_lead();
std::string status();

} // namespace parties
