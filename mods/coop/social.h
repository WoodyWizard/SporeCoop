// Creature stage: social games of guests with NPCs (docs/re/creature.md 8, docs/09-coop-design.md).
//
// The host simulates the NPCs; a guest sees AI-less mirrors (npc.cpp). A social game is run by the
// NPC's own behaviour tree (node 0x341BD4BE "SocialPropose"), so a mirror never answered the
// guest. The guest's social actions on a mirror go to the host, the host counts them like the
// mini-game's rounds, applies the native result on the real NPC (impress event of the species
// relationship manager, DNA, the creature's "impressed" flag) and sends the outcome back. The
// species relationship table (impressed / killed per species, ally / extinct) is the host's and is
// mirrored to the guests, so the goal cards of both show the same.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "net/protocol.h"
#include "net/session.h"
#include "sporekit.h"

namespace social {
int creature_relationship(void* creature);

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;      // own notification
    std::function<void(const std::string&)> notice;    // everybody's notification
    std::function<void*(uint8_t id)> puppet_of;        // host: the creature of player `id`
    std::function<bool()> blocked;                     // a menu / the co-op window takes the keys
    std::function<float(float)> pace;                  // host: a gain's share of the party total (BUG-0510)
    std::function<bool()> elsewhere;                   // guest: plays another world of the host's stage (BUG-0420)
};

void init(const SkHost* host, SkMod* mod, const Ctx& ctx);
void tick(double now);
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();
// a DNA gain paid by a species goal card (allied / extinct): the host pays it once per species and
// outcome, whichever machine's card saw it; true = swallow it here
bool goal_dna(float delta);
// 6V A (host, a galaxy planet): the player whose action the game's relationship events of this moment are (a social
// win, a guest's hit): they go to that player's party's table. Returns the one before (-1 none: this game's own)
int set_event_actor(int player);
// 6W C (BUG-9270, host): the parties' relations written next to the planet's save (after a native save)
void save_party_relations();
std::string status();
// guest (BUG-0021): true when a hit of the own avatar on an NPC proxy must not be sent because the avatar is in the
// social stance (it plays a social game: a hit then killed the Kook it was impressing)
bool drop_social_hit(uint16_t proxy_id);
std::string species_dump(); // coop.social species (tests)
extern unsigned g_stage_msg_win; // 0x60B4123 (a social win) seen by the creature-stage handler (tests)
std::string games_dump(); // coop.social games (tests, BUG-0831): the host's social games, their prompts' animations
std::string meter_dump();
std::string cards_dump(); // tests (BUG-9244)
void debug_meter_fill(int done, int rounds, bool close); // tests (BUG-9230) // coop.social meter (tests, BUG-0179): the game's social meter object
bool debug_answer(uint16_t id, uint32_t type); // coop.social answer <npc id> <hex type> (tests, BUG-0525)
std::string abilities_dump(int id); // coop.social abil <npc id | -1> (tests, BUG-0525): abilities and prompt candidates
std::string relations_dump(); // coop.social rel (tests, BUG-0462): each NPC's relation to the own avatar as the game sees it
bool debug_send_goal(uint32_t crt_instance, int outcome, float dna); // guest, tests: a goal card's reward (K_GOAL)
bool debug_set_record(uint32_t crt_instance, int relation, uint32_t impressed); // host, tests: a record of the table
// host (roadmap phase 2, decision 7): a party species is allied (relation 6) in the host's table
bool set_party_ally(uint32_t crt_instance);
// host (BUG-9285, the nest refresh): an extinct species' record back to "not set" (relation 0, no kills): the
// archetype decides again and a new hunt has to start over
bool clear_extinct(uint32_t crt_instance);
bool debug_event(uint32_t crt_instance, int event);
bool debug_event_by(uint16_t npc_id, int event, int player); // 6V A tests: the event on host NPC #id as that player's action // host, tests (BUG-0168): the game's ApplyEvent(event, species, null)
// guest: a social action (ability slot 0-3) on proxy #id as if done in game; slot -1 toggles the
// stimulus log
bool debug_social(uint16_t npc_id, int slot);
std::string debug_post(void* c); // tests (BUG-0994): the avatar's social stimulus to creature c (the hooked game call)
void debug_meter_null(unsigned n); // tests (BUG-1058): the next n meter tasks find no meter window
bool debug_goto(uint16_t npc_id); // tests: the guest's avatar steps next to proxy #id
std::string pmode_dump(uint16_t npc_id, uint8_t player); // tests (BUG-9308): the social goal per player
std::string debug_objects(uint32_t vt_lo, uint32_t vt_hi); // game objects with a vtable in the range

} // namespace social
