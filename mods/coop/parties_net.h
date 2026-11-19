// Decision 6J: parties of 4, the game side (parties.cpp). MSG_PARTY (reliable), u8 kind:
//   kTable  host -> guests: u8 n, n x {u16 party id, u8 leader, u8 k, k x u8 member (the longest standing first)}
//   kInvite / kAnswer / kLeave / kLead  guest -> host: u8 the other player, u8 flag (kAnswer: 1 yes)
//   kOffer  host -> the invited player: u8 who invites
//   kResult host -> the asking player: u8 the request's kind, u8 the other player, u8 parties::Result
//   kGxParty guest -> host (BUG-9266, a galaxy planet): u16 the galaxy server's id of the guest's party (0 none), sent
//            at the session's start and on every change: the host scales difficulty by each party's size
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/session.h"
#include "parties.h"
#include "sporekit.h"

namespace parties {

enum Kind : uint8_t { kTable = 0, kInvite = 1, kAnswer = 2, kLeave = 3, kLead = 4, kOffer = 5, kResult = 6, kKick = 7, kGxParty = 8 };

struct Ctx {
    net::Session* sess = nullptr;
    std::function<uint32_t(uint8_t id)> stage_of;      // host: the game mode the player plays (0 unknown)
    std::function<bool()> galaxy;                      // host: a galaxy planet (players start alone)
    std::function<std::string(uint8_t id)> name_of;
    std::function<void(const std::string&)> feed;
    std::function<void()> changed;                     // the book or the invite changed (the UI)
    // 6K: on a galaxy server the server keeps the parties (net/galaxy.h PARTY): this game's own party comes from it
    std::function<bool()> galaxy_welcomed;             // connected to a galaxy server
    std::function<uint16_t()> galaxy_slot;             // this game's slot there
    std::function<bool(uint8_t kind, uint64_t other, uint8_t flag, uint32_t stage)> galaxy_send;
    std::function<void()> galaxy_who;                  // ask for the players online
    std::function<uint64_t(uint8_t id)> identity_of;   // a session player's identity (0 unknown; the own game's too)
};

// a member of this game's party as the galaxy server sends it (6K)
struct Member {
    uint16_t slot = 0;
    uint64_t identity = 0;
    std::string name, planet;
    uint32_t stage = 0;
    bool leader = false, me = false;
};
// a player online on the galaxy server (the galaxy menu's list, 6K)
struct Online {
    uint16_t slot = 0, party = 0;
    uint64_t identity = 0;
    std::string name, planet;
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

int pending_invite(); // who invited this game (-1 none; on a galaxy: its slot)
std::string pending_name(); // who invited this game ("" none)
bool active();         // parties are in use (a galaxy server's, or this session's)
bool galaxy();         // the galaxy server keeps them (6K)
std::string kick(uint8_t id);            // the leader takes a member out (6K)
std::string invite_identity(uint64_t id); // a player of the galaxy (the galaxy menu: maybe not in this session)
std::string kick_identity(uint64_t id);
std::string lead_identity(uint64_t id);
std::vector<Member> members();           // this game's party, the longest standing first (on a galaxy: anywhere)
std::vector<Online> online();            // the galaxy's players (after ask_online)
void ask_online();
void on_galaxy(uint8_t type, net::Reader& r); // PARTYVIEW / PARTYNOTE / PLAYERS
const Book& book();
bool same_party(uint8_t a, uint8_t b);
std::vector<uint8_t> my_party(); // this game's party (the longest standing first)
// BUG-9266: the players of this session in player p's party, p included (1: alone). A guest knows its own party only
// (any other player: 1); on a galaxy planet the host counts the parties its guests report (kGxParty)
unsigned party_size(uint8_t p);
unsigned largest_party(); // the biggest party among this session's players (a guest: its own)
// 6V A (host, a galaxy planet): which relations table player p's party keeps: 0 this game's own party (the game's
// table), else the party's galaxy id, 0x10000 | p for a player in no party
uint32_t table_key(uint8_t p);
uint64_t identity(uint8_t p); // a session player's identity (0 unknown)
int leader_of(uint8_t p);
bool i_lead();
std::string status();

} // namespace parties
