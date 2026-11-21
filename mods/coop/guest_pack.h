// BUG-9129: a guest's own pack, run by the host. The game has one pack (cPosseSimulator, the avatar's posse); in
// a session the host owns every NPC, so a guest's Summon Flock and recruiting made nothing. The host now keeps a
// pack per guest: its game's creatures (summoned or recruited for that guest) stay leashed to the guest's puppet,
// attack what the guest attacks and defend it; the guest sees them as the party's (NF_PACK: allies).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "net/protocol.h"
#include "net/session.h"
#include "sporekit.h"

namespace guest_pack {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void*(uint8_t id)> puppet_of; // host: a guest's puppet
    std::function<void(const std::string&)> notice;
    std::function<uint32_t()> world; // the planet's key for its notes (worlds.ini), 0 = none (decision 5CD phase 3)
};

void init(const SkHost* host, SkMod* mod, Ctx ctx);
bool enabled(); // `coop.fix creature.guest_pack`

// host
int summon(uint8_t guest);              // the guest's Summon Flock: the game's flock, made that guest's (members)
bool recruit(uint8_t guest, void* npc); // the guest recruited host NPC `npc`
bool member(void* c);                   // in some guest's pack
int owner(void* c);                     // its guest, -1 none
void assist(uint8_t guest, void* victim);   // the guest hit `victim`: its pack attacks it
void defend(uint8_t guest, void* attacker); // `attacker` hit the guest's puppet: its pack attacks it
void tick(double now);                  // every frame (host)
void on_player_leave(uint8_t id);
void reset();                           // session end, world change
void set_herd_fix(bool on);             // tests
std::string status();

// decision 5CD: each partner's own pack (the game's pack manager, member list and leader swapped in)
bool party_mode();                        // `coop.fix creature.party_packs`
size_t party_enter_puppet(void* puppet);  // the game's pack is that puppet's player's until party_leave_token; 0: not entered
void party_leave_token(size_t token);
// host: the partner's own command to its creature (Creature_PlayerCommand's arguments, the target a host object or null)
// given to its puppet inside its pack, so the game passes it on to that pack as for the host's own
void pack_command(uint8_t guest, uint32_t lo, uint32_t hi, float value, uint32_t c, uint32_t d, void* target);
// phase 2: the host tells each partner which NPCs are its pack (MSG_PLAYER_RULE kind kPackListKind); the partner's game
// keeps those proxies in its own pack list (its Pack panel, names, the hint), as in single player
constexpr uint8_t kPackListKind = 14;
void host_command(void* target);  // tests: the host's own attack command at target (its click: SetTarget, PlayerCommand 0x10)
void set_follow_test(bool on);   // tests: the follow stimulus by hand instead of the walk back
void set_ctx_test(bool on);      // tests: off = members update outside their party
std::string callers(bool start); // tests: `coop.gpack callers start|stop` (return addresses of cPosseSimulator_Get)
void on_pack_list(const std::vector<std::pair<uint16_t, bool>>& ids); // guest
// decision 5EA: each partner's own game runs its pack (`coop.fix creature.pack_own_game`, both games)
constexpr uint8_t kPackStateKind = 15;  // guest -> host {u8 n, n x net::NpcState}: where its pack members are (10 Hz)
bool own_game();                        // the switch (and party_mode)
bool own_member(void* c);               // guest: c is a member of this player's pack that this game runs
void on_pack_states(uint8_t from, const std::vector<net::NpcState>& v); // host
std::string own_status();
constexpr uint8_t kPackAnimKind = 16;   // guest -> host {u8 n, n x {u16 id, u32 anim, u8 how, u8 a, u32 b}}: their animations
void own_anim(void* c, uint32_t anim, int how, int a, int b); // guest: a member this game runs started a behaviour animation
void on_pack_anims(uint8_t from, net::Reader& r);             // host: played on the members

// BUG-9321 (the party rings): every pack member this game knows with its player: the host its own pack (owner: the
// host) and each partner's (party lists and the guest pack); a guest its own pack's proxies (owner: this player).
// Pointers from the lists as they are: the caller checks game::listed (inside a game::LiveScope) before using one.
void for_each_member(uint8_t me, void (*fn)(void* c, uint8_t owner, void* user), void* user);

} // namespace guest_pack
