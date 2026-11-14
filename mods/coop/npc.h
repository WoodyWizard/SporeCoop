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
    // BUG-0426 class (L1): player id's Creature puppet only while it is the live object made for it (listed, its serial,
    // this frame's world); a Cell cell, a Tribe marker, a freed or reused one: null
    std::function<void*(uint8_t id)> puppet_live;
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
    // guest: the host plays in this guest's world now (its latest state: same place, in the world). Unset = yes.
    std::function<bool()> host_here;
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
bool debug_host_hit(uint16_t id, float damage, float push = 0); // host avatar hits host NPC #id (push: thrown, tests)
bool debug_recruit(uint16_t id);
std::string debug_ghost(uint16_t id); // tests (BUG-0931)
void* debug_spawn(uint32_t instance);           // host: an NPC of that crt species next to the avatar                 // host: NPC #id joins the pack (cPosseSimulator AddMember)
void* debug_spawn_epic(uint8_t player, float dist, uint32_t archetype, uint32_t species = 0); // host (tests): an epic near a player's creature
void* proxy(uint16_t id);                       // guest: local creature mirroring host NPC #id
bool damage_filter(void* creature, float& damage, int type, void* attacker);
std::string status();
std::string trace_line(const net::Vec3& me, float radius); // support trace: NPCs near `me`
// the invariant monitor (docs/06-testing.md#testing-monitor), guest: the proxies as drawn, sampled at the
// start of the mod's frame (4 times a second); fields " proxy_under=<n> proxy_stuck=<n>" for the own
// trace line: proxies under this game's ground for 1 s / over 3 m off the host's NPC for 2 s since the
// last line ("" on the host and outside Creature / Tribe)
void monitor_tick(double now);
std::string monitor_fields();
std::vector<syncdbg::Entry> sync_entries(); // coop.sync (tests)
bool entries_listed(); // BUG-0919: npc.entries_listed (the synced entries ask the game's object list)
// diagnostics (coop.bodyrec start diag, BUG-0363), guest: per NPC proxy a row `q<id>`, the interpolation target the
// driving of the frame before used (the drawn proxy of this frame is that frame's driving plus the game's update)
// and `u<id>`, M1's raw lead target now (no drive.lead_late time, no smoothing: what the target is without them)
void for_each_diag(void (*fn)(const char* key, const net::Vec3& p, void* user), void* user);
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
bool proxy_in_host_pack(void* c); // guest: proxy c mirrors a member of the host's pack (BUG-0556)
uint16_t id_of(void* c);         // host: the net id of NPC c, guest: of proxy c (0xFFFF: none)
void* host_npc(uint16_t id);     // host: NPC #id if it is alive this frame
std::string debug_pool(uint16_t id); // host (tests, BUG-0339): NPC #id to the game's creature pool
std::string debug_nanvel(float secs, int id, bool air); // tests (BUG-0894)
std::string debug_rxkeep(int n, float secs); // guest (tests, BUG-0611): only the first n NPCs of each state batch for secs
std::string debug_lift(uint16_t id, float m, float secs); // host (tests, BUG-0550): NPC #id held m over its ground for secs s
std::string debug_sink(uint16_t id, float m, bool mark = false); // guest (tests, BUG-0399): the proxy m under this game's ground once
std::string debug_pwalk(uint16_t id, const net::Vec3& p, float speed, float secs, int mode = 0); // guest (tests, BUG-0281)
std::string debug_bhv(uint16_t id, uint32_t anim); // host (tests, BUG-0440): NPC #id plays a behaviour animation, relayed
std::string debug_pushback(uint16_t id, float mps, float secs); // guest (tests, BUG-0440): the proxy moved back in its updates
std::string debug_raised(float r, const net::Vec3* at = nullptr); // tests (BUG-0440): nests near the avatar and the raised ground there
std::string debug_dash(uint16_t id, float len, float speed, float secs, float width, uint16_t mate = 0, float off = 0,
                       const net::Vec3* to = nullptr, float vel_k = 1.f); // host (tests, BUG-0305): NPC #id dashes back and forth
int host_id(void* creature);     // host: net id of an NPC, -1
int proxy_id(void* creature);    // guest: net id of a proxy, -1
bool test_hit_now();             // guest: a test's hit (coop.npc hit) is being applied (BUG-0021 social.no_bite)
int proxy_id_of_ai(void* ai);    // guest: the proxy whose AI block (+0xB4C) this is, -1
uint32_t ai_lookup_gone();       // BUG-0994 (tests): proxies proxy_id_of_ai skipped as gone
void set_enabled(bool on);
bool enabled();

// tests (BUG-0487): the guest forgets a proxy without telling the host; the resync's counters
std::string debug_forget(uint16_t id);
// guest (tests, BUG-0850): the game destroys proxy <id> by itself (DestroyInstance, no bookkeeping of ours), then the
// proxy is dropped before the next npc tick (as a despawn or the stale drop in the same frame); counts DestroyInstance
std::string debug_gamekill(uint16_t id);
std::string debug_settle(double s); // tests (BUG-0842): the guest's npc tick waits s seconds, as after an avatar change
// tests (BUG-0426 class): a freed address given to a new creature (npc.cpp ReuseHunt); proxy <id>'s creature
std::string debug_chunk(void* obj); // tests (BUG-1099): the heap chunk of an object and its neighbours' use
void debug_reuse_free_first(int frames); // tests (BUG-1099): no try until the target's heap chunk is free (at most N frames)
void debug_reuse_wait_free(int tries); // tests (BUG-0825): the hunt ends N tries after the target's heap chunk is free
std::string debug_reuse_hunt(void* target, int frames, void* species, int now_tries = 0, int burst = 1, void* herd = nullptr,
                             const net::Vec3* at = nullptr, bool herds = false, bool pool = false, bool keep = false, int every = 1);
std::string debug_reuse_status();
void* debug_proxy_creature(uint16_t id);
uint32_t handler_dropped(); // BUG-0805: handler calls that met a proxy whose creature is not the live one it made
std::string resync_status();
std::string tuning_status();
void set_anim_quiet(float s); // tests: the snapshot waits this long after a start (BUG-0464)
std::string anims_status(); // BUG-0464: behaviour animations relayed / played on proxies
std::string current_anims(); // coop.npc cur (tests): each NPC's current animation
std::string debug_untune(uint16_t id); // guest (tests): proxy <id> without tuning, as Create made it in a herd of ours // BUG-0462: proxies given the host NPC's archetype tuning

} // namespace npc
