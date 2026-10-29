// M2 "pack": host-authoritative NPC creatures (docs/03-gameplay.md 3.3, docs/04-netcode.md 4.5).
//
// Host : streams NPCs near each guest (spawn/state/despawn), applies guests' hits.
// Guest: removes its own NPCs, mirrors the host's NPCs as AI-less proxies, forwards
//        hits on proxies to the host and takes damage the host reports.
#pragma once

#include "syncdbg.h"

#include <functional>
#include <string>

#include "net/session.h"
#include "sporekit.h"

namespace npc {

struct Ctx {
    net::Session* sess = nullptr;
    // creature that represents player `id` locally (host: guest puppet), or nullptr
    std::function<void*(uint8_t id)> puppet_of;
    // last known world position of player `id`; false if not in the world
    std::function<bool(uint8_t id, net::Vec3& pos)> player_pos;
    // the latest state of player `id` says it is dead (death caption, respawn)
    std::function<bool(uint8_t id)> player_dead;
    std::function<void(const std::string&)> feed;
    // guest: the host left the stage (main menu): the guest's own game simulates its NPCs meanwhile
    std::function<bool()> host_away;
    // RTS stages (no avatar): the shared centre (player tribe) all players are near
    std::function<bool(net::Vec3& pos)> stage_center;
    // host: player `id` plays our world (same stage and world); a guest in another world gets no
    // NPCs (BUG-0130). Unset = everybody.
    std::function<bool(uint8_t id)> in_place;
};

void init(const SkHost* host, SkMod* mod, const Ctx& ctx);
void tick(double now);                        // every frame (also when offline)
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();                        // destroy proxies, restore local NPCs
void on_world_change();                       // world unloaded: forget (not destroy) proxies
void on_player_leave(uint8_t id);             // host: the NPCs a player knew are not the next one's (BUG-0125)
void set_death_fix(bool on);                  // tests: off = a killed proxy does not play the game's death (BUG-0015, BUG-0148)
void set_stall_fix(bool on);                  // tests: off = no teleport of a proxy far off while the NPC reports a speed (BUG-0169)
void set_known_fix(bool on);                  // tests: off = only host_tick forgets them (BUG-0125)
bool is_proxy(void* creature);
// TakeDamage filter shared with coop.cpp; returns true to let the game apply damage
bool debug_host_hit(uint16_t id, float damage); // host avatar hits host NPC #id
bool debug_recruit(uint16_t id);
void* debug_spawn(uint32_t instance);           // host: an NPC of that crt species next to the avatar                 // host: NPC #id joins the pack (cPosseSimulator AddMember)
void* debug_spawn_epic(uint8_t player, float dist, uint32_t archetype, uint32_t species = 0); // host (tests): an epic near a player's creature
void* proxy(uint16_t id);                       // guest: local creature mirroring host NPC #id
bool damage_filter(void* creature, float& damage, int type, void* attacker);
std::string status();
std::string trace_line(const net::Vec3& me, float radius); // support trace: NPCs near `me`
std::vector<syncdbg::Entry> sync_entries(); // coop.sync (tests)
std::string list();                                  // debug: mirrored NPCs
std::string creatures(bool untuned_only, uint32_t species); // debug: creatures (+0xE84 = archetype tuning)
std::string herds(bool all = false);                 // debug: herd objects
void set_strays(bool on);                            // tests: removal of creatures like ours from saves
void set_hatch_help(bool on);
void set_kill_clear(bool on);                        // tests: off = keep the guest's kill list across skipped frames (BUG-0082)
std::string debug_reuse(uint16_t id);                // tests: a dead NPC's object is a living creature again (BUG-0044)
void set_reuse_fix(bool on);                         // tests: off = a reused object keeps the old id (BUG-0044)
void set_id_fix(bool on);                            // tests: off = ids wrap over ids in use (BUG-0023)
void debug_next_id(uint16_t id);                     // tests: the next id the allocator tries
void debug_kill_hold();                              // tests: put the avatar into the guest's kill list                        // tests: a waiting hatchling grows up without its family
// host: aggressive NPCs see a partner's puppet as a target like the host's avatar (on by default)
void set_partner_targets(bool on);
bool debug_attack_msg(uint16_t id);                  // tests: the game's attack message on host NPC `id`
bool debug_hit(uint16_t id, float damage);           // guest: avatar hits proxy `id`
bool debug_hurt(uint8_t player, uint16_t id, float damage); // host: NPC `id` hits player's puppet
void* host_npc(uint16_t id);     // host: NPC #id if it is alive this frame
int host_id(void* creature);     // host: net id of an NPC, -1
int proxy_id(void* creature);    // guest: net id of a proxy, -1
int proxy_id_of_ai(void* ai);    // guest: the proxy whose AI block (+0xB4C) this is, -1
void set_enabled(bool on);
bool enabled();

} // namespace npc
