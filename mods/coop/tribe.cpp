#include "tribe.h"
#include "tribe_internal.h"
#include "party_difficulty.h"
#include "tribe_logic.h"
#include "call_guard.h"
#include "lifecycle.h"
#include "mirror_registry.h"
#include "host_busy.h"
#include "busy_model.h"
#include "world_id.h"
#include "player_life.h"

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "civ.h"
#include "coop_ui.h"
#include "creations.h"
#include "driver.h"
#include "effects.h"
#include "game.h"
#include "modes.h"
#include "npc.h"
#include "savestrip.h"
#include "space_ext.h"
#include "stage_epoch.h"
#include "world.h"
#include "owner_claims.h"
#include "own_world.h"
#include "autosave_glue.h"
#include "store.h"
#include "wsync.h"
#include "wire/tribe_anim.h"
#include "net/held_cmds.h"
#include "net/ids.h"
#include "net/interp.h"
#include "net/snapshot_rx.h"
#include "net/validate.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "settings.h"
#include "fields.h"

using skg::thiscall;
using skg::vcall;

namespace tribe {

const SkHost* H;
SkMod* M;
Ctx C;
bool g_ok;

constexpr uintptr_t kTribePosOff = 0x124;  // cTribe spatial position
static uintptr_t a_CitizenVt; // Simulator::cCreatureCitizen::vftable
constexpr uint32_t kTreeNoun = 0x6ABEE7E;    // cTreeData (planet flora: fruit trees), not a cGameData
static uintptr_t a_TreeVt;    // Simulator::cTreeData::vftable (main vtable)

uintptr_t a_GetPlayerTribe, a_DoAction, a_SetValueVector3, a_BTTick, a_DoActionForObjects, a_WalkSelected,
    a_SetSelected, a_CreateCitizen, a_AddMember, a_SpawnNpcTribe, a_MaxPop;

bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
bool active() { return g_ok && C.sess && C.sess->connected() && game::mode_id() == modes::kTribe; }
// BUG-0151: while the host is away (main menu) nothing simulates or orders the guest's members (the
// host sends no states and drops orders outside Tribe): the guest's own game runs their AI and its
// orders until the host is back. `coop.tribe fix away 0`: the old frozen tribe (tests).
bool g_fix_away = true;
COOP_FIX("tribe.away", "BUG-0151", g_fix_away, "tribe: while the host is away the guest's own game runs its members");
static uint32_t g_away_orders;
bool local_play() { return g_fix_away && is_guest() && C.host_away && C.host_away(); }

// C7 of 0.7.15 (BUG-1160, wire/tribe_anim.h): the members' animations, holds, air height, the theme and party music and
// the game-over / pause waits go to a guest as MSG_TRIBE_ANIM when its HELLO listed the tribe family at minor 1 or later,
// else (0.7.14 and before, a 0.7.15 build before it) as MSG_NPC_ANIM's sub-kinds, as before: the same bytes after the
// type. Off (`coop.fix tribe.anim_msg off`): MSG_NPC_ANIM to everyone (0.7.14).
static bool g_anim_msg_fix = true;
COOP_FIX("tribe.anim_msg", "BUG-1160", g_anim_msg_fix,
         "tribe: the members' animations reach a guest of 0.7.15 or later as their own message type (MSG_NPC_ANIM for an older guest)");
static uint32_t g_anim_sent_new, g_anim_sent_old, g_anim_rx_new, g_anim_rx_bad; // (status: tribe anim msg)
void anim_send(uint8_t to, const net::Writer& w, bool reliable)
{
    bool has = false;
    std::vector<net::FamilyRev> fam;
    uint8_t type = C.sess->peer_families(to, has, fam) ? wire::tribe_anim::type_for(has, fam, g_anim_msg_fix) : net::MSG_NPC_ANIM;
    (type == net::MSG_TRIBE_ANIM ? g_anim_sent_new : g_anim_sent_old)++;
    if (reliable)
        C.sess->send_reliable_to(to, type, w);
    else
        C.sess->send_unreliable_to(to, type, w);
}

void* player_tribe()
{
    void* nm = game::noun_manager();
    return nm ? thiscall<void*>(a_GetPlayerTribe, nm) : nullptr;
}

bool is_citizen(void* c) { return c && !IsBadReadPtr(c, 8) && *(uintptr_t*)c == a_CitizenVt; }

std::vector<void*> members(void* t)
{
    std::vector<void*> v;
    void** b = F<void**>(t, kMembersOff);
    void** e = F<void**>(t, kMembersOff + 4);
    for (void** it = b; it && it < e && v.size() < 256; ++it)
        if (*it && game::object_alive(*it))
            v.push_back(*it);
    return v;
}

bool stage_end()
{
    // cTribeUI +0x28 / +0x29 (the listener's flags) read 0 back in Tribe after the City Hall (lab 20261105-123325):
    // the game's own sign is the player tribe without its food mat (+0x260 = 0, BUG-0585), which a tribe has from its
    // founding to the City Hall
    if (!g_ok || game::mode_id() != modes::kTribe)
        return false;
    void* t = player_tribe();
    return t && !IsBadReadPtr((uint8_t*)t + 0x260, 4) && F<void*>(t, 0x260) == nullptr && !members(t).empty();
}

std::vector<void*> all_tribes()
{
    std::vector<void*> v;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            if (noun == kTribeNoun && game::object_alive(o))
                ((std::vector<void*>*)u)->push_back(o);
            return true;
        },
        &v);
    return v;
}

net::Vec3 tribe_pos(void* t)
{
    auto& p = F<float[3]>(t, kTribePosOff);
    return {p[0], p[1], p[2]};
}

// ---- planet trees (docs/re/tribe.md, "Trees"): the flora object keeps a sorted vector of patches,
// each patch has three fixed_vectors of intrusive_ptr<cTreeData>; a tree's position is at +0x10.
// Gather orders on a fruit tree name a cTreeData, which is no cGameData: it is found by position.
bool is_tree(void* t) { return t && !IsBadReadPtr(t, 0x20) && *(uintptr_t*)t == a_TreeVt; }

void* find_tree(const net::Vec3& pos, float max_dist)
{
    void* best = nullptr;
    float bd = max_dist;
    for_each_tree([&](void* t, const net::Vec3& p) {
        float d = net::dist(p, pos);
        if (d < bd) {
            bd = d;
            best = t;
        }
    });
    return best;
}

void for_each_tribe_center(void (*fn)(const std::string& key, const net::Vec3& pos, void* user), void* user)
{
    if (!g_ok)
        return;
    void* pt = player_tribe();
    auto tribes = all_tribes();
    for (size_t i = 0; i < tribes.size(); i++)
        fn("tribe" + std::to_string(i) + (tribes[i] == pt ? "p" : ""), tribe_pos(tribes[i]), user);
}

void for_each_tree_pos(void (*fn)(const net::Vec3& pos, bool fruit, void* user), void* user)
{
    if (g_ok) // +0x48: the fruit left (bits, +0x4C = all slots ~): a tree without fruit is no gather target
        for_each_tree([&](void* t, const net::Vec3& p) { fn(p, F<uint32_t>(t, 0x48) != 0, user); });
}

// BUG-0951 (class BUG-0426): pointers the Tribe code keeps across frames were checked by their own memory (object_alive):
// the host's member table (a new citizen at a freed member's address took its id and its owner), the target ring of a
// guest's attack order (kept up to 120 s: drawn under whatever the game built at the address), the epic animal, the
// babies the host's game made. Now each keeps the object's serial and is used only while it is that object in the
// game's list (game::alive). Old (`coop.fix tribe.kept_refs off`): object_alive.
bool g_kept_refs_fix = true;
COOP_FIX("tribe.kept_refs", "BUG-0951", g_kept_refs_fix, "tribe: the attack ring, the epic animal, the babies and the host's member table never follow another object the game built where theirs was");
uint32_t g_kept_stale;
bool kept_alive(void* o, uint32_t serial, uint32_t noun)
{
    if (!o)
        return false;
    if (!g_kept_refs_fix)
        return game::object_alive(o);
    if (game::alive(game::LiveRef{o, serial, noun}))
        return true;
    g_kept_stale++;
    return false;
}
// BUG-0620 (docs/re/tribe.md#tribe-member-body): a member that dies for good leaves its tribe's member list
// (+0x1014 = 0) while the game keeps its body (the dead flag +0xB5E, in the object list) for 10-20 s more; the host's
// snapshot dropped it at once and the guest removed its mirror: the body lay on the host only (the detector's
// `Tribe:unmatched:citizen`). The host sends the body with its last tribe (health 0: the guest's mirror plays the
// death, tribe.ko) while the game keeps it, at most kBodyMax s (BUG-0620).
// BUG-0665 (docs/archive/known-issues-2026.md#issue-tribe-join-body): a guest made no mirror for a body entry, so a guest that joined
// or loaded the world while a dead member's body lay on the host had none (its own loaded member of it, alive in the
// shared save, was removed at the end of the cycle as unknown). With tribe.join_body a body entry without a mirror binds
// or spawns one as a live entry does; guest_tick then plays its death (tribe.ko) and takes it out of its tribe
// (detach_body) as for a member that died while the guest was there. Off: no mirror for a body (0.7.10).
bool g_join_body_fix = true;
COOP_FIX("tribe.join_body", "BUG-0665", g_join_body_fix,
         "tribe: a guest that joins while a dead member's body lies on the host gets a mirror of that body too");
uint32_t g_join_bodies;
// guest: mirrors taken out of their tribe as bodies (destroyed on removal) -> the tribe they were taken out of
std::unordered_map<void*, void*> g_detached;
uintptr_t a_BTTerminate; // BUG-0667: SporeCoop::Citizen_BTTerminate (optional)
static uint32_t g_bodies_detached;
// BUG-0667 (docs/re/tribe.md#tribe-body-teardown): a detached body's mirror was destroyed (DestroyInstance) with its
// +0x1014 cleared. The citizen's teardown (0xC1FFE0) shuts its behaviour tree down, and the exit of a node still active
// there reads the tribe: the eat node 0x0461300F's exit 0xDB0C40 releases its food mat spot through
// cTribe_ReleaseSlot(citizen+0x1014 = null) and faulted in cTribe_ToolLayout's map lookup (0xAC9C7D, data 0x39C). A
// mirror's tree runs only while the host is away (local_play), so the node stayed active from then until the destroy
// (long_tribe on rel/0.7.9, the guest froze after the fault). The game's own RemoveMember destroys a member with its
// tribe still set. The body gets its tribe back right before it is destroyed (when that tribe still exists), and a
// slot release with no tribe is skipped (BUG-0667).
uint32_t g_teardown_restored, g_slot_null_skipped;
// guest: the mirror leaves its tribe's member and selectable lists as the host's body left its own (not selectable, not
// counted as a member), the object stays until the host's body goes
// BUG-0961: the host's game takes a dead member's card off the roster (the right edge) through the stage's message
// listener (0xCD81F0 -> 0xCD51F0: cast to a citizen, the roster's card removal 0xCCBCE0 on the stage's game UI +0x240);
// a guest's mirror taken out of its tribe here (detach_body) kept its card: the guest's roster showed one member more
// than its tribe (and the host's) had for as long as the play went on (play_tribe's end frame). The card goes with
// the body now, as on the host. Old (`coop.fix tribe.body_card off`): the card stays.
static bool g_body_card_fix = true;
COOP_FIX("tribe.body_card", "BUG-0961", g_body_card_fix,
         "tribe: a member that died on the host leaves the guest's roster too (one card more stayed there)");
static uint32_t g_body_cards;
static void body_card_off(void* c)
{
    static uintptr_t get = H->sym("SporeCoop::cTribeUI_Get"), off = H->sym("SporeCoop::TribeStage_RemoveMemberCard");
    void* stage = get ? skg::cdecl_call<void*>(get) : nullptr;
    if (!g_body_card_fix || !stage || !off || !skg::readable(stage, 0x244) || !F<void*>(stage, 0x240))
        return;
    thiscall<void>(off, stage, c);
    g_body_cards++;
}

void detach_body(void* c)
{
    void* t = F<void*>(c, 0x1014);
    if (!t || g_detached.count(c))
        return;
    body_card_off(c); // BUG-0961 (while it is still the tribe's member, as the game's message comes)
    for (uintptr_t off : {(uintptr_t)0x340, (uintptr_t)0x354}) {
        void**& b = F<void**>(t, off);
        void**& e = F<void**>(t, off + 4);
        if (!b || e < b || e - b > 256)
            continue;
        void* want = off == 0x340 ? c : (uint8_t*)c + 0xC0;
        void** w = b;
        for (void** it = b; it != e; ++it)
            if (*it != want)
                *w++ = *it;
        for (void** it = w; it != e; ++it)
            *it = nullptr;
        e = w;
    }
    F<uint8_t>(c, 0x12C) = 0; // (not selected)
    // BUG-0667: the body's behaviour tree is ended while its tribe is still set (as the host's body ended its nodes
    // by dying before its game took it out of the tribe): the exits of the nodes a mirror's skipped tree left active
    // (eat, GrabTool, ...) release their spots now, and nothing is left for its teardown, which may come after the
    // tribes are gone (the world's teardown at a menu exit: long_tribe_short 20261107-043030)
    if (a_BTTerminate)
        skg::cdecl_call<void>(a_BTTerminate, c);
    F<void*>(c, 0x1014) = nullptr;
    g_detached[c] = t;
    g_bodies_detached++;
}
bool hm_alive(const mirror::Ref& r) { return kept_alive(r.p, r.serial, r.noun); }
HostTable g_host;
// the entry of a member object (a pointer compare: no liveness check), null when the table has none
HEntry* hm_of(const void* c)
{
    uint16_t id;
    return c && g_host.members.id_of(c, id) ? g_host.members.find(id) : nullptr;
}
// the object the table has for an id (no liveness check), null when none
void* hm_obj(uint16_t id)
{
    HEntry* e = g_host.members.find(id);
    return e ? e->ref.p : nullptr;
}
// the entry's object is that member, listed now (BUG-0951's serial; its own memory with tribe.kept_refs off), uncounted
bool hm_ok(const HEntry& e)
{
    if (!e.ref.p)
        return false;
    return g_kept_refs_fix ? game::alive(game::LiveRef{e.ref.p, e.ref.serial, e.ref.noun}) : game::object_alive(e.ref.p);
}
// BUG-0671 (below, h_BabyBorn)
using BabyBorn_t = void(SK_THISCALL*)(void*, float, float, float);
static BabyBorn_t o_BabyBorn;
double g_last_send;
uint16_t g_cycle;
uint32_t g_cmds_done, g_cmds_rejected;
std::string g_last_cmd; // the last order (guest: sent, host: done or why it was rejected), tests
static std::string g_last_local; // the host's own last order (tests)
static uint32_t g_local_orders;
uint32_t g_gifts;

// ---------------- guest ----------------
float g_test_lift = 0.f; // tests (BUG-0329): `coop.tribe lift <m>` on a guest
// BUG-0844 (class BUG-0426): a mirror's citizen was kept across frames and checked by its own memory (object_alive):
// the game frees a removed citizen later and gives the block to the next citizen it makes (a member of any tribe,
// the guest's own too). While the host's states paused (its editor, the fork's graduation report) the mirror stayed
// bound to that address: the guest then drove a member of another tribe as the host's member, and the snapshot's end
// called RemoveMember through the citizen's owner field (garbage in a freed block: the fork's crash in
// remove_member). Now a mirror's citizen is used only while it is the object bound (the object list of the frame and
// its serial), and RemoveMember runs only on a live tribe that has it in its member vector; a citizen that is not is
// only forgotten. Old (`coop.fix tribe.mirror_member_lifetime off`): object_alive and the owner field.
bool g_member_lifetime_fix = true;
COOP_FIX("tribe.mirror_member_lifetime", "BUG-0844", g_member_lifetime_fix, "tribe: a guest never moves or removes another tribe's member that the game built where a removed copy of a host's member was");
// C1 (BUG-1140): the guest's mirrors (see g_hm); the liveness rule of BUG-0844 is the registry's
bool gm_alive(const mirror::Ref& r)
{
    if (!r.p)
        return false;
    if (!g_member_lifetime_fix)
        return game::object_alive(r.p);
    return game::alive(game::LiveRef{r.p, r.serial, kMemberNoun});
}
GuestMirrors g_guest;
// the mirror's object is the one bound and alive (no detach: guest_state forgets a dead one, with its air hold)
bool mirror_ok(const GEntry& e) { return gm_alive(e.ref); }
// the mirror entry a member object of this game is bound to (a pointer compare), null when none
static GEntry* gm_of(const void* c)
{
    uint16_t id;
    return c && g_guest.mirrors.id_of(c, id) ? g_guest.mirrors.find(id) : nullptr;
}
// the mirror bound to c forgets it (its entry stays: the next snapshot binds or spawns one)
static void gm_forget(const void* c)
{
    if (GEntry* g = gm_of(c))
        g_guest.mirrors.detach(g->id);
}
uint32_t g_mirror_logs;
// BUG-0365 (see mirror_drive)
uint32_t g_mirror_walks, g_mirror_timed;
// BUG-0444: the mirrors kept their own distance from the planet's centre (own_height, BUG-0329) and were never
// put onto this game's ground: that is the game's own locomotion, and a guest's game does not update its
// citizens for 20-40 s after a cold join (game time paused, no BehaviorTreeTick) or while it is paused. A member
// the host walked up a slope then was placed on the host's path at its old height, deeper and deeper under the
// guest's ground (2.6-3.3 m for ~20 s in tribe_coop's hunt; 9 m in 60 m up a hill, tribe_mirror_ground).
// The ground policy of the animal proxies (npc.proxy_ground, BUG-0399, driver.h): a new mirror is made on this
// game's ground when the host's position is under it, a walking one is placed onto it (WalkPlace::ground, and
// out_of_ground for a mirror deeper than its 3 m), a standing one found more than 0.25 m under it is teleported onto
// it (drive::step's ground).
uint32_t g_mirror_grounded; // diagnostics (coop.tribe): standing mirrors put out of the ground
// tests (BUG-0481): `coop.tribe delay <s>` on a guest: a planted late mirror (play_tribe's sync check must fail)
double g_test_delay = kMirrorDelay;
// BUG-0974: since the lead (BUG-0425) the mirrors are shown where the host has its members now and the buffer's
// delay is not read, so the planted delay changed nothing and play_tribe passed with it. Now the delay also holds the
// lead target back (net::SnapshotBuffer::test_lag). Off: the delay is the buffer's interpolation delay only (0.7.13).
bool g_test_lag_fix = true;
COOP_FIX("tribe.test_delay_lead", "BUG-0974", g_test_lag_fix, "tests: coop.tribe delay also delays the mirrors shown by the lead (play_tribe's planted desync)");
// BUG-0386 (`coop.fix tribe.mirror_keep`): a walked member mirror whose placing this game does not keep is moved by
// teleports with the mark until it is on its path again (WalkPlace::keep_lost, the NPC proxies' rule of BUG-0361).
// Observed (tribe_walk_mobile, fix/w45-remote-bodies-2 lab 20261106-172428 #2, track tribe_walk_guest_walks_173129,
// the guest's own member 2 on the guest): from the frame its game's walk took over (game speed 0 -> 11-12 m/s) every
// placing of 0.38-0.40 m was undone and the game moved it 0.03-0.04 m per frame for 0.9 s while its target walked on at
// 9 m/s; the error grew to 8 m and drive::step teleported it. The placing was never counted as lost (under 0.5 m a
// frame) and keep_lost was off for mirrors. Off: placed without the mark always (0.7.9).
bool g_mirror_keep_fix = true;
COOP_FIX("tribe.mirror_keep", "BUG-0386", g_mirror_keep_fix, "tribe: a member mirror the guest's game holds back (its placing undone) is moved on its path by marked teleports, not left to fall 8 m behind");
// BUG-0612 (the older-cycle part of BUG-0425's umbrella): a late chunk of an older snapshot cycle started that cycle
// again (the newer cycle's chunks forgotten, older states over newer ones). Off: 0.7.8 (any other cycle restarts).
bool g_cycle_order_fix = true;
COOP_FIX("net.cycle_order", "BUG-0612", g_cycle_order_fix, "net: a late chunk of an older Tribe / Civ snapshot or realign planet-list cycle is dropped, not taken as a new cycle (BUG-0886 too)");
static uint32_t g_cmds_sent;
// BUG-1051: the guest's cycle tracker of the host's Tribe snapshots belongs to that host's session and world: a guest
// that joined another host (or the same one after it restarted its game, whose counter starts at 1 again) dropped
// every snapshot as "older" until the new counter passed the old one (up to an hour). Reset on every session end and
// world change (lifecycle.h; the drop counter `older` stays for the status). Off: kept (0.7.13).
static bool g_cycle_reset_fix = true;
COOP_FIX("tribe.cycle_reset", "BUG-1051", g_cycle_reset_fix, "tribe: a guest's Tribe snapshot cycle starts over with every session and world, so a new or restarted host's snapshots are not dropped as older");
// C4 of 0.7.15 (BUG-1143, docs/01-architecture.md#arch-snapshot-rx): the chunked receive of the host's snapshot
// cycles (the older-cycle drop of BUG-0612, a cut chunk that counts for nothing, the sweep only after a complete
// cycle) is net::SnapshotReceiver's, shared with Civ and the squadron. One difference on purpose: a duplicate of a
// completed cycle's last chunk does not sweep a second time (sweep_once).
LIFECYCLE_STATE("tribe.cycle", lifecycle::kSession | lifecycle::kWorld, &g_cycle_reset_fix, [] {
    g_guest.snap.reset(); // (its counters stay)
}, []() -> size_t { return g_guest.snap.held(); });
// BUG-1055: the host tribe index -> local tribe map points into the old world after a load (its users check it
// against all_tribes, the test path frametool did not)
WORLD_STATE("tribe.world_map", nullptr, [] { g_guest.local_tribe_of.clear(); }, []() -> size_t {
    size_t n = 0;
    for (void* t : g_guest.local_tribe_of)
        n += t != nullptr;
    return n;
});
// BUG-0672 (tribe.world_map): the mapping outlived the world it was made in. After a load (a warm reset, a received
// world) the guest's wobj tool kind made the host's tool in tribe index i before the new world's first tribe state had
// mapped it again: tribe_of_index gave the old world's freed tribe (its species +0x18CC already released), and the
// tool's SetPosition (0xC9DB80) re-laid that tribe's spots (0xC98940 type 6), which read the species' size: the fault
// at 0xCE6AE7 (data 0x554). On: the mapping is dropped with the world and an entry is used only while it is one of
// this world's tribes. Off: 0.7.9.
static bool g_world_map_fix = true;
COOP_FIX("tribe.world_map", "BUG-0672", g_world_map_fix,
         "tribe: right after a world load the guest never builds the host's tool buildings into a tribe of the old world");
uint32_t g_map_stale_skipped;
bool g_state_hold; // tests (BUG-0672): the guest ignores the host's tribe states (the mapping is not renewed)

uint8_t owner_of(void* c)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        HEntry* e = hm_of(c);
        return e ? e->data.owner : 0xFF;
    }
    GEntry* g = gm_of(c);
    return g ? g->data.owner : 0xFF;
}

// ---------------- hooks ----------------
using BTTick_t = int(SK_CDECL*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static BTTick_t o_BTTick;
static uint32_t g_bt_skipped, g_bt_calls; // diagnostics (coop.tribe): mirrors' AI ticks skipped / all ticks
// tests (BUG-0667): `coop.tribe localbt <member id> 1|0` on a guest: that mirror's tree runs as while the host is away
std::unordered_set<void*> g_bt_local;
std::unordered_map<void*, FreezeAt> g_bt_freeze_at;
std::unordered_set<void*> g_bt_frozen;
// BUG-0667: cTribe_ReleaseSlot (a behaviour node's exit giving a layout spot back) with no tribe is skipped
using ReleaseSlot_t = void(SK_THISCALL*)(void*, int, int);
static ReleaseSlot_t o_ReleaseSlot;
static void SK_THISCALL h_ReleaseSlot(void* t, int type, int slot)
{
    if (!t) {
        if (g_slot_null_skipped++ < 8)
            H->log(M, SK_WARN, "tribe: a layout spot release (type %d, slot %d) with no tribe skipped (BUG-0667)", type, slot);
        return;
    }
    o_ReleaseSlot(t, type, slot);
}
// BUG-0667: on a guest a citizen torn down with no tribe gets back the tribe it left last (the game's SetTribe(null),
// e.g. a member's death in this game's own play while the host was away, or detach_body), so the exits of the
// behaviour nodes still active on it (a mirror's tree is skipped: eat 0x0461300F, 0x0551759F, ...) find it; with no
// such tribe left, its tree's stack is emptied first (no exit runs, none can read the null tribe). long_tribe_short
// (20261107-032137): the guest left the world for the menu, the eat exit's slot release was skipped, the next exit
// (0xDA7FB0, reads tribe+0x26C) faulted.
static std::unordered_map<void*, void*> g_left_tribe; // guest: citizen -> the tribe its SetTribe(null) took it out of
static uint32_t g_teardown_tribe_back, g_teardown_bt_emptied;
using SetTribe_t = void(SK_THISCALL*)(void*, void*);
static SetTribe_t o_SetTribe;

// BUG-1460 (`coop.fix tribe.makeway_null`): Creature_AskGroupToMakeWay 0xC28530 (cdecl(ctx, creature), plain ret,
// bool: the caller GroupLocomotion_Step pops 8 bytes) asks a blocked walker's group to step aside; for a citizen it
// calls Citizen_GetTribe(c)->PostStimulusNear(...) with no null check, so a walking citizen out of its tribe
// (+0x1014 null) faults at 0x00C8F6D3. A citizen without a tribe asks nobody (false: not made to wait); everything
// else goes to the game's own function. Called from the locomotion step for every moving creature: read-only, no
// lock, no allocation; the counter is atomic (any thread). Off: the game's function always (the fault)
static bool g_makeway_null_fix = true;
COOP_FIX("tribe.makeway_null", "BUG-1460", g_makeway_null_fix,
         "tribe: a walking member that is out of its tribe does not make the game fault when its way is blocked");
static std::atomic<uint32_t> g_makeway_null_skipped{0};
typedef bool(__cdecl* MakeWay_t)(void*, void*);
static MakeWay_t o_MakeWay;
static bool __cdecl h_MakeWay(void* ctx, void* c)
{
    const bool citizen = c && a_CitizenVt && *(uintptr_t*)c == a_CitizenVt;
    if (tribe_logic::makeway_skip(g_makeway_null_fix, citizen, citizen ? F<void*>(c, 0x1014) : c)) {
        g_makeway_null_skipped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return o_MakeWay(ctx, c);
}
static void SK_THISCALL h_SetTribe(void* c, void* t)
{
    if (is_guest()) {
        void* old = F<void*>(c, 0x1014);
        if (!t && old) {
            g_left_tribe[c] = old;
            static uint32_t said;
            if (said++ < 24) { // diagnostics (BUG-0667): who takes a citizen out of its tribe on a guest
                char b[160] = "";
                int n = 0;
                const uint32_t* sp = (const uint32_t*)__builtin_frame_address(0);
                for (int i = 0; i < 96 && n < 8; i++)
                    if (sp[i] > 0x401000 && sp[i] < 0x1300000)
                        n += snprintf(b + strlen(b), sizeof b - strlen(b), " %08X", sp[i]) > 0;
                GEntry* gi = gm_of(c);
                H->log(M, SK_INFO, "tribe: SetTribe(null) on %p (mirror #%d, old tribe %p, dead %u, node %08X); stack:%s (BUG-0667)", c,
                       gi ? (int)gi->id : -1, old, F<uint8_t>(c, 0xB5E),
                       F<uint8_t*>(c, 0xB4C) ? *(uint32_t*)(F<uint8_t*>(c, 0xB4C) + 0x1D8) : 0, b);
            }
        }
        else if (t)
            g_left_tribe.erase(c);
    }
    o_SetTribe(c, t);
}
using CitizenTeardown_t = void(SK_THISCALL*)(void*);
static CitizenTeardown_t o_CitizenTeardown;
static void SK_THISCALL h_CitizenTeardown(void* c)
{
    if (!F<void*>(c, 0x1014) && (is_guest() || g_left_tribe.count(c) || g_detached.count(c))) {
        auto tribes = all_tribes();
        auto listed = [&](void* t) { return t && std::find(tribes.begin(), tribes.end(), t) != tribes.end(); };
        void* t = nullptr;
        bool add_ref = false;
        if (auto it = g_detached.find(c); it != g_detached.end() && listed(it->second))
            t = it->second; // detach_body kept the reference
        else if (auto it2 = g_left_tribe.find(c); it2 != g_left_tribe.end() && listed(it2->second))
            t = it2->second, add_ref = true; // SetTribe(null) released it
        if (t) {
            if (add_ref)
                vcall<void>(t, 0); // AddRef: the teardown releases +0x1014 (0xC27CF3)
            F<void*>(c, 0x1014) = t;
            g_teardown_tribe_back++;
        } else {
            uint8_t* ai = F<uint8_t*>(c, 0xB4C);
            // the tree's terminate (0xC0D500 -> 0xBC8850) runs the exits unless the stack's first word (ai+0x1D0) is
            // -1, its "no tree running" mark; it then fills the 0x418-byte stack with 0xFF itself
            if (ai && skg::readable(ai, 0x620) && *(int*)(ai + 0x1D0) != -1) {
                if (g_teardown_bt_emptied++ < 8) {
                    auto lt = g_left_tribe.find(c);
                    H->log(M, SK_WARN, "tribe: citizen %p torn down with no tribe in node %08X (depth %d): its tree's exits "
                           "skipped (BUG-0667; left tribe %p, detached %d, tribes listed %u)", c, *(uint32_t*)(ai + 0x1D8),
                           *(int*)(ai + 0x1DC), lt == g_left_tribe.end() ? nullptr : lt->second, (int)g_detached.count(c),
                           (unsigned)tribes.size());
                }
                memset(ai + 0x1D0, 0xFF, 0x418); // as 0xBC8850 leaves it (BUG-0667: depth 0 alone did not stop the exits)
            }
        }
    }
    g_detached.erase(c);
    g_left_tribe.erase(c);
    o_CitizenTeardown(c);
}
// behaviour tree tick (shared by every creature kind): skipped for citizen mirrors on guests
static int SK_CDECL h_BTTick(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t self, uint32_t a5, uint32_t a6,
                             uint32_t a7, uint32_t a8)
{
    g_bt_calls++;
    if (g_detached.count((void*)self)) { // BUG-0667: a detached body's tree stays ended
        g_bt_skipped++;
        return 0;
    }
    if (is_guest() && is_citizen((void*)self) && game::mode_id() == modes::kTribe && !local_play() &&
        !g_bt_local.count((void*)self)) {
        g_bt_skipped++;
        return 0;
    }
    if (g_bt_frozen.count((void*)self)) { // (tests, BUG-1084)
        g_bt_skipped++;
        return 0;
    }
    if (!is_guest()) // BUG-0531 (diagnostics): does a host member's tree run
        if (HEntry* e = hm_of((void*)self))
            e->data.bt_ticks++;
    int r = o_BTTick(a0, a1, a2, a3, self, a5, a6, a7, a8);
    if (!g_bt_freeze_at.empty()) // (tests, BUG-1084)
        if (auto it = g_bt_freeze_at.find((void*)self); it != g_bt_freeze_at.end()) {
            uint8_t* ai = F<uint8_t*>((void*)self, 0xB4C);
            int depth = ai && skg::readable(ai, 0x620) ? *(int*)(ai + 0x1DC) : -1;
            int st = depth >= 1 && depth < 16 ? *(int*)(ai + 0x1D0 + 4 * (0xF + 0x2B * (depth - 1))) : -1;
            if (depth >= 1 && *(uint32_t*)(ai + 0x1D8) == it->second.node && st >= it->second.state && it->second.ticks-- <= 0) {
                H->log(M, SK_INFO, "tribe: test: citizen %p held in node %08X state %d (BUG-1084)", (void*)self, it->second.node, st);
                g_bt_frozen.insert((void*)self);
                g_bt_freeze_at.erase(it);
            }
        }
    return r;
}

// BUG-1087: a click, a box or a roster card on another player's member selected nothing and said nothing: a new player
// saw "some of my members do not react". On: the refusals of a frame are told once (at most every 5 s, by the tick):
// "That member is <name>'s: you lead only your own members". `coop.fix tribe.foreign_note off`: silent (0.7.13).
static bool g_foreign_note_fix = true;
COOP_FIX("tribe.foreign_note", "BUG-1087", g_foreign_note_fix,
         "tribe: a click on another player's member (or its roster card) says whose member it is");
static uint32_t g_foreign_clicks, g_foreign_told; // refused selections / notes shown (coop.status)
static uint8_t g_foreign_owner = 0xFF;            // the owner of the last refused member, not yet told
static unsigned g_foreign_n;                       // refused members since the last note
static double g_foreign_told_at = -100;
static void foreign_note_tick(double now)
{
    if (!g_foreign_n)
        return;
    if (!g_foreign_note_fix || !C.sess || !C.feed) {
        g_foreign_n = 0;
        return;
    }
    if (now - g_foreign_told_at < 5.0) { // (the refusals of the same click spree)
        g_foreign_n = 0;
        return;
    }
    std::string who = g_foreign_owner < 0xFE ? C.sess->name_of(g_foreign_owner) + "'s" : std::string("not yours");
    C.feed(g_foreign_n == 1 ? "That member is " + who + ": you lead only your own members"
                            : "Those members are " + who + ": you lead only your own members");
    g_foreign_told++;
    g_foreign_told_at = now;
    g_foreign_n = 0;
}

// a player may select only their own members of the player tribe
using SetSelected_t = int(SK_THISCALL*)(void*, int);
static SetSelected_t o_SetSelected;
static int SK_THISCALL h_SetSelected(void* spatial, int sel)
{
    if ((sel & 0xFF) && active()) {
        void* c = (uint8_t*)spatial - 0xC0;
        uint8_t owner = owner_of(c);
        if (owner != 0xFF && owner != C.sess->my_id()) {
            sel = 0;
            g_foreign_clicks++;
            g_foreign_owner = owner;
            g_foreign_n++;
        }
    }
    return o_SetSelected(spatial, sel);
}

static Target encode_target(void* t)
{
    Target r;
    if (!t)
        return r;
    if (GEntry* g = gm_of(t)) {
        r.kind = 1;
        r.id = g->id;
        return r;
    }
    if (is_tree(t)) { // a planet tree: no cGameData, the host finds its own copy by position
        r.kind = 3;
        r.noun = kTreeNoun;
        r.pos = {F<float>(t, 0x10), F<float>(t, 0x14), F<float>(t, 0x18)};
        return r;
    }
    r.kind = 2;
    r.noun = game::noun_id(t);
    game::object_position(t, r.pos);
    return r;
}

void write_target(net::Writer& w, const Target& t)
{
    w.u8(t.kind);
    w.u16(t.id);
    w.u32(t.noun);
    w.f32(t.pos.x); w.f32(t.pos.y); w.f32(t.pos.z);
}

Target read_target(net::Reader& r)
{
    Target t;
    t.kind = r.u8();
    t.id = r.u16();
    t.noun = r.u32();
    t.pos.x = r.f32(); t.pos.y = r.f32(); t.pos.z = r.f32();
    return t;
}

// BUG-0879: while the host is away the guest's own game plays (tribe.away): a raid of its members can end an NPC
// village there (the hut's fall, the village's conquered flag +0x556, the conquest cinematic), and nothing told the
// host: when it was back its state stood the village up again. A village this guest's game conquers while the host
// is away now goes to the host as CMD_CONQUER (its position; held there until the host is back in Tribe, BUG-0693),
// and the host's game fells that village's hut with the game's own blow by a member of that player.
// `coop.fix tribe.away_conquest off`: 0.7.12.
bool g_away_conquest_fix = true;
COOP_FIX("tribe.away_conquest", "BUG-0879", g_away_conquest_fix,
         "tribe: a village a guest conquers while the host is away is conquered in the host's game when it is back");
uint32_t g_conquests_sent, g_conquests_done, g_conquests_refused;

static void send_cmd(uint8_t kind, const std::vector<uint16_t>& ids, int action, const Target& tgt, const net::Vec3& pos)
{
    if (ids.empty())
        return;
    net::Writer w;
    w.u8(kind);
    w.u8((uint8_t)std::min<size_t>(ids.size(), 64));
    for (size_t i = 0; i < ids.size() && i < 64; i++)
        w.u16(ids[i]);
    w.u32((uint32_t)action);
    write_target(w, tgt);
    w.f32(pos.x); w.f32(pos.y); w.f32(pos.z);
    C.sess->send_reliable_to(0, net::MSG_TRIBE_CMD, w);
    g_cmds_sent++;
    char b[128];
    snprintf(b, sizeof b, "kind=%u n=%u action=%d target=%u/%u/%X (%.1f %.1f %.1f)", kind, (unsigned)ids.size(), action,
             tgt.kind, tgt.id, tgt.noun, tgt.pos.x, tgt.pos.y, tgt.pos.z);
    g_last_cmd = b;
}

static void ring_order(int action, void* target, const std::vector<uint16_t>& ids); // BUG-0570

// cTribeInputStrategy::DoActionForObjects(vector<cSpatialObject*>*, action, target, prop) - every
// order on an object goes through it; on guests the order goes to the host instead
using DoActionForObjects_t = int(SK_THISCALL*)(void*, void*, int, void*, void*);
static DoActionForObjects_t o_DoActionForObjects;
static int SK_THISCALL h_DoActionForObjects(void* self, void* vec, int action, void* target, void* prop)
{
    if (local_play()) {
        g_away_orders++;
        return o_DoActionForObjects(self, vec, action, target, prop);
    }
    if (!is_guest() || game::mode_id() != modes::kTribe) {
        if (is_host() && game::mode_id() == modes::kTribe) {
            char lb[96];
            snprintf(lb, sizeof lb, "action=%d n=%d obj=%p noun=%X", action,
                     vec ? (int)(F<void**>(vec, 4) - F<void**>(vec, 0)) : -1, target, target ? game::noun_id(target) : 0);
            g_last_local = lb;
            g_local_orders++;
        }
        return o_DoActionForObjects(self, vec, action, target, prop);
    }
    std::vector<uint16_t> ids;
    void** b = vec ? F<void**>(vec, 0) : nullptr;
    void** e = vec ? F<void**>(vec, 4) : nullptr;
    for (void** it = b; it && it < e; ++it) {
        if (GEntry* g = gm_of((uint8_t*)*it - 0xC0))
            ids.push_back(g->id);
    }
    send_cmd(CMD_ACTION, ids, action, encode_target(target), {});
    ring_order(action, target, ids); // BUG-0570
    char ob[48];
    snprintf(ob, sizeof ob, " obj=%p", target);
    g_last_cmd += ob;
    return ids.empty() ? 1 : 0;
}

// ---------------------------------------------------------------- BUG-0570
// The red circle under the enemy of an attack order is not drawn by the order: the Tribe HUD's ring update
// (TribeTargetRings_Update 0xCCC7C0, owner [TribeUI 0x169B2C8] +0x240) draws, every frame, a ring
// (TribeTargetRing_Draw 0xCCBE90) under the combat target (+0xE7C) of every selected player citizen and sweeps the
// rings nobody touched. +0xE7C is set by the citizen's AI attack node only, and a guest's mirrors run no AI: its
// order went to the host, its own game showed no ring. Now the guest's game draws the ring of its own attack
// order, as the HUD draws it, while one of the ordered members is selected and the target lives (120 s at most).
static constexpr int kActionAttack = 7; // DoActionForAllSelected(7, target) of a right click on an enemy
Ring g_ring;
uint32_t g_rings_drawn = 0;
// BUG-1055: the guest's own attack order (its ring) ends with its session and its world
LIFECYCLE_STATE("tribe.ring", lifecycle::kSession | lifecycle::kWorld, nullptr, [] {
    g_ring.target = nullptr;
    g_ring.ids.clear();
}, []() -> size_t { return g_ring.target ? 1 : 0; });
static uintptr_t a_RingDraw;
using RingsUpdate_t = void(SK_THISCALL*)(void*);
static RingsUpdate_t o_RingsUpdate;

static void ring_order(int action, void* target, const std::vector<uint16_t>& ids)
{
    if (action == kActionAttack && target && !ids.empty() && (is_citizen(target) || game::noun_id(target) == 0x18EB45E)) {
        g_ring.target = target;
        g_ring.ids = ids;
        g_ring.since = H->time_seconds();
        g_ring.serial = game::object_serial(target);
    } else
        g_ring.target = nullptr; // another order ends the attack
}

static void* g_ring_owner = nullptr; // the HUD's ring owner, as the game passes it (tests: ring_shown)
static uint32_t g_ring_owner_frame = 0;

static void SK_THISCALL h_RingsUpdate(void* owner)
{
    o_RingsUpdate(owner);
    g_ring_owner = owner;
    g_ring_owner_frame = H->frame_index ? (uint32_t)H->frame_index() : 0;
    void* t = g_ring.target;
    if (!t || !a_RingDraw || !is_guest() || game::mode_id() != modes::kTribe)
        return;
    if (H->time_seconds() - g_ring.since > 120 || !kept_alive(t, g_ring.serial, 0) || game::is_destroyed(t) || game::is_dead(t) ||
        F<float>(t, 0x5E0) <= 0) {
        g_ring.target = nullptr;
        return;
    }
    void* by = nullptr;
    for (uint16_t id : g_ring.ids) {
        GEntry* g = g_guest.mirrors.find(id);
        if (g && mirror_ok(*g) && F<uint8_t>(g->ref.p, kSelectedOff)) { // (BUG-0955)
            by = g->ref.p;
            break;
        }
    }
    if (!by)
        return; // the HUD rings the targets of selected members only
    uint8_t* sp = (uint8_t*)t + 0xC0;
    float* p = vcall<float*>(sp, 0x2C / 4);
    if (!p)
        return;
    float pos[3] = {p[0], p[1], p[2]};
    float radius = vcall<float>(sp, 0x74 / 4);
    thiscall<void>(a_RingDraw, owner, by, F<uint32_t>(t, 0x24), pos, radius, 0, (int)F<uint8_t>(sp, 0x77));
    g_rings_drawn++;
}

// tests: the HUD's ring map (the hash map at owner +0x2C0) holds a live ring (an effect) under creature t:
// TribeTargetRing_Find 0xCCBD80 (the draw's own lookup; it inserts an empty entry the next pass erases)
bool ring_shown(void* t)
{
    uintptr_t find = H->sym("SporeCoop::TribeTargetRing_Find");
    // the owner the HUD's update ran on this or the last frame (the HUD of an older stage is gone)
    uint32_t now = H->frame_index ? (uint32_t)H->frame_index() : 0;
    if (!find || !g_ring_owner || !t || game::mode_id() != modes::kTribe || now - g_ring_owner_frame > 2)
        return false;
    uint32_t key = F<uint32_t>(t, 0x24);
    uint32_t* v = thiscall<uint32_t*>(find, (uint8_t*)g_ring_owner + 0x2C0, &key); // {effect id, IVisualEffect*, touched}
    return v && v[1] != 0;
}

// cTribeInputStrategy::WalkSelectedTo(const Vector3*)
using WalkSelected_t = int(SK_THISCALL*)(void*, const float*);
static WalkSelected_t o_WalkSelected;
static int SK_THISCALL h_WalkSelected(void* self, const float* pos)
{
    if (local_play()) {
        g_away_orders++;
        return o_WalkSelected(self, pos);
    }
    if (!is_guest() || game::mode_id() != modes::kTribe || !pos) {
        if (is_host() && game::mode_id() == modes::kTribe && pos) {
            char lb[96];
            snprintf(lb, sizeof lb, "walk to (%.1f %.1f %.1f)", pos[0], pos[1], pos[2]);
            g_last_local = lb;
            g_local_orders++;
        }
        return o_WalkSelected(self, pos);
    }
    std::vector<uint16_t> ids;
    g_guest.mirrors.for_each([&](GEntry& e) {
        if (e.ref.p && F<uint8_t>(e.ref.p, kSelectedOff) && e.data.owner == C.sess->my_id())
            ids.push_back(e.id);
    });
    send_cmd(CMD_WALK, ids, 4, {}, {pos[0], pos[1], pos[2]});
    g_ring.target = nullptr; // a walk ends the attack (BUG-0570)
    char b[64];
    snprintf(b, sizeof b, " to (%.1f %.1f %.1f)", pos[0], pos[1], pos[2]);
    g_last_cmd += b;
    return 0;
}

static void coop_init();

// BUG-0475: a member's size is its own game's pick (cCreatureCitizen GetDesiredScale vt 0x80: a random
// size per member, the chief's bigger): the guest's mirror (bound to a local member or spawned) kept a
// size of its own (3.79 host, 4.47 guest). The members are one kind of the field replication (fields.h):
// the guest sets the host member's scale on its mirror.
// BUG-0652: which member leads its tribe (the tribe's cCommunity::mpLeader +0x11C: the chief, staff 0xB, chief
// size) is each game's own: after a mirror of the host's leader was made again (a load, a rejoin, the mirror
// respawned) the guest's game had made another member the leader (cTribe::SetChieftain(0): the first adult), an
// NPC tribe's musician (tool 6) there. That mirror kept the staff (tribe.leader_tool, BUG-0583: the warning
// "fields: tribe_member_tool N tool: applied 6.000, the game reads 11.000" in 55 lab runs), the host's leader
// mirror was a member with a staff too: the guest saw two chiefs and one musician less. The leader is a field of
// the members (2 = leads, 1 = not: values under 0.001 are not sent); the guest's game makes the mirror of the
// host's leader its tribe's leader with SetChieftain. In this kind (kid 2) it is applied before the tools (kid 5)
// in the same pass: the old leader takes the host's tool at once. `coop.fix tribe.leader_sync off`: own leaders.
static bool g_leader_sync_fix = true;
COOP_FIX("tribe.leader_sync", "BUG-0652", g_leader_sync_fix, "tribe: a guest's game makes the mirror of the host's tribe leader its leader (staff, chief size): no second chief, the old one takes its tool");
static float read_leader(void* c);
static void apply_leader(void* c, float v);
static const fields::Field kLeader{fields::F_LEADER, "leader", read_leader, apply_leader, 0.01f};
static const fields::Field kMemberFields[] = {fields::kScale, kLeader};

static void member_each(void (*cb)(uint16_t, void*, void*), void* u)
{
    // the tribes' own member lists now (a g_hm entry can be a member freed since the last cycle)
    for (void* t : all_tribes())
        for (void* c : members(t))
            if (HEntry* e = hm_of(c))
                cb(e->id, c, u);
}

void* member_mirror(uint16_t id)
{
    GEntry* g = g_guest.mirrors.find(id);
    return g && mirror_ok(*g) ? g->ref.p : nullptr; // (BUG-0955)
}

AnimStats g_anim;

uintptr_t a_SetChieftain; // BUG-0652
uint16_t member_id(void* c);
uintptr_t a_SetSpecializedTool, a_ToolModelReset, a_SetToolPurchased, a_ToolLayout, a_IsLeader;
// BUG-0583: a citizen's abilities (vt 0xB0 count, vt 0xB4 get, 0xC24DB0 / 0xC24E40) are its species' plus 2 for a
// tool of type 1..6 or 1 for 9 / 0xB, plus 6 for its tribe's leader (0xC23430: owner +0x11C == it); the getter
// serves the leader 1 tool slot + 6, so a leader holding a tool of type 1..6 counts one ability more than it has:
// the last one is null and GetAbilityIndexByType (every citizen Update) faults on it every frame. The game never
// gives its leader such a tool (SetSpecializedTool(0) on it makes 0xB, the staff), the mirrors' tool field did: a
// guest whose tribe's leader is the mirror of a host member that took a weapon (after a rejoin: lab, guest faulted
// 1151 times, ECX = 1 at the fault = the leader branch). On: no tool of type 1..6 on this game's leader (it keeps
// its staff). Off: the host's tool as before.
static bool g_leader_tool_fix = true;
COOP_FIX("tribe.leader_tool", "BUG-0583", g_leader_tool_fix, "tribe: a guest's tribe leader never holds a weapon or instrument from the host's member (the game faulted every frame)");
uint32_t g_tool_sets, g_leader_tools_kept;
bool is_leader(void* c) { return a_IsLeader && is_citizen(c) && thiscall<bool>(a_IsLeader, c); }
// BUG-1335 (creature.fx_bone): a hand item (the chief's staff, a tool) is attached at the member's hand bone (+0xFC4):
// CreateEffect reads that bone of its body's bone array without a check, and a member made a moment ago (a guest's
// new mirror) has its hand bone from its species before its body has the bones (lab 20261119-214823 late_species:
// the mirror made chief 0.65 s after its spawn faulted). Ready: the body has the bone (or the hand is -1, "none").
uint32_t g_hand_waits, g_hand_repairs;
bool hand_ready(void* c)
{
    if (!game::fx_bone_fix())
        return true;
    return has_body(c) && game::creature_bone(c, F<int>(c, kHandOff)) != bone_rules::Bone::Out;
}
static float read_held_tool(void* c) { return is_citizen(c) ? (float)F<int>(c, kHeldToolOff) : 0.f; }
void apply_held_tool(void* c, float v)
{
    int t = (int)std::lround(v);
    if (!is_citizen(c) || !a_SetSpecializedTool || t < 0 || t > 0x20)
        return;
    int cur = F<int>(c, kHeldToolOff);
    // SetSpecializedTool(0) on the tribe's leader makes it 0xB (the staff): leave the leader's staff
    if (cur == t || (t == 0 && cur == 0xB))
        return;
    if (g_leader_tool_fix && t >= 1 && t <= 6 && is_leader(c)) {
        if (g_leader_tools_kept++ < 5)
            H->log(M, SK_INFO, "tribe: this game's tribe leader keeps its staff (the host's member holds tool %d, BUG-0583)", t);
        return;
    }
    thiscall<void>(a_SetSpecializedTool, c, t);
    g_tool_sets++;
}
// BUG-0652 (above kMemberFields)
static uint32_t g_leaders_set;
static float read_leader(void* c) { return is_leader(c) ? 2.f : 1.f; }
static void apply_leader(void* c, float v)
{
    if (!g_leader_sync_fix || !a_SetChieftain || v < 1.5f || !is_citizen(c) || is_leader(c))
        return;
    void* tribe = F<void*>(c, 0x1014); // cCreatureCitizen: its tribe (+0x1010 its city: Civ, not here)
    if (F<void*>(c, 0x1010) || !tribe || !skg::readable(tribe, 0x120))
        return;
    if (!hand_ready(c)) { // BUG-1335: its staff goes onto its hand: not before its body has that bone
        g_hand_waits++;
        return;
    }
    void* old = F<void*>(tribe, 0x11C);
    thiscall<void>(a_SetChieftain, tribe, c);
    if (g_leaders_set++ < 5)
        H->log(M, SK_INFO, "tribe: member #%u leads its tribe here too, as in the host's game (the leader was %s, BUG-0652)",
               member_id(c), old ? ("#" + std::to_string(member_id(old))).c_str() : "none");
}
static const fields::Field kHeldTool{fields::F_TOOL, "tool", read_held_tool, apply_held_tool, 0.01f, 0.f, 32.f}; // (0: no tool, BUG-1043)
static const fields::Field kToolFields[] = {kHeldTool};

constexpr uint32_t kHandKeyBase = 0x2735300; // the attachment key of hand h (0xC250A0)
uintptr_t a_SetHandheldItem, a_AttachmentOf;
uintptr_t a_ClearPending; // BUG-0992
static bool g_held_item_fix = true;
COOP_FIX("tribe.held_item", "BUG-0906", g_held_item_fix, "tribe: a guest's mirror of a member holds what the host's member holds (the chief's staff, a weapon, an instrument)");
uint32_t g_item_sets;
// the member's animated body exists (+0xB54: null once the game cleaned the citizen up)
bool has_body(void* c) { return is_citizen(c) && F<void*>(c, 0xB54) != nullptr; }
static float read_held_item(void* c) { return has_body(c) ? (float)(F<int>(c, kHeldItemOff) + 1) : 1.f; }
// The item's model alone is not the look: how it is held is a timed modifier of kind 5 of the creature (the table at
// +0xB48, 8 entries of 0x44 bytes; data: the attachment's index, the handle of an animation, a "started" flag; added
// by the attach 0xC250A0 through 0xC240E0). Its tick 0xC11240 starts the taking of the item on its first run
// (attachment +0xC count up, 0xC0FCA0) and lives as long as its animation plays; its remove 0xC14A00 (count down,
// 0xC141D0) puts the item into the resting grip (the chief: the arm raised, the staff upright). In the host's game
// the animation is a short one (the change animation 0x2C39200, a behaviour's), the modifier ends with it. A
// mirror's animation is its idle loop, which never ends: the modifier stayed and the item was held as in the middle
// of taking it (the staff stuck out forward, the arm down; the same on the host when a test attached there, until
// the modifier was ended by hand). So a started kind 5 modifier that a mirror has had for kHoldMax s is ended with
// the game's own remove (as the detach 0xC241A0 does): the item settles into the grip.
constexpr uintptr_t kModifiersOff = 0xB48; // cCreatureBase: the timed modifiers' table (8 x 0x44)
constexpr double kHoldMax = 1.5;
static uintptr_t a_ModifierRemove;
uint32_t g_item_holds_ended;
struct HoldSeen {
    uint32_t handle;
    double since;
};
static std::unordered_map<uint16_t, HoldSeen> g_hold_seen; // mirror id -> its started modifier, first seen
// BUG-1347: a mirror's hold that hold_try bound with the attach (T with hold 0, then SetHandheldItem) in the frame the
// host's animation was started on it (P) or a relayed restart came (R) did not start: hold 0 until the host's
// once-a-second resend bound it again (T, then O 0.02 s later). Guest hold traces of tribe_chief_still's FAILs (lab
// 20261113-060910 twice, 20261117-154911, 20261119-214823, 20261120-041304): the chief's staff in the hand on the host and
// in the resting grip on the guest for 1.0-1.4 s. On: an attach whose hold has not started kHoldRetry s later, while the
// mirror still draws that animation and holds the item, is made again (its pending starts of that id ended first, the
// BUG-0992 call), at most kHoldRetries times per bind. Off: it waits for the resend (as before).
static bool g_hold_retry_fix = true;
COOP_FIX("tribe.hold_retry", "BUG-1347", g_hold_retry_fix,
         "tribe: a guest's copy of a member takes its item into the hand again when the first take in the host's animation did not start");
constexpr double kHoldRetry = 0.3;
constexpr uint8_t kHoldRetries = 2;
// the hand's started modifier of c: its handle (u16 serial | index << 16), 0 none; end: remove it
uint32_t hold_modifier(void* c, bool end)
{
    uint8_t* tab = F<uint8_t*>(c, kModifiersOff);
    if (!a_ModifierRemove || !tab || !skg::readable(tab, 8 * 0x44))
        return 0;
    for (uint32_t i = 0; i < 8; i++) {
        uint8_t* e = tab + i * 0x44;
        if (*(int*)(e + 0x24) != 5 || *(int*)(e + 0x28) != 0 || *(void**)(e + 0x3C) != c || !*(int*)(e + 8))
            continue; // (not started: ended now it would leave the item without a grip)
        uint32_t h = (uint32_t) * (uint16_t*)(e + 0x20) | (i << 16) | 0x80000000u;
        if (end) {
            thiscall<void>(a_ModifierRemove, tab, h & 0x7FFFFFFFu);
            g_item_holds_ended++;
        }
        return h;
    }
    return 0;
}
void set_hand(void* c, int item) { thiscall<void>(a_SetHandheldItem, c, item, -1); } // -1: its own hand (+0xFC4)
// BUG-0914: a member's staff or instrument is held in its hand while the animation it was taken in plays (the kind 5
// timed modifier the attach 0xC240E0 adds with that animation's handle; #tribe-held-item), else in the resting grip.
// The host's game takes the item inside its behaviour animations (a chief swinging its staff: `hold=1`); the guest's
// mirror plays the same animation (the relay) but its hand never takes the item again: the staff stayed upright. The
// host sends each start of a member's hold with the animation playing then (kind 11: u8 n, n x {u16 id, u32 anim}; anim 0:
// the hold ended, and a mirror's hold taken for it ends too); a
// guest attaches the item again on its mirror while it plays that animation (Citizen_SetHandheldItem, its own hand:
// the modifier is bound to the mirror's animation and ends with it, as on the host), and held_items_tick leaves such
// a hold to its animation (8 s at most). `coop.fix tribe.mirror_hold off`: nothing sent or done.
bool g_mirror_hold_fix = true;
COOP_FIX("tribe.mirror_hold", "BUG-0914", g_mirror_hold_fix, "tribe: a guest's mirror swings its staff or instrument when the host's member does (it stood with the item in the resting grip)");
constexpr double kHoldBoundMax = 8.0;
// BUG-0962: the host's hold is the truth for a mirror's hand too. A gesture's own event can take the item on the mirror
// where the host's member does not take it (lab, the chief's 0x04C6CB02: the mirror held its staff for 1.66 s, the
// host's member never; the same_view stills showed the staff in the hand on one game and upright on the other). Such a
// hold (not bound to a host's hold, none of the host's known for the member) ends after kHoldUnsent s instead of
// BUG-0906's 1.5 s; a hold the host's member also takes comes with the host's start 0.0-0.07 s after the mirror's own
// (lab) and is bound then. `coop.fix tribe.mirror_hold_only off`: 1.5 s as before.
static bool g_hold_only_fix = true;
COOP_FIX("tribe.mirror_hold_only", "BUG-0962", g_hold_only_fix,
         "tribe: a guest's mirror lets go of an item its own animation took when the host's member did not take it (the staff was in the hand on one game only)");
constexpr double kHoldUnsent = 0.35;
// BUG-0971: the change animation 0x02C39200 (Citizen's animated change 0xC255E0) is where a member takes an item into
// its hand (or another one): the host's member has it at the animation's event, the hold starts with it, and the
// mirror's item (the replicated field, a few frames later) is still 0 or the old one when the host's start comes:
// hold_try found no item and the mirror played the change empty-handed (lab: `coop.tribe holdtrace`, the start came
// 0.1-0.3 s after the host's hold began, the mirror's item 0 until it ended). The host sends the item with the start
// (kind 12: kind 11's entries + u8 item) and the mirror takes that item. `coop.fix tribe.hold_item off`: kind 11, the
// mirror's own item.
bool g_hold_item_fix = true;
COOP_FIX("tribe.hold_item", "BUG-0971", g_hold_item_fix,
         "tribe: a guest's mirror takes the item the host's member takes in its change animation (it played the change with the hand empty)");
// BUG-0968: a host's hold whose animation the mirror does not play could never bind (hold_try waits for the mirror to
// play it): lab, `coop.tribe holdtrace` of the guest's chief: the host went from its gesture 0x04C6CB02 straight into
// the change 0x02C39200 and held, the relay brought no start of it (or the mirror's gesture went on), and every start
// (again once a second) found the mirror in the gesture until the host's end; the same_view still showed the host's
// chief with the torch raised in its hand and the guest's copy in another pose without it. The host's start says what
// its member plays now: a mirror that plays another animation starts that one (at most once a second) and binds the
// hold to it. `coop.fix tribe.hold_anim off`: it waits for the relay as before.
bool g_hold_anim_fix = true;
COOP_FIX("tribe.hold_anim", "BUG-0968", g_hold_anim_fix,
         "tribe: a guest's mirror plays the animation the host's member holds its item in when it played another one (the chief raised its torch on one game only)");
// BUG-0992: a start over a non-looping animation (a gesture 0x04C6CB02, 2-46 s) waits until that one ends, on the
// host too (its change began 1.1 s after its own start call). (a) The mirror's gesture began later than the host's, so
// it ended later: the host's change and its 0.35-0.43 s hold were over before the mirror's change began (0.7.13 final
// check, holdtrace `P 02C39200 04C6CB02`, `t` until `E`). Now a fresh host hold whose animation the mirror still does
// not play 0.2 s after hold_try started it is held in the animation the mirror plays (trace Q). (b) A relayed restart
// of the gesture the mirror plays waited as a pending track of the same id, and the attach bound the hold to that
// pending track (0xC240E0 looks there first): no hold for the host's 2.7-4.2 s. Now the pending tracks are ended
// first (the game's 0xA02B00, trace W) and the hold binds to the running one. docs/re/tribe.md#tribe-anim-queued-start.
// `coop.fix tribe.hold_queued off`: neither.
bool g_hold_queued_fix = true;
COOP_FIX("tribe.hold_queued", "BUG-0992", g_hold_queued_fix,
         "tribe: a guest's mirror holds its item while the host's member does even when the mirror's own gesture still runs (a short hold in the change animation was missed)");
static uint32_t g_hold_only_ended;
void hold_try(GEntry& e, double now);
HoldEv g_hold_ev[4096];
uint32_t g_hold_ev_n;
void hold_ev(uint16_t id, char what, uint32_t a, uint32_t b)
{
    g_hold_ev[g_hold_ev_n++ % 4096] = {H->time_seconds(), id, what, a, b};
}
// guest, a few times a second: the mirrors' items settle into the grip
// BUG-0993: the mirror's own holds are looked at every frame (they were looked at with the rest every 0.25 s: the
// cut came 0.5-0.85 s after the hold began, over 1.0 s at 26 fps with hitches); hold_try keeps the 0.25 s tick.
// `coop.fix tribe.hold_cut_frame off`: everything every 0.25 s as before.
static bool g_hold_cut_frame_fix = true;
COOP_FIX("tribe.hold_cut_frame", "BUG-0993", g_hold_cut_frame_fix,
         "tribe: a guest's mirror lets go of an item its own animation took 0.35 s after it took it at any frame rate (the 0.25 s tick made it 0.5-1.05 s)");
// tests (BUG-1105): for 3 s after `coop.tribe hand <id> <item>` the guest looks every frame (whatever the switches)
// at the mirror's hand: trace B on each frame a hold modifier begins (a new handle), the start of the hold a cut
// belongs to (the trace's O comes on held_items_tick's 0.25 s tick with tribe.hold_cut_frame off)
uint16_t g_hand_probe_id;
uint32_t g_hand_probe_handle;
double g_hand_probe_until;
static void hand_probe_tick(double now)
{
    if (!g_hand_probe_id || now > g_hand_probe_until || now < g_hand_probe_until - 3.0) {
        g_hand_probe_id = 0;
        return;
    }
    void* c = member_mirror(g_hand_probe_id);
    uint32_t h = has_body(c) ? hold_modifier(c, false) : 0;
    if (h && h != g_hand_probe_handle)
        hold_ev(g_hand_probe_id, 'B', h, 0);
    g_hand_probe_handle = h;
}
// BUG-1335: a hand item whose effect creature.fx_bone did not make (the bone was not in the body yet): the game wrote
// the item (+0xFFC) and attached nothing; once the body has the bone the item is attached again with the game's call
// (the fields' sync sees the item as the host's and never sends it again). Every game, in Tribe; 30 s at most.
struct HandRepair {
    void* c;
    uint32_t serial;
    double since;
};
static std::vector<HandRepair> g_hand_repair;
HandPlant g_hand_plant;
static void hand_repair_tick(double now)
{
    if (g_hand_plant.c) { // (tests: the planted call faulted and unwound past the command)
        if (kept_alive(g_hand_plant.c, g_hand_plant.serial, kMemberNoun))
            F<int>(g_hand_plant.c, kHandOff) = g_hand_plant.hand;
        g_hand_plant = {};
    }
    for (auto& [c, serial] : game::take_fx_bone_skipped()) {
        bool known = false;
        for (auto& r : g_hand_repair)
            known |= r.c == c && r.serial == serial;
        if (!known && g_hand_repair.size() < 64)
            g_hand_repair.push_back({c, serial, now});
    }
    for (auto it = g_hand_repair.begin(); it != g_hand_repair.end();) {
        void* c = it->c;
        if (now - it->since > 30 || !a_SetHandheldItem || !kept_alive(c, it->serial, kMemberNoun) || !is_citizen(c)) {
            it = g_hand_repair.erase(it);
            continue;
        }
        if (!hand_ready(c)) {
            ++it;
            continue;
        }
        int item = F<int>(c, kHeldItemOff);
        if (item > 0 && item <= 0x17 && hand_model(c) == 0) {
            set_hand(c, item);
            g_hand_repairs++;
            H->log(M, SK_INFO, "tribe: member #%u's item %d is in its hand now that its body has the bone (%.1f s later, BUG-1335)",
                   member_id(c), item, now - it->since);
        }
        it = g_hand_repair.erase(it);
    }
}
static void held_items_tick(double now)
{
    hand_probe_tick(now);
    static double last;
    bool slow = !(now - last < 0.25 && now >= last);
    if (!slow && !g_hold_cut_frame_fix)
        return;
    if (slow)
        last = now;
    if (!g_held_item_fix) {
        g_hold_seen.clear();
        return;
    }
    g_guest.mirrors.for_each([&](GEntry& e_) {
        const uint16_t id = e_.id;
        GMember& m = e_.data;
        void* c = e_.ref.p;
        if (slow && m.hold_anim && g_mirror_hold_fix) // BUG-0914: a hold that came before the mirror had the item or the animation
            hold_try(e_, now);
        uint32_t h = mirror_ok(e_) && has_body(c) && F<int>(c, kHeldItemOff) ? hold_modifier(c, false) : 0; // (BUG-0955)
        if (h)
            m.hold_set_at = 0; // (BUG-1347: started)
        else if (slow && g_hold_retry_fix && g_mirror_hold_fix && m.hold_bound && m.hold_set_at > 0 &&
                 now - m.hold_set_at >= kHoldRetry && now >= m.hold_set_at && a_SetHandheldItem && mirror_ok(e_) &&
                 has_body(c) && !game::is_dead(c)) {
            uint32_t cur = 0;
            int item = F<int>(c, kHeldItemOff);
            if (m.hold_retries >= kHoldRetries || item <= 0 || !game::drawn_state(c, nullptr, &cur) ||
                cur != m.hold_anim_bound) {
                m.hold_set_at = 0; // (another animation now, or tried enough: the host's resend binds it)
            } else if (hand_ready(c)) {
                float sc = read_item_scale(c);
                clear_pending_same(c, cur);
                set_hand(c, item);
                if (g_item_scale_fix && sc != 1.f)
                    apply_item_scale(c, sc);
                m.hold_retries++;
                m.hold_set_at = now;
                g_anim.hold_retried++;
                hold_ev(id, 'A', cur, m.hold_retries);
            }
        }
        auto it = g_hold_seen.find(id);
        if (!h) {
            if (it != g_hold_seen.end())
                g_hold_seen.erase(it);
        } else if (it == g_hold_seen.end() || it->second.handle != h || now < it->second.since) {
            g_hold_seen[id] = {h, now};
            uint32_t cur = 0;
            game::drawn_state(c, nullptr, &cur);
            hold_ev(id, 'O', cur, m.hold_bound);
        } else if (g_hold_only_fix && g_mirror_hold_fix && !m.hold_bound && !m.hold_anim &&
                 now - it->second.since >= kHoldUnsent) { // BUG-0962: no host hold for it
            hold_ev(id, 'C', 0, 0);
            hold_modifier(c, true);
            g_hold_seen.erase(it);
            g_hold_only_ended++;
        } else if (now - it->second.since >= kHoldMax) {
            uint32_t cur = 0;
            if (g_mirror_hold_fix && m.hold_bound && now - m.hold_at < kHoldBoundMax && game::drawn_state(c, nullptr, &cur) &&
                cur == m.hold_anim_bound)
                return; // BUG-0914: taken in the host's animation, it ends with it
            m.hold_bound = false;
            hold_ev(id, 'M', 0, 0);
            hold_modifier(c, true);
            g_hold_seen.erase(it);
        }
    });
    if (g_hold_seen.size() > 512)
        g_hold_seen.clear();
}
static void apply_held_item(void* c, float v)
{
    int item = (int)std::lround(v) - 1, cur = has_body(c) ? F<int>(c, kHeldItemOff) : -1;
    if (cur < 0 || !a_SetHandheldItem || item < 0 || item > 0x17 || cur == item)
        return;
    if (!hand_ready(c)) { // BUG-1335 (the next round tries again)
        g_hand_waits++;
        return;
    }
    set_hand(c, item);
    if (g_item_sets++ < 20)
        H->log(M, SK_INFO, "tribe: member #%u's hand: item %d -> %d as in the host's game (BUG-0906)", member_id(c), cur, item);
}
// tests: 1 when the game has a model attached to the member's hand (what is drawn), 0 none, -1 unknown
int hand_model(void* c)
{
    if (!has_body(c) || !a_AttachmentOf)
        return -1;
    uint8_t* rec = thiscall<uint8_t*>(a_AttachmentOf, c, kHandKeyBase + F<uint32_t>(c, kHandOff));
    return rec && skg::readable(rec + 0x8C, 4) && *(void**)(rec + 0x8C) ? 1 : 0;
}
static const fields::Field kHeldItem{fields::F_HELD_ITEM, "item", read_held_item, apply_held_item, 0.01f};
static const fields::Field kItemFields[] = {kHeldItem};

// BUG-0912: the game sizes the item's model when it attaches it (0xC250A0: the scale of the model's transform = the
// height of the member's bounding box, spatial vt 0x68 [+0x14] - [+8], times the tuning float 0x1581334) and never
// again. The host's chief got its staff at its own size and was made 1.3x by the mod afterwards (chiefs_tick); its
// mirror gets the staff when it already has the host's 1.3x size: the staff was 1.3x bigger on the guest's screen
// (tribe_held_item's key frames, lab 20261110-214352). The size of the held model is a replicated field of the
// members (its own kind and switch): the guest sets the host's on its mirror's item, the way the attach does (the
// model's transform read, its scale set, written back). 1 stands for "no model". `coop.fix tribe.item_scale off`:
// the mirror's own item size.
bool g_item_scale_fix = true;
COOP_FIX("tribe.item_scale", "BUG-0912", g_item_scale_fix, "tribe: what a guest's mirror of a member holds has the size it has in the host's game (the chief's staff was 1.3x bigger)");
struct ItemXf { // Math Transform as the game passes it to a model (0x38 bytes)
    uint16_t flags, count;
    float offset[3];
    float scale;
    float rot[9];
};
static void* hand_item_model(void* c)
{
    if (!has_body(c) || !a_AttachmentOf || !F<int>(c, kHeldItemOff))
        return nullptr;
    uint8_t* rec = thiscall<uint8_t*>(a_AttachmentOf, c, kHandKeyBase + F<uint32_t>(c, kHandOff));
    void* m = rec && skg::readable(rec + 0x8C, 4) ? *(void**)(rec + 0x8C) : nullptr;
    return m && skg::readable(m, 4) && skg::readable(*(void**)m, 0x48) ? m : nullptr;
}
float read_item_scale(void* c)
{
    void* m = hand_item_model(c);
    if (!m)
        return 1.f;
    ItemXf t{};
    vcall<void>(m, 0x20 / 4, &t); // the model's transform (as 0xC2553E)
    return std::isfinite(t.scale) && t.scale > 0.001f && t.scale < 1000.f ? t.scale : 1.f;
}
void apply_item_scale(void* c, float v)
{
    void* m = hand_item_model(c);
    if (!m || !(v > 0.01f && v < 200.f))
        return;
    ItemXf t{};
    vcall<void>(m, 0x20 / 4, &t);
    t.scale = v;
    t.count++; // (as 0xC25556: the transform's change count)
    vcall<void>(m, 0x18 / 4, &t);
}
static const fields::Field kItemScale{fields::F_ITEM_SCALE, "item_scale", read_item_scale, apply_item_scale, 0.02f};
static const fields::Field kItemScaleFields[] = {kItemScale};

// BUG-0935: a guest's roster showed every member card red with an empty bar while the host's were well. A member's
// hunger is cCreatureBase +0xBBC (mHunger, 0..100; +0xBB8 mHungerDelta, -0.28 a second; +0xB90 mHungerDelayTimer). Its
// update 0xC0D5A0 (cCreatureBase vt, every frame) adds delta x dt and, below 1, has the member starve: TakeDamage of its
// combatant (+0x5A8 vt 0x18) every frame. The member eats in its behaviour tree (0xC22C40: hunger up, AddHealth, the
// tribe's food bundles down). A guest's mirror runs no behaviour tree (h_BTTick), so it never ate: its hunger ran down to
// 0 in about 6 minutes and the game showed the member starving (the card red, the bar empty) and took health off it,
// which the snapshot put back. Hunger is a replicated field of the members (its own kind and switch), set with the
// game's clamped setter 0xC0BB30. `coop.fix tribe.mirror_hunger off`: the mirror's own hunger.
bool g_hunger_fix = true;
COOP_FIX("tribe.mirror_hunger", "BUG-0935", g_hunger_fix, "tribe: a guest's mirror of a member is as hungry as the host's member (the roster's cards were red, starving, on the guest)");
uintptr_t a_SetHunger;
uint32_t g_hunger_sets;
static float read_hunger(void* c) { return has_body(c) ? F<float>(c, kHungerOff) : 100.f; }
// BUG-1187: a member that is full (hunger 100, an adult) starts its "full" clock (+0xB90, the game's clock: +0x18
// running) and the hunger update 0xC0D5A0 sets 100 again every frame until that clock passes the member's delay
// (+0xB8C ms); then it stops the clock and the hunger runs down. A mirror the guest's game spawned is full when it is
// made (and so is one the host's value made full): for its own delay the update put 100 back over the host's value
// every frame (lab: "member #3's hunger 100.0 -> 0.0" applied 8 times, the mirror at 100 while the host's member
// starved). The host's member is the one that ate or is full: below 100 on the host, the mirror's full clock ends
// (the game's own stop 0xBC3280, as the update does when the delay is over) before the setter.
// `coop.fix tribe.mirror_hunger_full off`: the mirror's own full delay (0.7.14).
bool g_hunger_full_fix = true;
COOP_FIX("tribe.mirror_hunger_full", "BUG-1187", g_hunger_full_fix, "tribe: a guest's mirror that is full in its own game (a mirror just made) is as hungry as the host's member at once, not 100 for its own full time");
uintptr_t a_ClockStop, a_ClockElapsed;
uint32_t g_hunger_full_ended;
bool end_full_delay(void* c)
{
    uint8_t* clk = (uint8_t*)c + kHungerClockOff;
    if (!a_ClockStop || !clk[0x18])
        return false;
    thiscall<void>(a_ClockStop, clk);
    g_hunger_full_ended++;
    return true;
}
static void apply_hunger(void* c, float v)
{
    if (!has_body(c) || !a_SetHunger || !(v >= 0.f && v <= 100.f))
        return;
    if (g_hunger_sets++ < 10)
        H->log(M, SK_INFO, "tribe: member #%u's hunger %.1f -> %.1f as in the host's game (BUG-0935)", member_id(c),
               F<float>(c, kHungerOff), v);
    if (g_hunger_full_fix && v < 100.f && end_full_delay(c) && g_hunger_full_ended <= 10) // BUG-1187
        H->log(M, SK_INFO, "tribe: member #%u's full time ends as in the host's game (hunger %.1f, BUG-1187)", member_id(c), v);
    thiscall<void>(a_SetHunger, c, v);
}
static const fields::Field kHunger{fields::F_HUNGER, "hunger", read_hunger, apply_hunger, 0.02f, 0.f, 100.f}; // (0: starving, BUG-1043)
static const fields::Field kHungerFields[] = {kHunger};

int tool_type(void* tool) { return vcall<int>(tool, 0x58 / 4); }

void for_each_player_tool(void (*fn)(void* tool, int type, const net::Vec3& pos, void* user), void* user)
{
    void* t = g_ok ? player_tribe() : nullptr;
    if (!t || IsBadReadPtr((uint8_t*)t + kToolsOff, 8))
        return;
    void** b = F<void**>(t, kToolsOff);
    void** e = F<void**>(t, kToolsOff + 4);
    if (!b || e < b || e - b > 64 || IsBadReadPtr(b, (e - b) * 4))
        return;
    std::vector<void*> v(b, e); // a copy: fn may make a tool (the vector grows)
    for (void* tool : v) {
        net::Vec3 p;
        if (tool && game::object_alive(tool) && game::object_position(tool, p))
            fn(tool, tool_type(tool), p, user);
    }
}

// the rotation of the model's up (+Z) onto the planet's up at p (the planet's centre is the origin)
net::Quat upright(const net::Vec3& p)
{
    float l = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (l < 1.f)
        return {0, 0, 0, 1};
    float nx = p.x / l, ny = p.y / l, nz = p.z / l;
    net::Quat q{-ny, nx, 0.f, 1.f + nz};
    float ql = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (ql < 1e-4f)
        return {1, 0, 0, 0};
    return {q.x / ql, q.y / ql, q.z / ql, q.w / ql};
}

void* make_player_tool(int type, const net::Vec3& pos)
{
    void* t = g_ok ? player_tribe() : nullptr;
    if (!t || type < 1 || type > 10 || !a_ToolModelReset || !a_SetToolPurchased)
        return nullptr;
    // the palette's placement (0xD13C05): CreateTool (vt 0xB4), SetTribe (tool vt 0x64), 0xC9D2D0; the
    // finish (0xD10F90) puts it into a layout slot of the village and sets the purchased bit
    void* tool = vcall<void*>(t, 0xB4 / 4, type);
    if (!tool)
        return nullptr;
    vcall<void>(tool, 0x64 / 4, t);
    thiscall<void>(a_ToolModelReset, tool);
    void* sp = game::cast(tool, 0x1186577);
    if (sp) {
        float p[3] = {pos.x, pos.y, pos.z};
        net::Quat r = upright(pos);
        float q[4] = {r.x, r.y, r.z, r.w};
        vcall<void>(sp, 0x38 / 4, (const float*)p);
        vcall<void>(sp, 0x3C / 4, (const float*)q);
    }
    thiscall<void>(a_SetToolPurchased, t, type);
    return tool;
}

std::vector<void*> tribe_tools(void* t)
{
    std::vector<void*> v;
    if (!t || IsBadReadPtr((uint8_t*)t + kToolsOff, 8))
        return v;
    void** b = F<void**>(t, kToolsOff);
    void** e = F<void**>(t, kToolsOff + 4);
    if (!b || e < b || e - b > 64 || IsBadReadPtr(b, (e - b) * 4))
        return v;
    for (void** it = b; it < e; ++it)
        if (*it && game::object_alive(*it))
            v.push_back(*it);
    return v;
}
static bool tool_dragged(void* tool) { return F<uint8_t>(tool, kToolDragOff) != 0; }
void* g_test_dragged; // tests: the tool `coop.tribe buy` drags

// this game's tribe of the host's index i (the host: its own list; a guest: the mapping of the last tribe state)
void* tribe_of_index(int i)
{
    if (i < 0)
        return nullptr;
    if (is_host()) {
        auto ts = all_tribes();
        return i < (int)ts.size() && i < 32 ? ts[i] : nullptr;
    }
    void* t = i < (int)g_guest.local_tribe_of.size() ? g_guest.local_tribe_of[i] : nullptr;
    if (t && g_world_map_fix) { // BUG-0672: a tribe of this world only (the entry may be of a world that is gone)
        auto ts = all_tribes();
        if (std::find(ts.begin(), ts.end(), t) == ts.end()) {
            if (g_map_stale_skipped++ < 8)
                H->log(M, SK_WARN, "tribe: the host's tribe %d maps to %p, no tribe of this world: not used (BUG-0672)", i, t);
            return nullptr;
        }
    }
    return t;
}

void for_each_tool(bool all, ToolFn fn, void* user)
{
    if (!g_ok)
        return;
    void* pt = player_tribe();
    std::vector<void*> ts;
    if (all)
        ts = all_tribes();
    else if (pt)
        ts.push_back(pt);
    for (size_t ti = 0; ti < ts.size() && ti < 64; ti++) {
        void* t = ts[ti];
        int ix = -1;
        if (is_host())
            ix = all ? (ti < 32 ? (int)ti : -1) : -1;
        else
            for (size_t i = 0; i < g_guest.local_tribe_of.size(); i++)
                if (g_guest.local_tribe_of[i] == t) {
                    ix = (int)i;
                    break;
                }
        if (is_host() && !all) { // the player tribe's index in the host's table
            auto at = all_tribes();
            auto f = std::find(at.begin(), at.end(), t);
            ix = f != at.end() && f - at.begin() < 32 ? (int)(f - at.begin()) : -1;
        }
        for (void* tool : tribe_tools(t)) { // a copy: fn may make or remove a tool
            net::Vec3 p;
            if (tool_dragged(tool) || !game::object_position(tool, p))
                continue;
            fn(tool, tool_type(tool), ix, t == pt, p, user);
        }
    }
}

void* make_tool_in(void* t, int type, const net::Vec3& pos)
{
    if (!t || type < 1 || type > 10 || !a_ToolModelReset || !a_SetToolPurchased)
        return nullptr;
    void* tool = vcall<void*>(t, 0xB4 / 4, type);
    if (!tool)
        return nullptr;
    vcall<void>(tool, 0x64 / 4, t);
    thiscall<void>(a_ToolModelReset, tool);
    void* sp = game::cast(tool, 0x1186577);
    if (sp) {
        float p[3] = {pos.x, pos.y, pos.z};
        net::Quat r = upright(pos);
        float q[4] = {r.x, r.y, r.z, r.w};
        vcall<void>(sp, 0x38 / 4, (const float*)p);
        vcall<void>(sp, 0x3C / 4, (const float*)q);
    }
    thiscall<void>(a_SetToolPurchased, t, type);
    return tool;
}

void* make_tool(int tribe, int type, const net::Vec3& pos)
{
    void* t = g_ok ? tribe_of_index(tribe) : nullptr;
    if (!t || net::dist(tribe_pos(t), pos) > 150.f) // a tool stands in its village (a stale mapping: not here)
        return nullptr;
    return make_tool_in(t, type, pos);
}

int has_tool_type(int tribe, int type)
{
    void* t = g_ok ? tribe_of_index(tribe) : nullptr;
    if (!t)
        return -1;
    for (void* tool : tribe_tools(t))
        if (tool_type(tool) == type)
            return 1;
    return 0;
}

bool remove_tool(void* tool)
{
    if (!g_ok || !tool)
        return false;
    for (void* t : all_tribes())
        for (void* x : tribe_tools(t))
            if (x == tool) {
                vcall<void>(t, 0xB8 / 4, tool);
                return true;
            }
    return false;
}

uint32_t g_tools_bought;
bool buy_player_tool(int type, const net::Vec3& pos)
{
    void* pt = g_ok ? player_tribe() : nullptr;
    if (!pt || type < 1 || type > 10 || !std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z) ||
        !(net::dist(tribe_pos(pt), pos) <= 150.f)) // a guest's request: a place near the player tribe only
        return false;
    for (void* tool : tribe_tools(pt)) {
        net::Vec3 p;
        if (tool_type(tool) != type || !game::object_position(tool, p))
            continue;
        if (net::dist(p, pos) < 2.f)
            return false; // there already
        if (tool_dragged(tool))
            return false; // this game's player is placing one of the type now
    }
    // the palette's placement of a type the tribe has: the old one goes (0xD13C05: RemoveObject, RemoveTool)
    for (void* tool : tribe_tools(pt))
        if (tool_type(tool) == type)
            vcall<void>(pt, 0xB8 / 4, tool);
    void* tool = make_tool_in(pt, type, pos);
    if (tool)
        g_tools_bought++;
    return tool != nullptr;
}
// BUG-0505: what a member does in its game (docs/re/tribe.md#tribe-orders): the order record (ai+0x60C list,
// rec+0 action, +0x28 target, +0x38 has target), the behaviour node (ai+0x1D8) with its depth (ai+0x1DC) and
// the leaf node's memory (ai+0x1D0 + 4 * (0xF + 0x2B * (depth - 1)), the 6th argument of a node's enter /
// tick), the walk (spatial c+0xC0: goal mode +0x250, flags +0x270, goal = last of the list +0x1F4..+0x1F8
// minus 0x3C, or +0x208)
// BUG-0531: the food a member carries (its cGameBundleContainer at c+0x34: bundle vector +4..+8, amount at
// bundle +0x134), what the gather node's 0xCEE4E0 sums. CORRECTION (2026-11-19, BUG-1317): read at c+0x3C..+0x40
// before (the container taken at +0x38): `carry=` was 0.0 while a member carried food home (gather state 19,
// docs/re/tribe.md#tribe-gather-block); the container is at +0x34 (0xCEE490(c+0x34), #tribe-carried-bundles)
static float carried(void* c)
{
    void** b = F<void**>(c, 0x38);
    void** e = F<void**>(c, 0x3C);
    float sum = 0;
    if (!b || e < b || e - b > 32 || !skg::readable(b, (e - b) * 4))
        return 0;
    for (void** it = b; it < e; ++it)
        if (*it && skg::readable(*it, 0x138))
            sum += *(float*)((uint8_t*)*it + 0x134);
    return sum;
}
std::string ai_line(void* c)
{
    uint8_t* ai = F<uint8_t*>(c, 0xB4C);
    if (!ai || !skg::readable(ai, 0x620))
        return "no ai";
    char b[400];
    uint8_t* head = ai + 0x60C;
    uint8_t* first = *(uint8_t**)head;
    uint8_t* rec = first != head && skg::readable(first, 0x50) ? first + 8 : nullptr;
    uint32_t node = *(uint32_t*)(ai + 0x1D8);
    int depth = *(int*)(ai + 0x1DC);
    uint32_t mem[8] = {};
    if (depth >= 1 && depth < 16)
        memcpy(mem, ai + 0x1D0 + 4 * (0xF + 0x2B * (depth - 1)), sizeof mem);
    uint8_t* sp = (uint8_t*)c + 0xC0;
    int mode = *(int*)(sp + 0x250);
    uint8_t lf = *(uint8_t*)(sp + 0x270);
    uint8_t* lb = *(uint8_t**)(sp + 0x1F4);
    uint8_t* le = *(uint8_t**)(sp + 0x1F8);
    float* goal = (float*)(lb == le ? sp + 0x208 : le - 0x3C);
    if (!skg::readable(goal, 16))
        goal = (float*)(sp + 0x208);
    net::Vec3 p = game::position(c);
    snprintf(b, sizeof b,
             "rec=%d target=%p has=%d node=%08X depth=%d mem=%d %08X %.1f %.1f %.1f %08X %d %d walk mode=%d flags=%02X "
             "goal=(%.1f %.1f %.1f) r=%.1f pos=(%.1f %.1f %.1f) sel=%u",
             rec ? *(int*)rec : -1, rec ? *(void**)(rec + 0x28) : nullptr, rec ? (int)rec[0x38] : 0, node, depth, (int)mem[0],
             mem[1], *(float*)&mem[2], *(float*)&mem[3], *(float*)&mem[4], mem[5], (int)mem[6], (int)mem[7], mode, lf,
             goal[0], goal[1], goal[2], goal[3], p.x, p.y, p.z, F<uint8_t>(c, kSelectedOff));
    char e[200];
    snprintf(e, sizeof e, " carry=%.1f tool=%d", carried(c), F<int>(c, kHeldToolOff));
    std::string out = std::string(b) + e;
    { // BUG-0531 (diagnostics): the walk's waypoints left, the tree's ticks so far (host), the animation playing
        HEntry* hm = hm_of(c);
        uint32_t an = 0;
        game::drawn_state(c, nullptr, &an);
        // (cCreatureCitizen::Update 0xC24A30 ticks the tree only with +0x135 set, the tree *ai set and no city +0x1010)
        snprintf(e, sizeof e, " path=%d bt=%u anim=%08X upd=%u tree=%d city=%d", lb && le >= lb ? (int)((le - lb) / 0x3C) : -1,
                 !hm ? 0u : hm->data.bt_ticks, an, F<uint8_t>(c, 0x135), *(void**)ai != nullptr,
                 F<void*>(c, 0x1010) != nullptr);
        out += e;
    }
    // BUG-0531: the order's target object: alive in the object list, its noun and place, its distance
    if (void* t = rec ? *(void**)(rec + 0x28) : nullptr) {
        struct Q {
            void* t;
            uint32_t noun;
        } q{t, 0};
        game::for_each_object(
            [](void* o, uint32_t n, void* u) {
                if (o == ((Q*)u)->t) {
                    ((Q*)u)->noun = n;
                    return false;
                }
                return true;
            },
            &q);
        net::Vec3 tp{};
        bool has_pos = q.noun && game::object_alive(t) && game::object_position(t, tp);
        snprintf(e, sizeof e, " tgt_listed=%d tgt_noun=%08X tgt_alive=%d tgt_pos=(%.1f %.1f %.1f) tgt_d=%.1f", q.noun != 0, q.noun,
                 q.noun ? (int)game::object_alive(t) : -1, tp.x, tp.y, tp.z, has_pos ? net::dist(tp, p) : -1.f);
        out += e;
    }
    return out;
}
static bool save_filter(void* o, uint32_t noun); // BUG-0495 (below)
void* SK_THISCALL h_Form(void* self, int arg); // BUG-0483 (below)
Form_t o_Form;
uintptr_t a_Form, a_NpcCtrl;
void theme_hooks_install(); // BUG-0568 (the Tribe theme music relay, below)
void party_hooks_install(); // BUG-0646 (the fire pit party music relay, below)
void party_forget();
static void SK_THISCALL h_BabyBorn(void*, float, float, float); // BUG-0671 (below)
void baby_tick(double now);
static void baby_forget();
static void on_world_effect_start(uint8_t from, uint32_t id, void* fx, const net::Vec3& pos); // BUG-0824 (below)
static void save_start();
static void save_end();

void on_idle_anim(void* c, uint32_t anim);
static uintptr_t a_TribeGroundStamp; // BUG-0947 (village_stamps_tick)
bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    fields::add_kind({fields::K_TRIBE_MEMBER, "tribe_member", nullptr, kMemberFields, 2, member_each, member_mirror,
                      [] { return active(); }});
    fields::add_kind({fields::K_TRIBE_TOOL, "tribe_member_tool", nullptr, kToolFields, 1, member_each, member_mirror,
                      [] { return active(); }});
    fields::add_kind({fields::K_TRIBE_ITEM, "tribe_member_item", &g_held_item_fix, kItemFields, 1, member_each, member_mirror,
                      [] { return active(); }});
    fields::add_kind({fields::K_TRIBE_ITEM_SCALE, "tribe_member_item_scale", &g_item_scale_fix, kItemScaleFields, 1, member_each,
                      member_mirror, [] { return active(); }});
    fields::add_kind({fields::K_TRIBE_HUNGER, "tribe_member_hunger", &g_hunger_fix, kHungerFields, 1, member_each, member_mirror,
                      [] { return active(); }});
    a_SetHunger = H->sym("SporeCoop::Creature_SetHunger");
    a_ClockStop = H->sym("SporeCoop::Clock_Stop"); // BUG-1187
    a_ClockElapsed = H->sym("SporeCoop::Clock_Elapsed"); // BUG-1187 (coop.tribe hunger)
    a_TribeGroundStamp = H->sym("SporeCoop::cTribe_ApplyGroundStamp"); // BUG-0947
    game::set_idle_anim_listener(on_idle_anim); // BUG-0936
    a_SetHandheldItem = H->sym("SporeCoop::Citizen_SetHandheldItem");
    a_AttachmentOf = H->sym("SporeCoop::Creature_AttachmentOf");
    a_ModifierRemove = H->sym("SporeCoop::CreatureModifiers_Remove");
    a_ClearPending = H->sym("SporeCoop::AnimatedCreature_ClearPending"); // BUG-0992
    a_SetSpecializedTool = H->sym("Simulator::cCreatureCitizen::SetSpecializedTool");
    a_IsLeader = H->sym("SporeCoop::Citizen_IsLeader");
    a_SetChieftain = H->sym("SporeCoop::cTribe_SetChieftain");
    a_ToolModelReset = H->sym("SporeCoop::cTribeTool_PlacementReset");
    a_SetToolPurchased = H->sym("SporeCoop::cTribe_SetToolPurchased");
    a_ToolLayout = H->sym("SporeCoop::cTribe_ToolLayout");
    a_GetPlayerTribe = H->sym("Simulator::cGameNounManager::GetPlayerTribe");
    a_DoAction = H->sym("Simulator::cCreatureCitizen::DoAction");
    a_SetValueVector3 = H->sym("App::Property::SetValueVector3");
    a_BTTick = H->sym("Simulator::BehaviorTreeTick");
    a_DoActionForObjects = H->sym("Simulator::cTribeInputStrategy::DoActionForObjects");
    a_WalkSelected = H->sym("Simulator::cTribeInputStrategy::WalkSelectedTo");
    a_SetSelected = H->sym("Simulator::cCreatureCitizen::SetSelected");
    a_CreateCitizen = H->sym("Simulator::cTribe::CreateCitizen");
    a_AddMember = H->sym("Simulator::cTribe::AddMember");
    a_CitizenVt = H->sym("Simulator::cCreatureCitizen::vftable");
    a_TreeVt = H->sym("Simulator::cTreeData::vftable");
    a_SpawnNpcTribe = H->sym("Simulator::SpawnNpcTribe"); // BUG-0381 (optional)
    a_MaxPop = H->sym("SporeCoop::cTribe_MaxPopulation"); // BUG-0645 (optional)
    g_ok = a_GetPlayerTribe && a_DoAction && a_SetValueVector3 && a_BTTick && a_DoActionForObjects && a_WalkSelected &&
           a_SetSelected && a_CreateCitizen && a_AddMember && a_CitizenVt && a_TreeVt;
    if (!g_ok) {
        H->log(M, SK_WARN, "tribe: symbols missing, tribe co-op disabled");
        return false;
    }
    H->hook_create(M, (void*)a_BTTick, (void*)h_BTTick, (void**)&o_BTTick);
    H->hook_create(M, (void*)a_SetSelected, (void*)h_SetSelected, (void**)&o_SetSelected);
    H->hook_create(M, (void*)a_DoActionForObjects, (void*)h_DoActionForObjects, (void**)&o_DoActionForObjects);
    H->hook_create(M, (void*)a_WalkSelected, (void*)h_WalkSelected, (void**)&o_WalkSelected);
    a_RingDraw = H->sym("SporeCoop::TribeTargetRing_Draw"); // BUG-0570 (optional)
    if (uintptr_t a = H->sym("SporeCoop::TribeTargetRings_Update"))
        H->hook_create(M, (void*)a, (void*)h_RingsUpdate, (void**)&o_RingsUpdate);
    a_BTTerminate = H->sym("SporeCoop::Citizen_BTTerminate"); // BUG-0667 (optional)
    if (uintptr_t a = H->sym("SporeCoop::cTribe_ReleaseSlot")) // BUG-0667 (optional)
        H->hook_create(M, (void*)a, (void*)h_ReleaseSlot, (void**)&o_ReleaseSlot);
    if (uintptr_t a = H->sym("Simulator::cCreatureCitizen::SetTribe")) // BUG-0667 (optional)
        H->hook_create(M, (void*)a, (void*)h_SetTribe, (void**)&o_SetTribe);
    if (uintptr_t a = H->sym("SporeCoop::Creature_AskGroupToMakeWay")) // BUG-1460 (optional)
        H->hook_create(M, (void*)a, (void*)h_MakeWay, (void**)&o_MakeWay);
    if (uintptr_t a = H->sym("SporeCoop::cCreatureCitizen_Teardown")) // BUG-0667 (optional)
        H->hook_create(M, (void*)a, (void*)h_CitizenTeardown, (void**)&o_CitizenTeardown);
    a_Form = H->sym("SporeCoop::NpcTribeController_FormTribe"); // BUG-0483 (optional)
    a_NpcCtrl = H->sym("Simulator::cNpcTribeController::Get");
    if (a_Form)
        H->hook_create(M, (void*)a_Form, (void*)h_Form, (void**)&o_Form);
    theme_hooks_install(); // BUG-0568 (optional)
    party_hooks_install(); // BUG-0646 (optional)
    if (uintptr_t a = H->sym("SporeCoop::TribeStage_BabyBorn")) // BUG-0671 (optional)
        H->hook_create(M, (void*)a, (void*)h_BabyBorn, (void**)&o_BabyBorn);
    effects::set_world_start_observer(on_world_effect_start); // BUG-0824
    coop_init();
    savestrip::add_filter(save_filter); // BUG-0495
    savestrip::add_save_hooks(save_start, save_end);
    species_init(); // BUG-1271
    graduation_init(); // BUG-1325 (optional)
    return true;
}

// ---------------- host: snapshots + orders ----------------
// BUG-0340 (second part, the owner's request): at a player's arrival in Tribe and after a rejoin each
// player's chief gets its own spot: a chief closer to another than the sum of their sizes + 1 m goes to
// its ring spot round the village. Only then (not on later chief changes: no jump in a fight).
// `coop.fix tribe.place_chiefs off`: chiefs stay where the game put them.
static bool g_place_fix = true;
COOP_FIX("tribe.place_chiefs", "BUG-0340", g_place_fix, "tribe: at arrival and after a rejoin every player's chief too close to another gets its own spot round the village");
// BUG-0610: place_chiefs looked at every chief for 60 s after an arrival, also one that walked past another chief: a
// member walking at 6-9 m/s on its player's order was teleported 9-22 m onto its spot (both games saw the jump). Only a
// chief that stands is put on its spot now (as spread_chiefs).
static uint32_t g_place_walking = 0; // chiefs left alone because they walked
static double g_place_due = 0, g_place_until = 0;
static uint32_t g_placed;
static std::unordered_set<void*> g_placed_now; // chiefs placed in this arrival's window
static std::unordered_map<void*, net::Vec3> g_place_first; // BUG-0610: where each chief stood at its first look
void arrival_place_due(double now)
{
    g_placed_now.clear();
    g_place_first.clear();
    g_place_due = now + 3.0;   // the split and the chiefs settle first
    g_place_until = now + 60.0; // a player still loading: the guests' games come in later
}

constexpr uintptr_t kTribeArchetypeOff = 0x550; // cTribe: the archetype SpawnNpcTribe stores (< 0x20)
static uint32_t g_npc_tribes_made, g_npc_tribes_failed;
TribeInfo tribe_info(void* t)
{
    TribeInfo i;
    uint32_t a = F<uint32_t>(t, kTribeArchetypeOff);
    i.arch = a < 0x20 ? (uint8_t)a : 0xFF;
    for (void* c : members(t)) { // a citizen is a cCreatureBase: its species key at +0xB28 (game::species_key)
        game::Key k = game::species_key(c);
        if (k.instance) {
            i.key = k;
            break;
        }
    }
    return i;
}

// BUG-0960: when every member of the player tribe is knocked out the game-over caption ("Alas! Your population has
// fallen below the minimum ...") comes on the host only and stands the host's game (busy=cinematic,input_lock,paused)
// until its player answers it; the guest's game showed nothing and played on against the paused host (its orders and
// the mirrors' moves waited for a game that stood). Now the host tells the guests while its caption is up
// (MSG_NPC_ANIM kind 20 {u8 up}, reliable, again every 3 s while up) and a guest's game stands too (the game's own
// cGameTimeManager::Pause with the reason of the game's own UI modals 0x4BF38A7, docs/re/tribe.md#tribe-game-time-pauses) with a co-op note, until the
// host's caption is gone (its OK reloads the tribe for everybody), the host's news stops for 10 s, or the stage or the
// session ends. The guest's own caption is not shown: its OK would reload its own save (BUG-0685).
// Old (`coop.fix tribe.gameover_wait off`): the guest plays on.
static bool g_gameover_wait_fix = true;
COOP_FIX("tribe.gameover_wait", "BUG-0960", g_gameover_wait_fix,
         "tribe: a guest's game waits (paused, with a note) while the host's game-over caption is up");
constexpr uint8_t kGameOverKind = 20;
constexpr uint32_t kGameOverPause = 0x4BF38A7u; // (the game's UI-modal / hint reason; counted: balanced with ours)
static bool g_go_host_up;       // host: its caption is up (last sent)
static double g_go_host_sent = -100;
static bool g_go_paused;        // guest: its game stands for the host's caption (one Pause of ours)
static double g_go_news = -100; // guest: the host's last "up"
static uint32_t g_go_sent, g_go_pauses, g_go_resumes;
bool player_tribe_out(void* t);
// BUG-1081: the players' own hut destroyed in the host's game brings the game's other defeat caption ("Oh, the humanity!
// Your tribal hut has been destroyed! ...", its OK reloads the host's save) and stands the host's game the same way;
// the guests played on. The same wait above covers it: the host's "up" also starts when its player tribe's hut
// (cTribe +0x368 mpHut) is dead (its combatant +0x120 state +0x34 == 2, as cTribe::HutIsDead 0xC90730 reads it).
// Old (`coop.fix tribe.hut_defeat_wait off`): only the tribe knocked out holds the guests.
static bool g_hut_defeat_wait_fix = true;
COOP_FIX("tribe.hut_defeat_wait", "BUG-1081", g_hut_defeat_wait_fix,
         "tribe: a guest's game waits while the host's 'Your tribal hut has been destroyed' caption is up, too");
// BUG-1086: any other stand of the host's game in Tribe (the pause menu, P, the Sporepedia, My Collections, a tutorial
// box or modal: cGameTimeManager +0x48 bit 0 for 1 s) left the guest playing on without a word: its orders went to the
// host's stood game and its members stood until the host went on. The same wait: the host sends its pause
// (MSG_NPC_ANIM kind 21 {u8 up}: a new kind, which a 0.7.13 guest ignores, so it is never told "the tribe is out"),
// the guest's game stands with the note "<host> paused the game" and goes on with the host. A game over (kind 20)
// replaces the pause. Old (`coop.fix tribe.host_pause_wait off`): the guest plays on.
static bool g_host_pause_wait_fix = true;
COOP_FIX("tribe.host_pause_wait", "BUG-1086", g_host_pause_wait_fix,
         "tribe: a guest's game waits (paused, with a note) while the host's game is paused (pause menu, Sporepedia, a modal)");
constexpr uint8_t kHostPauseKind = 21;
enum GameOverWhy : uint8_t { kGoTribeOut = 1, kGoHutDown = 2, kGoHostPaused = 3 };
static double g_hp_since;     // host: its game time stands since (0: it runs)
static double g_hp_note_at;   // guest: when to show the host's pause note (0: shown, or none due)
static uint32_t g_hp_ups;     // host: pauses told
static uint8_t g_go_why;       // host: why its caption is up (last sent); guest: the host's last reason
static uint32_t g_go_hut_ups;  // host: waits started for the hut's defeat

static bool player_hut_down(void* t)
{
    void* h = t && skg::readable(t, 0x36C) ? F<void*>(t, 0x368) : nullptr;
    return h && skg::readable(h, 0x158) && F<int>(h, 0x120 + 0x34) == 2;
}

// host: why the player's game is over (0: it is not), the shared condition of the wait
static uint8_t gameover_why(void* t)
{
    if (player_tribe_out(t))
        return kGoTribeOut;
    if (g_hut_defeat_wait_fix && player_hut_down(t))
        return kGoHutDown;
    return 0;
}

static void* time_manager()
{
    static uintptr_t get = H->sym("Simulator::cGameTimeManager::Get");
    return get ? skg::cdecl_call<void*>(get) : nullptr;
}

// the guest's note while its game stands for the host's (by the host's last reason)
static std::string gameover_note()
{
    std::string host = C.sess ? C.sess->name_of(0) : std::string("the host");
    if (g_go_why == kGoHostPaused)
        return host + " paused the game: yours waits, your orders run when " + host + " goes on";
    return std::string(g_go_why == kGoHutDown ? "The tribal hut is destroyed" : "The tribe is out") + ": waiting for " + host +
           " (host) to answer the caption";
}

static void gameover_pause(bool on, const char* why)
{
    static uintptr_t pause = H->sym("Simulator::cGameTimeManager::Pause"),
                     resume = H->sym("Simulator::cGameTimeManager::Resume");
    void* tm = time_manager();
    if (on == g_go_paused || !tm || !pause || !resume)
        return;
    thiscall<int>(on ? pause : resume, tm, kGameOverPause);
    g_go_paused = on;
    (on ? g_go_pauses : g_go_resumes)++;
    H->log(M, SK_INFO, "tribe: the host's %s is %s: this game %s (%s, %s)", g_go_why == kGoHostPaused ? "pause" : "game-over caption",
           on ? "up" : "gone", on ? "waits" : "goes on", g_go_why == kGoHostPaused ? "BUG-1086" : "BUG-0960", why);
    if (!C.feed || !C.sess)
        return;
    if (g_go_why == kGoHostPaused) { // the pause's note only once it held 1.5 s (a join's or a reload's short stand: none)
        if (on)
            g_hp_note_at = H->time_seconds() + 1.5;
        else if (!g_hp_note_at) // (told: so is the end)
            C.feed(C.sess->name_of(0) + " (host) goes on: so does your game");
        if (!on)
            g_hp_note_at = 0;
        return;
    }
    g_hp_note_at = 0;
    C.feed(on ? gameover_note() : C.sess->name_of(0) + " (host) answered: the game goes on");
}

// host, every send cycle: its caption is up (the input lock and the stood time in Tribe, entered with the player
// tribe out: a knocked-out chief may stand up again under the caption, BUG-0922)
void gameover_host_tick(double now)
{
    if (!g_gameover_wait_fix || !C.sess)
        return;
    bool paused = world::time_paused();
    bool lock = coop_ui::input_lock_now() > 0 && paused;
    // a game over holds its reason while the lock stands (a knocked-out chief may stand up under it, BUG-0922)
    bool over = g_go_host_up && g_go_why != kGoHostPaused;
    uint8_t why = over ? (lock ? g_go_why : 0) : lock ? gameover_why(player_tribe()) : 0;
    g_hp_since = paused ? (g_hp_since ? g_hp_since : now) : 0;
    if (!why && g_host_pause_wait_fix && paused && now - g_hp_since >= 1.0) // BUG-1086 (not a frame's stand)
        why = kGoHostPaused;
    bool up = why != 0;
    if (up == g_go_host_up && why == g_go_why && (!up || now - g_go_host_sent < 3.0))
        return;
    if (up && !g_go_host_up && why == kGoHutDown)
        g_go_hut_ups++;
    if (why == kGoHostPaused && g_go_why != kGoHostPaused)
        g_hp_ups++;
    uint8_t was = g_go_host_up ? g_go_why : 0;
    g_go_host_up = up;
    g_go_why = why;
    g_go_host_sent = now;
    auto send = [](uint8_t kind, uint8_t v) {
        net::Writer w;
        wire::tribe_anim::Wait{kind, v}.write(w); // (C7: u8 kind, u8 why)
        for (auto& p : C.sess->players())
            if (p.id != 0)
                anim_send(p.id, w, true);
        g_go_sent++;
    };
    // the game over: kind 20 {0 gone, else the reason (BUG-1081; a 0.7.13 guest reads any non-zero as up)}; the pause:
    // kind 21 {up} (BUG-1086). A change between them ends the other one first.
    if (was && was != kGoHostPaused && why == kGoHostPaused)
        send(kGameOverKind, 0);
    if (was == kGoHostPaused && why != kGoHostPaused)
        send(kHostPauseKind, 0);
    if (why == kGoHostPaused)
        send(kHostPauseKind, 1);
    else if (why || (was && was != kGoHostPaused))
        send(kGameOverKind, why);
}

// guest, every tick (any mode): the wait ends with the stage, the session or the host's news
static void gameover_guest_tick(double now)
{
    if (!g_go_paused)
        return;
    if (g_hp_note_at && now >= g_hp_note_at && g_go_why == kGoHostPaused && C.feed) { // BUG-1086
        g_hp_note_at = 0;
        C.feed(gameover_note());
    }
    if (!C.sess || !C.sess->connected() || game::mode_id() != modes::kTribe)
        gameover_pause(false, "the stage or the session ended");
    else if (now - g_go_news > 10.0)
        gameover_pause(false, "no news from the host for 10 s");
}

// BUG-1101: in campaign_tribe_noclick (0.7.14 final check) two guests' game time stood (`busy=paused`: no cinematic, no
// input lock, no box, no wait of ours) from a village caption's forced end (BUG-0983) to the end of Tribe (30 / 16
// min): their world froze, proxies hung in the air. Which pause the game left counted was not caught (the runs that
// followed, with every Pause / Resume logged, never left one; docs/re/tribe.md#tribe-caption-pause-leak). ✔ (Ghidra)
// one way the game can leave one: the cinematic manager's own pause (+0x160, a script's "pause" step) is cleared
// without a Resume when the next cinematic starts (0xAD8A90), so a pause step taken while no cinematic plays stays
// counted for good. A safety net instead of the unknown cause: a guest's game time that stands 20 s in Tribe with
// only the reasons a caption or a modal of the game counts (0x4BF38A5 / 0x600C4AE the cinematic's, 0x4BF38A7 the UI
// modal one) while nothing holds it (no cinematic, input lock, modal window, the host's wait) is released: the
// cinematic's own pause through its "resume" message, the rest by Resume, logged with the reasons. The player's own
// stands (P 0x4BF38A8, the pause menu 0x4BF38A4, an editor) are never touched; a released reason whose owner resumes
// it later is paused back to 0 (a count under 0 would swallow the reason's next pause).
// Old (`coop.fix tribe.guest_pause_net off`): the guest's game stands until the stage ends.
static bool g_pause_net_fix = true;
COOP_FIX("tribe.guest_pause_net", "BUG-1101", g_pause_net_fix,
         "tribe: a guest's game time left standing 20 s by a caption's pause that nothing holds is released (logged)");
constexpr double kPauseNetWait = 20.0;
static double g_net_since, g_net_checked, g_net_ran_at;
static uint32_t g_net_releases, g_net_rebalanced;
static std::map<uint32_t, int> g_net_released; // reason -> counts the net resumed (paused back if they go under 0)
static bool net_reason(uint32_t r) { return r == 0x4BF38A5u || r == 0x600C4AEu || r == 0x4BF38A7u; }
static std::string reasons_text(const std::vector<std::pair<uint32_t, int>>& rs)
{
    std::string t;
    char b[32];
    for (auto& [r, c] : rs) {
        snprintf(b, sizeof b, " %08X=%d", r, c);
        t += b;
    }
    return t.empty() ? " none" : t;
}

static void pause_net_release(void*)
{
    if (game::mode_id() != modes::kTribe || world::cinematic_playing() || g_go_paused || host_busy::waiting())
        return;
    std::string before = reasons_text(world::pause_reasons()), how;
    if (world::cinematic_pause_slot() && world::cinematic_pause_resume())
        how = " the cinematic's own pause by its resume;";
    for (auto& [r, c] : world::pause_reasons())
        if (net_reason(r) && c > 0) {
            for (int k = 0; k < c; k++)
                world::time_pause(r, false);
            g_net_released[r] += c;
            char b[48];
            snprintf(b, sizeof b, " %08X x%d by Resume;", r, c);
            how += b;
        }
    g_net_releases++;
    H->log(M, SK_WARN, "tribe: this game's time stood %.0f s with nothing holding it (reasons:%s): released%s now%s, paused=%d "
           "(BUG-1101, %u)", kPauseNetWait, before.c_str(), how.empty() ? " nothing" : how.c_str(),
           reasons_text(world::pause_reasons()).c_str(), (int)world::time_paused(), g_net_releases);
}

// guest, every tick (cheap: once a second)
static void pause_net_tick(double now)
{
    if (now - g_net_checked < 1.0)
        return;
    g_net_checked = now;
    if (game::mode_id() != modes::kTribe) {
        g_net_since = g_net_ran_at = 0;
        g_net_released.clear();
        return;
    }
    auto rs = world::pause_reasons();
    for (auto it = g_net_released.begin(); it != g_net_released.end();) { // the owner's late Resume: back to 0
        int c = 0;
        for (auto& [r, n] : rs)
            if (r == it->first)
                c = n;
        int k = c < 0 ? std::min(-c, it->second) : 0;
        for (int i = 0; i < k; i++)
            world::time_pause(it->first, true);
        if (k) {
            it->second -= k;
            g_net_rebalanced += k;
            H->log(M, SK_INFO, "tribe: the pause %08X released by the net was resumed by its owner too: counted back to 0 "
                   "(BUG-1101)", it->first);
        }
        it = it->second > 0 ? std::next(it) : g_net_released.erase(it);
    }
    bool paused = world::time_paused();
    if (!paused)
        g_net_ran_at = now; // (the stage's own entry stand, BUG-0313, comes before the game ever ran)
    bool held = world::cinematic_playing() || coop_ui::input_lock_now() > 0 || coop_ui::game_modal_up() || g_go_paused ||
                host_busy::waiting(); // C3 (BUG-1142): the wait for the host's pause is ours
    bool own = false; // a reason of the player's (P, the pause menu), an editor's, the stage card's ...
    for (auto& [r, c] : rs)
        own |= c > 0 && !net_reason(r);
    if (!g_pause_net_fix || !paused || held || own || !g_net_ran_at || !is_guest()) {
        g_net_since = 0;
        return;
    }
    if (!g_net_since)
        g_net_since = now;
    if (now - g_net_since < kPauseNetWait)
        return;
    g_net_since = 0;
    H->run_outside_frame(pause_net_release, nullptr);
}

// BUG-1495 (audit TR-9 part 2): MSG_TRIBE_ANIM / MSG_NPC_ANIM are class Module (not dropped at dispatch for a guest
// elsewhere: the module gates them per place) and on_member_anims had no place check: a guest playing another Tribe
// world stood under the host's game-over caption ("The tribe is out: waiting for <host>", kind 20) and heard the host's
// theme and party music (kinds 4, 5, 7) in its own world. Now such a guest drops the plays and a wait's start; the
// stops and a wait's end pass (tribe_logic::elsewhere_drops). `coop.fix tribe.elsewhere_anims off`: 0.7.16.1.
static bool g_elsewhere_anims_fix = true;
COOP_FIX("tribe.elsewhere_anims", "BUG-1495", g_elsewhere_anims_fix,
         "tribe: a guest playing another world is not paused by the host's game-over caption and does not hear the host's music");
static uint32_t g_elsewhere_anims_dropped;
static bool elsewhere_drop(uint8_t kind, bool up)
{
    if (!tribe_logic::elsewhere_drops(g_elsewhere_anims_fix, is_guest() && own_world::elsewhere(), kind, up))
        return false;
    if (g_elsewhere_anims_dropped++ % 50 == 0)
        H->log(M, SK_INFO, "tribe: the host's news (kind %u%s) not applied: this guest plays another world (BUG-1495, %u)", kind,
               up ? ", up" : "", g_elsewhere_anims_dropped);
    return true;
}

// 0.7.17 WP-B1 (audit TR items, BUG-1522): one status line of their counters (tests read it)
static std::string audit_status();

// guest: MSG_NPC_ANIM kinds 2 / 3 (npc.cpp passes them on after the kind byte)
void on_member_anims(uint8_t kind, net::Reader& r)
{
    if (kind == kGameOverKind || kind == kHostPauseKind) { // BUG-0960, BUG-1086 (the host's pause)
        uint8_t why = r.u8();
        if (kind == kHostPauseKind && why)
            why = kGoHostPaused;
        bool up = why != 0;
        if (!r.ok() || !g_ok || !is_guest())
            return;
        if (elsewhere_drop(kind, up)) // BUG-1495
            return;
        bool fix = kind == kGameOverKind ? g_gameover_wait_fix : g_host_pause_wait_fix;
        // C3 (BUG-1142): a 0.7.15 host's pause comes as MSG_HOST_BUSY (host_busy.cpp waits for it): kind 21 is the
        // 0.7.14 host's (its end still ends a wait it started)
        if (kind == kHostPauseKind && up && host_busy::takes_tribe_pause())
            return;
        if (!up && kind == kHostPauseKind && g_go_paused && g_go_why != kGoHostPaused)
            return; // the pause's end does not end a game over's wait
        if (up) {
            g_go_news = H->time_seconds();
            if (g_go_paused && why != g_go_why && fix && C.feed) { // the reason changed while waiting
                g_go_why = why;
                g_hp_note_at = why == kGoHostPaused ? H->time_seconds() + 1.5 : 0;
                if (why != kGoHostPaused)
                    C.feed(gameover_note());
            }
            g_go_why = why;
        }
        if (fix && (!up || game::mode_id() == modes::kTribe))
            gameover_pause(up, "the host's news");
        else if (!fix && !up)
            gameover_pause(false, "the host's news");
        return;
    }
    if (kind == 8 && g_party_stop_fix && g_ok && is_guest() && game::mode_id() != modes::kTribe) { // BUG-0890
        g_party_stops_outside++;
        on_party(kind, r);
        return;
    }
    if (g_ok && (kind == 4 || kind == 5 || kind == 7) && elsewhere_drop(kind, true)) // BUG-1495
        return;
    if (kind == 6 && g_ok && is_guest() && game::mode_id() != modes::kTribe) { // BUG-1496: the stop comes back later
        theme_stop_outside(r);
        return;
    }
    if (!g_ok || !is_guest() || game::mode_id() != modes::kTribe)
        return;
    if (kind >= 4 && kind <= 6) { // BUG-0568: the theme music
        on_theme(kind, r);
        return;
    }
    if (kind == 7 || kind == 8) { // BUG-0646: the fire pit party music
        on_party(kind, r);
        return;
    }
    double now = H->time_seconds();
    if (kind == 11 || kind == 12) { // BUG-0914: a host member took its item in this animation (12: and which, BUG-0971)
        uint8_t n = r.u8();
        for (int i = 0; i < n && r.ok(); i++) {
            uint16_t id = r.u16();
            uint32_t anim = r.u32();
            int item = kind == 12 ? r.u8() : 0;
            if (!r.ok())
                break;
            g_anim.hold_rx++;
            GEntry* it = g_guest.mirrors.find(id);
            if (!g_mirror_hold_fix || !it)
                continue;
            hold_ev(id, anim ? 'S' : 'E', anim, 0);
            if (!anim) { // the host's member put the item back into the grip: so does a mirror that took it for it
                GMember& g = it->data;
                g.hold_anim = 0;
                g.hold_end_at = now;
                if (g.hold_bound && mirror_ok(*it) && has_body(it->ref.p))
                    hold_modifier(it->ref.p, true);
                g.hold_bound = false;
                continue;
            }
            if (now - it->data.hold_end_at < 0.4)
                continue; // (an unreliable start sent before the end, arriving after it)
            it->data.hold_anim = anim;
            it->data.hold_at = now;
            it->data.hold_item = item;
            hold_try(*it, now);
        }
        return;
    }
    if (kind == 16) { // BUG-1072: the members' other playing tracks and idle (u16 id, u32 anim, f32 t, f32 +0xA0, u16 +0xB0)
        if (!g_phase_tracks_fix)
            return;
        uint8_t n = r.u8();
        int rtt = C.sess ? C.sess->rtt_of(0) : -1;
        double d = rtt >= 0 && rtt < 1000 ? rtt / 2000.0 : 0.05;
        for (int i = 0; i < n && r.ok(); i++) {
            uint16_t id = r.u16();
            uint32_t anim = r.u32();
            float t = r.f32(), a0 = r.f32();
            int b0 = r.u16();
            if (!r.ok())
                break;
            g_anim.tracks_rx++;
            GEntry* it = g_guest.mirrors.find(id);
            if (!it || !mirror_ok(*it) || !std::isfinite(t) || t < 0 || !std::isfinite(a0) ||
                game::is_dead(it->ref.p))
                continue;
            uint8_t* tr = nullptr;
            playing_tracks(it->ref.p, [&](uint8_t* x, bool) {
                if (!tr && *(uint32_t*)(x + 0xD8) == anim && *(int*)(x + 0xC0) != 3)
                    tr = x;
            });
            if (!tr)
                continue;
            *(float*)(tr + 0xA0) = a0;
            *(int*)(tr + 0xB0) = b0;
            double& tm = *(double*)(tr + 0xA8), want = t + d;
            if (std::fabs(tm - want) > 0.1) {
                tm = want;
                g_anim.tracks_set++;
            }
        }
        return;
    }
    if (kind == 14 || kind == 15) { // BUG-0988: where in its behaviour animation each host member is (15: BUG-1069,
        // in any animation but the locomotion's, with the track's pass setting: u16 id, u32 anim, f32 t, f32 +0xA0, u16 +0xB0)
        if (kind == 15 && !g_phase_any_fix)
            return;
        uint8_t n = r.u8();
        int rtt = C.sess ? C.sess->rtt_of(0) : -1;
        double d = rtt >= 0 && rtt < 1000 ? rtt / 2000.0 : 0.05;
        for (int i = 0; i < n && r.ok(); i++) {
            uint16_t id = r.u16();
            uint32_t anim = r.u32();
            float t = r.f32();
            float a0 = kind == 15 ? r.f32() : 1.0f;
            int b0 = kind == 15 ? r.u16() : 0;
            if (!r.ok())
                break;
            g_anim.phase_rx++;
            GEntry* it = g_guest.mirrors.find(id);
            if (!g_mirror_phase_fix || !it || !mirror_ok(*it) || !std::isfinite(t) || t < 0 ||
                !std::isfinite(a0))
                continue;
            uint8_t* tr = main_track(it->ref.p);
            if (!tr || *(uint32_t*)(tr + 0xD8) != anim)
                continue;
            if (kind == 14 && track_loops(tr))
                continue;
            if (kind == 15 && (*(float*)(tr + 0xA0) != a0 || *(int*)(tr + 0xB0) != b0)) {
                // BUG-1069: the host's track of it loops (+0xA0 -1.0) or plays once (1.0) and has made +0xB0 passes;
                // the mirror's start of the same id was made the other way (lab probe: the host's chief in a looping
                // staff gesture for 30 s+, its mirror in single passes that ended and came back; or the mirror looping
                // 8 s after the host's single pass ended): the mirror's track gets the host's
                *(float*)(tr + 0xA0) = a0;
                *(int*)(tr + 0xB0) = b0;
                g_anim.phase_unlooped++;
            }
            double& tm = *(double*)(tr + 0xA8), want = t + d;
            // (BUG-1069: any difference with kind 15: the chief's looping staff gesture ran 30 s+ on the host)
            if (std::fabs(tm - want) > 0.1 && (kind == 15 || std::fabs(tm - want) < 5.0)) {
                g_phase_last_off = tm - want;
                tm = want;
                g_anim.phase_set++;
            }
        }
        return;
    }
    if (kind == 10) { // BUG-0936: what the host's members play now that is no behaviour animation
        uint8_t n = r.u8();
        for (int i = 0; i < n && r.ok(); i++) {
            uint16_t id = r.u16();
            uint32_t anim = r.u32();
            uint8_t how = r.u8();
            if (!r.ok())
                break;
            GEntry* it = g_guest.mirrors.find(id);
            if (!g_mirror_idle_fix || !it || !mirror_ok(*it) || game::is_dead(it->ref.p))
                continue;
            GMember& g = it->data;
            uint32_t cur = 0;
            if (now - g.anim_fix_at < 2.0 || now - g.anim_rx_at < 1.5 || !game::drawn_state(it->ref.p, nullptr, &cur) || cur == anim)
                continue;
            // the locomotion's own idle / walk (Creature_ChooseIdleAnim, docs/re/creature.md): started by the animation
            // system's gait, not by the idle refresh, on the host too; never played as a main animation on a mirror
            if (anim == 0x02481DE5u || anim == 0x04866D8Eu || anim == 0x04866DABu || anim == 0x04866DD4u)
                how = 1;
            bool stale = false;
            if (how == 1 && cur != g.relayed_anim) {
                if (!g_stale_anim_fix || cur == 0x02481DE5u || cur == 0x04866D8Eu || cur == 0x04866DABu || cur == 0x04866DD4u)
                    continue; // (its own idle or walk: its locomotion's choice, which may lag the host's by a step)
                stale = true; // BUG-0972: an earlier relayed start that waited behind another animation
            }
            g.anim_fix_at = now;
            hold_ev(id, stale ? 'Z' : 'I', anim, cur); // (tests: the mirror's animation ended for the host's idle / other start)
            if (how == 1) {
                if (stale && a_ClearPending) { // (the waiting starts go too, or the next one comes back the same way)
                    uint8_t* ac = has_body(it->ref.p) ? F<uint8_t*>(it->ref.p, 0xB54) : nullptr;
                    if (ac)
                        thiscall<void>(a_ClearPending, ac);
                    g_anim.stale_cut++;
                }
                game::idle_refresh(it->ref.p, true);
                g_anim.idle_refreshed++;
            } else if (game::play_behaviour_anim(it->ref.p, anim, 0, 1, -1)) {
                g.relayed_anim = anim;
                g_anim.idle_played++;
            }
        }
        return;
    }
    if (kind == 9) { // BUG-0946: the members off their ground on the host
        uint8_t n = r.u8();
        for (int i = 0; i < n && r.ok(); i++) {
            uint16_t id = r.u16();
            float agl = r.f32();
            if (!r.ok())
                break;
            g_anim.air_rx++;
            GEntry* it = g_guest.mirrors.find(id);
            if (it && std::isfinite(agl)) {
                it->data.air_agl = agl;
                it->data.air_at = now;
            }
        }
        return;
    }
    uint8_t n = r.u8();
    for (int i = 0; i < n && r.ok(); i++) {
        uint16_t id = r.u16();
        uint32_t anim = r.u32();
        uint8_t how = 0, a = 1;
        int32_t b = -1;
        if (kind == 2) {
            how = r.u8();
            a = r.u8();
            b = (int32_t)r.u32();
        }
        if (!r.ok())
            break;
        g_anim.anim_relay_rx++;
        GEntry* it = g_guest.mirrors.find(id);
        void* c = !it ? nullptr : it->ref.p;
        if (!c || !mirror_ok(*it) || game::is_dead(c)) // (BUG-0955)
            continue;
        GMember& g = it->data;
        if (kind == 3) { // only when the mirror plays another one, at most every 2 s, not right after a start
            uint32_t cur = 0;
            if (now - g.anim_fix_at < 2.0 || now - g.anim_rx_at < 1.5 || !game::drawn_state(c, nullptr, &cur) || cur == anim)
                continue;
            g.anim_fix_at = now;
            if (game::play_behaviour_anim(c, anim, 0, 1, -1)) {
                g_anim.anim_snap_played++;
                g.relayed_anim = anim; // (BUG-0936)
                hold_try(*it, now);      // (BUG-0914)
            }
            continue;
        }
        g.anim_rx_at = now;
        if (g_hold_anim_fix && now - g.hold_play_at < 0.5 && g.relayed_anim == anim) {
            uint32_t cur = 0;
            if (game::drawn_state(c, nullptr, &cur) && cur == anim) {
                // BUG-0968: hold_try started it for the host's hold just now (the start and the relay came together);
                // started again, the hold bound to the first start ended and the item never came back (lab trace:
                // P, T, R at one moment, then no hold for the host's 2.7 s)
                hold_ev(id, 'D', anim, cur);
                continue;
            }
        }
        if (g_hold_restart_fix && g_mirror_hold_fix && g.hold_bound && g.hold_anim_bound == anim && now - g.hold_at < 0.5) {
            uint32_t cur = 0;
            if (game::drawn_state(c, nullptr, &cur) && cur == anim) {
                // BUG-1069: the host's hold just bound to the mirror's running instance (S, T); the relayed restart of
                // that animation (the host's member restarted it with the hold) would end the bound instance and the
                // hold with it until the next resend (lab: S T R, the hand empty 1.1 s). The mirror goes on in it; its
                // moment comes with the phase (kind 14 / 15).
                hold_ev(id, 'D', anim, cur);
                continue;
            }
        }
        bool played = game::play_behaviour_anim(c, anim, how, a, b);
        {
            uint32_t now_cur = 0;
            game::drawn_state(c, nullptr, &now_cur);
            hold_ev(id, played ? 'R' : 'r', anim, now_cur); // (tests: what the mirror plays right after)
        }
        if (played) {
            g_anim.anim_relay_played++;
            g.relayed_anim = anim; // (BUG-0936)
            hold_try(*it, now);      // (BUG-0914: the hold came before the animation)
        } else
            g_anim.anim_relay_failed++;
    }
}

std::unordered_map<void*, TestVel> g_test_vel;
void test_vel_tick(double now)
{
    for (auto it = g_test_vel.begin(); it != g_test_vel.end();) {
        if (now > it->second.until || !game::object_alive(it->first)) {
            it = g_test_vel.erase(it);
            continue;
        }
        auto& f = F<float[3]>(it->first, 0xC0 + 0x1C8);
        f[0] = it->second.v.x; f[1] = it->second.v.y; f[2] = it->second.v.z;
        ++it;
    }
}

// tests (BUG-1082): `coop.tribe hang <id> <m> <s>` holds a host member <m> m over its ground (not grounded, no velocity)
// for s seconds after every physics update (game::test_hang), as the host's game held members in the campaign's raids
// (the airborne node 0x0485FD10 at 5-15 m with speed 0, an NPC village member 114 m up: docs/re/tribe.md#tribe-member-hang)

// BUG-0671: "a member the guest buys is made in the guest's game only and removed again". A click on the own hut
// (food 10, the game's own price) makes a baby some seconds later: the stage's message 0x63BBC42 (from data, no code
// sends it) reaches SporeCoop::TribeStage_BabyBorn (0xCD74A0), which spawns the member (cTribe vt 0x98), plays its cry and
// fills the HUD. A guest's game makes a baby of its own there, which the mirror reconcile removes (the host never has
// it). The host got one only because the world effects relay (effects.other_stages) replays the guest's hut effect in
// the host's game, whose message makes the host's baby too (lab: with the relay off the guest paid 10 food and the host
// had no new member, 20261107-104533); its owner was the next in the turn split, not the buyer.
// With tribe.guest_baby a guest's game makes no baby of its own while it mirrors the host's tribe: it sends the place
// (MSG_TRIBE_CMD kind CMD_BABY) and the host's game makes the member, the buyer's, and the guest gets its mirror. The
// host takes a baby its own game makes within kBabyPair s at that place (the relayed effect's) as that one instead of a
// second, and makes it itself (the same function) when none comes. The food was paid in the guest's game (the shared
// food). `coop.fix tribe.guest_baby off`: the guest's game makes its own baby (0.7.9).
bool g_guest_baby_fix = true;
COOP_FIX("tribe.guest_baby", "BUG-0671", g_guest_baby_fix, "tribe: a tribe member the guest buys at its hut is born in the host's game and is the guest's");
// BUG-1345: the host makes a guest's baby itself when its own game made none for the guest's ask within kBabyPair s (no
// relayed hut effect, or its message never came: baby_tick). That call named no owner, so tribe.birth_species (BUG-1299)
// gave it the tribe's species and baby_give named the guest only afterwards; tribe.kid_species (BUG-1313) never changes
// a kid, so the guest's kid wore the tribe's species on the host until it grew up while the guest's mirror of it was
// born in the guest's (lab 20261120-041304 tribe_kid_species: first seen 9.1 s after the click, 5.2 s + 4 s). On: the
// asking guest is the owner at the birth. Off: as before.
static bool g_ask_baby_species_fix = true;
COOP_FIX("tribe.ask_baby_species", "BUG-1345", g_ask_baby_species_fix,
         "tribe: a member a guest buys that the host's game makes for its ask is born in that guest's own species");
static constexpr double kBabyPair = 4.0;
static constexpr float kBabyNear = 6.0f;
struct BabyAsk {
    uint8_t from;
    net::Vec3 pos;
    double t;
};
struct BabyMade {
    void* c;
    net::Vec3 pos;
    double t;
    uint32_t serial = 0; // BUG-0951
    uint8_t buyer = 0xFF; // BUG-0824: the guest whose relayed purchase it is (0xFF: not known)
};
static std::vector<BabyAsk> g_baby_asks;   // host: guests' babies its game did not make yet
static std::vector<BabyMade> g_baby_made;  // host: babies its own game made lately that no guest claimed
uint32_t g_babies_sent, g_babies_made, g_babies_paired, g_babies_local;
uintptr_t g_baby_from; // the last caller (tests: which path made it)
static bool baby_near(const net::Vec3& a, const net::Vec3& b)
{
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz < kBabyNear * kBabyNear;
}
void member_claim(void* c, uint8_t owner, uint8_t flag);
static void baby_give(void* c, uint8_t from)
{
    if (HEntry* it = hm_of(c))
        it->data.owner = from;
    else
        g_host.born_for[c] = from;
    member_claim(c, from, 1); // E1 (BUG-1154): the buyer's
}
// host: the new members of the player tribe that one call of the game's function made
// (owner: the buyer when known before the call: BUG-1299, the baby is born in its line species)
static std::vector<void*> baby_call(void* stage, const net::Vec3& p, uint8_t owner = 0xFF)
{
    std::vector<void*> out;
    void* pt = player_tribe();
    auto before = pt ? members(pt) : std::vector<void*>{};
    birth_begin(pt, owner);
    o_BabyBorn(stage, p.x, p.y, p.z);
    if (pt)
        for (void* c : members(pt))
            if (std::find(before.begin(), before.end(), c) == before.end())
                out.push_back(c);
    birth_end(out);
    return out;
}
// BUG-0824: "a member the host buys is given to the guest". Every game makes a baby for every purchase: the hut
// effect 0xDB6F2233 a click on the own hut starts here is relayed to the partners (effects.other_stages), and its data
// sends the stage's message 0x63BBC42 5.2 s later in each game that plays it (lab 20261112-084158: 5.16-5.20 s after
// the start, the own and the relayed one alike). Before, the host took any baby its game made within 4 s of a guest's
// ask (CMD_BABY) as that guest's, its own purchase too, and a guest asked for a baby for the host's relayed effect as if
// it were its own purchase: every member the host bought went to the guest (lab 20261112-083858). Now each game notes
// whose hut effect started (effects' observer: its own or the player that relayed it) and gives a baby of the message
// the oldest such purchase: the host's own is the host's, a guest's relayed one is that guest's (and pairs only with
// that guest's ask), and a guest asks only for its own. A baby without a noted purchase goes the old way.
// `coop.fix tribe.baby_pair off`: the 0.7.12 pairing.
bool g_baby_pair_fix = true;
COOP_FIX("tribe.baby_pair", "BUG-0824", g_baby_pair_fix, "tribe: a member a player buys at the hut is that player's, the host's own purchase too; a guest asks only for its own");
static constexpr uint32_t kHutBuyFx = 0xDB6F2233u; // the effect a click on the own hut starts (lab 20261112-084158)
static constexpr double kBuyKeep = 12.0;           // the baby comes 5.2 s after the effect
static constexpr float kBuyNear = 25.0f;           // the baby is born ~10 m from the effect's place
std::vector<Buy> g_buys; // the purchases (hut effects) of this world whose baby did not come yet, oldest first
uint32_t g_buys_seen, g_buys_own, g_buys_other, g_buys_none;
static void on_world_effect_start(uint8_t from, uint32_t id, void* fx, const net::Vec3& pos)
{
    if (id != kHutBuyFx || !g_ok || game::mode_id() != modes::kTribe)
        return;
    double now = H->time_seconds();
    g_buys.erase(std::remove_if(g_buys.begin(), g_buys.end(), [&](const Buy& b) { return now - b.t > kBuyKeep; }), g_buys.end());
    for (auto& b : g_buys)
        if (b.fx == fx && now - b.t < 1.0) // the same effect reported by a second creator hook (lab 20261112-084503)
            return;
    if (g_buys.size() >= 16)
        g_buys.erase(g_buys.begin());
    g_buys.push_back({from, pos, now, fx});
    g_buys_seen++;
}
// the purchase a baby born at p belongs to (the oldest one near), taken from the list; false: none noted
static bool take_buy(const net::Vec3& p, uint8_t* from)
{
    double now = H->time_seconds();
    for (auto it = g_buys.begin(); it != g_buys.end(); ++it) {
        float dx = it->pos.x - p.x, dy = it->pos.y - p.y, dz = it->pos.z - p.z;
        if (now - it->t <= kBuyKeep && dx * dx + dy * dy + dz * dz < kBuyNear * kBuyNear) {
            *from = it->from;
            g_buys.erase(it);
            return true;
        }
    }
    return false;
}
std::string g_baby_last; // tests: how the last baby went
static void SK_THISCALL h_BabyBorn(void* stage, float x, float y, float z)
{
    g_baby_from = (uintptr_t)__builtin_return_address(0);
    static uintptr_t a_msg_ret = H->sym("SporeCoop::TribeStage_BabyBornMsg_Ret");
    // BUG-0824: whose purchase this baby is (only a baby of the hut's message; the stage's first baby is nobody's)
    uint8_t buyer = 0xFF;
    bool bought = g_ok && g_baby_pair_fix && g_guest_baby_fix && a_msg_ret && g_baby_from == a_msg_ret && C.sess &&
                  take_buy(net::Vec3{x, y, z}, &buyer);
    if (g_baby_pair_fix && g_ok && a_msg_ret && g_baby_from == a_msg_ret && !bought)
        g_buys_none++;
    {
        char b[96];
        snprintf(b, sizeof b, "at (%.1f %.1f %.1f) buyer=%d", x, y, z, bought ? (int)buyer : -1);
        g_baby_last = b;
    }
    bool on = g_ok && g_guest_baby_fix && game::mode_id() == modes::kTribe && stage;
    if (on && is_guest() && !local_play() && !g_guest.mirrors.empty()) {
        if (bought && buyer != C.sess->my_id()) { // BUG-0824: another player's purchase: the host makes it
            g_buys_other++;
            return;
        }
        net::Writer w;
        w.u8(CMD_BABY); w.u8(0); w.u32(0);
        write_target(w, Target{});
        w.f32(x); w.f32(y); w.f32(z);
        C.sess->send_reliable_to(0, net::MSG_TRIBE_CMD, w);
        g_babies_sent++;
        g_cmds_sent++;
        return;
    }
    g_babies_local++;
    if (!(on && is_host())) {
        o_BabyBorn(stage, x, y, z);
        return;
    }
    net::Vec3 p{x, y, z};
    double now = H->time_seconds();
    if (bought && buyer == C.sess->my_id()) { // BUG-0824: the host's own purchase is the host's
        for (void* c : baby_call(stage, p, buyer))
            baby_give(c, buyer);
        g_buys_own++;
        return;
    }
    if (bought) { // BUG-0824: the relayed effect of guest `buyer`: its member; its ask (before or after) pairs with it
        g_buys_other++;
        for (void* c : baby_call(stage, p, buyer)) {
            baby_give(c, buyer);
            auto a = std::find_if(g_baby_asks.begin(), g_baby_asks.end(), [&](const BabyAsk& k) { return k.from == buyer; });
            if (a != g_baby_asks.end()) {
                g_baby_asks.erase(a);
                g_babies_paired++;
            } else
                g_baby_made.push_back({c, p, now, game::object_serial(c), buyer});
        }
        return;
    }
    auto asked = std::find_if(g_baby_asks.begin(), g_baby_asks.end(), [&](const BabyAsk& k) { return baby_near(k.pos, p); });
    for (void* c : baby_call(stage, p, asked != g_baby_asks.end() ? asked->from : (uint8_t)0xFF)) {
        auto a = std::find_if(g_baby_asks.begin(), g_baby_asks.end(), [&](const BabyAsk& k) { return baby_near(k.pos, p); });
        if (a != g_baby_asks.end()) { // the guest's baby, made here by its relayed effect
            baby_give(c, a->from);
            g_baby_asks.erase(a);
            g_babies_paired++;
        } else {
            g_baby_made.push_back({c, p, now, game::object_serial(c)});
        }
    }
}
void baby_on_host(uint8_t from, const net::Vec3& pos)
{
    if (!o_BabyBorn || !std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z) || std::fabs(pos.x) > 1e6 ||
        std::fabs(pos.y) > 1e6 || std::fabs(pos.z) > 1e6 || g_baby_asks.size() >= 16) {
        g_cmds_rejected++;
        return;
    }
    auto m = std::find_if(g_baby_made.begin(), g_baby_made.end(), [&](const BabyMade& k) {
        return g_baby_pair_fix && k.buyer != 0xFF ? k.buyer == from : baby_near(k.pos, pos); // BUG-0824: only that guest's own purchase
    });
    if (m != g_baby_made.end()) { // the host's game made it already (the relayed effect came first)
        if (kept_alive(m->c, m->serial, 0x18EB4B7u)) // (BUG-0951; kCitizenNoun)
            baby_give(m->c, from);
        g_baby_made.erase(m);
        g_babies_paired++;
        return;
    }
    g_baby_asks.push_back({from, pos, H->time_seconds()});
}
void baby_tick(double now)
{
    g_baby_made.erase(std::remove_if(g_baby_made.begin(), g_baby_made.end(), [&](const BabyMade& k) { return now - k.t > kBabyPair; }),
                      g_baby_made.end());
    for (size_t i = 0; i < g_baby_asks.size();) {
        if (now - g_baby_asks[i].t < kBabyPair) {
            i++;
            continue;
        }
        BabyAsk k = g_baby_asks[i];
        g_baby_asks.erase(g_baby_asks.begin() + i);
        static uintptr_t a_Stage = H->sym("SporeCoop::cTribeUI_Get");
        void* stage = a_Stage ? skg::cdecl_call<void*>(a_Stage) : nullptr;
        if (!stage || !player_tribe() || !net::is_player(*C.sess, k.from))
            continue;
        // none came: the host's game makes it (null at the cap: none). BUG-1345: born for the guest that asked (its line
        // species, tribe.birth_species), not named its owner only afterwards
        for (void* c : baby_call(stage, k.pos, g_ask_baby_species_fix ? k.from : (uint8_t)0xFF)) {
            baby_give(c, k.from);
            g_babies_made++;
        }
    }
}
static void baby_forget()
{
    g_buys.clear(); // BUG-0824
    g_host.born_for.clear();
    g_baby_asks.clear();
    g_baby_made.clear();
}

void conquer_on_host(uint8_t from, const net::Vec3& p); // BUG-0879 (below)

// BUG-0495: the objects the mod makes on a guest (the members it spawns as mirrors of the host's, and the
// NPC tribes it makes for the host's with SpawnNpcTribe, BUG-0381) are the host's world, not the guest's:
// they never go into the guest's saves (CLAUDE.md 2). A savestrip filter writes a null reference for them,
// and while the save is written they leave the lists that hold them (a tribe's member vector +0x340 and
// selectable vector +0x354, citizen+0xC0) so no null is read back there on load (BUG-0470's rule). A made
// tribe goes out with its hut (+0x368), its tools (+0x36C, +0x380) and its food mat. Members of the guest's
// own game bound as mirrors stay (they came with the loaded world).
constexpr uint32_t kCitizenNoun = 0x18EB4B7, kHutNoun = 0x1E4DAAE, kToolNoun = 0x18C8F0C, kFoodMatNoun = 0x629BAFE;
std::unordered_map<void*, uint32_t> g_ours_c, g_ours_t; // spawned mirrors / made tribes -> serial
static std::unordered_set<void*> g_strip_now;                  // this save's objects of ours
static uint32_t g_save_stripped, g_save_saves;
void note_ours(std::unordered_map<void*, uint32_t>& m, void* o)
{
    if (m.size() > 512) // (old worlds' pointers)
        for (auto it = m.begin(); it != m.end();)
            it = game::object_alive(it->first) && game::object_serial(it->first) == it->second ? std::next(it) : m.erase(it);
    m[o] = game::object_serial(o);
}
bool ours_in(const std::unordered_map<void*, uint32_t>& m, void* o)
{
    auto it = m.find(o);
    return it != m.end() && game::object_alive(o) && game::object_serial(o) == it->second;
}
struct TookT {
    void* t;
    uintptr_t off;
    void* p;
    void* c;         // the citizen (p is c + 0xC0 in the selectable vector)
    uint32_t serial; // its cGameData serial when it was taken out
};
static std::vector<TookT> g_took_t;
// BUG-1522 (CIV-1's bug, BUG-1416, in Tribe): save_start runs inside the save's first write, save_end only on the next
// frame's savestrip::tick, after that frame's network receive. A host state in between that no longer names a mirror
// called remove_member: the mirror was out of every member vector, so tribe_holding found no tribe and the mirror was
// only forgotten (BUG-0844's refusal; with tribe.member_lifetime off RemoveMember(c, destroy) freed it), and save_end
// put it back: an orphan member nobody mirrors (or a freed pointer in the member and selectable vectors). Now a member
// the host removes during the window goes back first and is then removed the usual way (took_return), and save_end puts
// back only a member still listed as the same object, of a tribe still listed, not in the vector already
// (tribe_logic::took_goes_back). World change and session end clear the list. `coop.fix tribe.save_took_listed off`
// (unsafe): every taken entry back, a removal in the window refused (0.7.16.1).
static bool g_took_listed_fix = true;
COOP_FIX_UNSAFE("tribe.save_took_listed", "BUG-1522", g_took_listed_fix,
                "tribe: a guest's save puts back only the members the game still has (one the host removed during the save goes)");
static bool g_debug_save_kill; // tests (coop.tribe savekill): the next save_end first removes a taken mirror, as a host state
static uint32_t g_took_back, g_took_gone, g_took_returned, g_took_cleared, g_took_killed;
static bool in_vector(void* t, uintptr_t off, void* p)
{
    void** b = F<void**>(t, off);
    void** e = F<void**>(t, off + 4);
    for (void** it = b; it && it < e && e - b < 1024; ++it)
        if (*it == p)
            return true;
    return false;
}
static void took_put(const TookT& t)
{
    void**& e = F<void**>(t.t, t.off + 4);
    if (e < F<void**>(t.t, t.off + 8))
        *e++ = t.p;
}
// a member the host removes while a save has it out of its tribe's vectors: back first (true: it was taken)
bool took_return(void* c)
{
    if (!g_took_listed_fix || g_took_t.empty())
        return false;
    bool any = false;
    for (auto it = g_took_t.begin(); it != g_took_t.end();)
        if (it->c == c) {
            if (game::listed(c, kCitizenNoun) && game::object_serial(c) == it->serial && !in_vector(it->t, it->off, it->p))
                took_put(*it);
            it = g_took_t.erase(it);
            any = true;
        } else
            ++it;
    if (any) {
        g_took_returned++;
        H->log(M, SK_INFO, "tribe: member %p removed while a save has it out of its tribe: back first, then removed (BUG-1522)", c);
    }
    return any;
}
static void took_clear()
{
    if (!g_took_listed_fix || g_took_t.empty())
        return;
    g_took_cleared += (uint32_t)g_took_t.size();
    g_took_t.clear();
}
void took_debug_kill(bool on) { g_debug_save_kill = on; }
std::string took_status()
{
    char b[200];
    snprintf(b, sizeof b, "save_took: fix=%d taken=%u back=%u gone=%u returned=%u cleared=%u killed=%u armed=%d\n",
             (int)g_took_listed_fix, (unsigned)g_took_t.size(), g_took_back, g_took_gone, g_took_returned, g_took_cleared,
             g_took_killed, (int)g_debug_save_kill);
    return b;
}
// the food mat of a tribe: the noun 0x629BAFE object that holds the tribe's pointer (offset found at runtime)
static uintptr_t g_mat_off;
static bool mat_of(void* o, void* t)
{
    if (IsBadReadPtr(o, 0x400))
        return false;
    if (g_mat_off)
        return F<void*>(o, g_mat_off) == t;
    for (uintptr_t off = 0x24; off < 0x400; off += 4)
        if (F<void*>(o, off) == t) {
            g_mat_off = off;
            H->log(M, SK_INFO, "tribe: a food mat holds its tribe at +0x%X (BUG-0495)", (unsigned)off);
            return true;
        }
    return false;
}
static void save_start()
{
    g_strip_now.clear();
    if (!is_guest() || game::mode_id() != modes::kTribe)
        return;
    std::vector<void*> made;
    for (void* t : all_tribes()) {
        bool whole = t != player_tribe() && ours_in(g_ours_t, t);
        if (whole) {
            made.push_back(t);
            g_strip_now.insert(t);
            if (void* hut = F<void*>(t, 0x368))
                g_strip_now.insert(hut);
            for (uintptr_t off : {(uintptr_t)0x36C, (uintptr_t)0x380}) {
                void** b = F<void**>(t, off);
                void** e = F<void**>(t, off + 4);
                for (void** it = b; it && it < e && e - b < 64; ++it)
                    if (*it)
                        g_strip_now.insert(*it);
            }
        }
        for (uintptr_t off : {kMembersOff, (uintptr_t)0x354}) {
            void**& b = F<void**>(t, off);
            void**& e = F<void**>(t, off + 4);
            if (!b || e < b || e - b > 256)
                continue;
            void** w = b;
            for (void** it = b; it != e; ++it) {
                void* c = *it ? (off == kMembersOff ? *it : (uint8_t*)*it - 0xC0) : nullptr;
                if (c && (whole || ours_in(g_ours_c, c))) {
                    g_took_t.push_back({t, off, *it, c, game::object_serial(c)});
                    g_strip_now.insert(c);
                } else
                    *w++ = *it;
            }
            for (void** it = w; it != e; ++it)
                *it = nullptr;
            e = w;
        }
    }
    if (!made.empty()) {
        struct U {
            std::vector<void*>* made;
        } u{&made};
        game::for_each_object(
            [](void* o, uint32_t noun, void* p) {
                if (noun == kFoodMatNoun)
                    for (void* t : *((U*)p)->made)
                        if (mat_of(o, t))
                            g_strip_now.insert(o);
                return true;
            },
            &u);
    }
    if (!g_strip_now.empty()) {
        g_save_saves++;
        H->log(M, SK_INFO, "tribe: %u objects of ours (%u made tribes) leave this save (tribe.save_strip)",
               (unsigned)g_strip_now.size(), (unsigned)made.size());
    }
}
static void save_end()
{
    if (g_debug_save_kill && !g_took_t.empty()) { // tests: as if this frame's host state removed a taken mirror first
        g_debug_save_kill = false;
        for (auto& t : g_took_t) {
            uint16_t id;
            if (t.off != kMembersOff || !g_guest.mirrors.id_of(t.c, id))
                continue;
            H->log(M, SK_INFO, "tribe: savekill: the taken mirror #%u %p removed before the save's end (tests, BUG-1522)", id, t.c);
            g_test_gone.insert(id); // (the host's states skip it from now on: no new mirror binds the member again)
            g_guest.mirrors.detach(id);
            remove_member(t.c); // (as guest_state's sweep: may change g_took_t)
            g_took_killed++;
            break;
        }
    }
    std::vector<void*> tribes;
    if (g_took_listed_fix && !g_took_t.empty() && game::mode_id() == modes::kTribe)
        tribes = all_tribes();
    for (auto& t : g_took_t) { // back at the end of the list (the capacity is the one they left)
        if (g_took_listed_fix) {
            bool tribe_here = std::find(tribes.begin(), tribes.end(), t.t) != tribes.end();
            bool listed = tribe_here && game::listed(t.c, kCitizenNoun);
            if (!tribe_logic::took_goes_back(true, tribe_here, listed, listed && game::object_serial(t.c) == t.serial,
                                             listed && in_vector(t.t, t.off, t.p))) {
                g_took_gone++;
                H->log(M, SK_INFO, "tribe: a member %p taken out for the save is gone or back already: not put back (BUG-1522)", t.c);
                continue;
            }
            took_put(t);
            g_took_back++;
            continue;
        }
        void**& e = F<void**>(t.t, t.off + 4);
        if (game::object_alive(t.t) && e < F<void**>(t.t, t.off + 8))
            *e++ = t.p;
    }
    g_took_t.clear();
    g_strip_now.clear();
}
static bool save_filter(void* o, uint32_t noun)
{
    if (g_strip_now.empty() ||
        (noun != kCitizenNoun && noun != kTribeNoun && noun != kHutNoun && noun != kToolNoun && noun != kFoodMatNoun) ||
        !g_strip_now.count(o))
        return false;
    g_save_stripped++;
    return true;
}

// BUG-0741: the host's NPC tribe controller (FormTribe 0xCC6440) destroys the tribe spawn marker (cGameplayMarker noun
// 0x36BE27E, +0x108 == 0x57F09E5E) it formed a tribe at (cGameNounManager::DestroyInstance); a guest's copy of that
// tribe (make_npc_tribes) never goes through FormTribe, so the guest kept the marker (the detector's unpaired
// gameplay marker): a guest controller that ever formed tribes (tribe.guest_tribes off, a host handover) could form
// one on a site already used. With the fix the guest destroys its marker nearest the copy, within 30 m.
static bool g_marker_fix = true;
COOP_FIX("tribe.copy_marker", "BUG-0741", g_marker_fix,
         "tribe: a guest's copy of the host's new NPC tribe uses up the tribe spawn marker there, as the host's controller did");
uint32_t g_markers_used;
static net::Vec3 marker_pos(void* o)
{
    const float* q = (const float*)((uint8_t*)o + 0x1A8); // mNounPosition (docs/re/creature.md#creature-gameplay-markers)
    return {q[0], q[1], q[2]};
}
static void use_marker_at(const net::Vec3& at)
{
    if (!g_marker_fix)
        return;
    struct Best {
        net::Vec3 at;
        void* o = nullptr;
        float d = 30.f;
    } best{at};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* b = (Best*)u;
            if (noun == 0x36BE27E && F<uint32_t>(o, 0x108) == 0x57F09E5E) {
                float d = net::dist(marker_pos(o), b->at);
                if (d < b->d) {
                    b->d = d;
                    b->o = o;
                }
            }
            return true;
        },
        &best);
    if (!best.o)
        return;
    net::Vec3 mp = marker_pos(best.o);
    game::destroy_instance(best.o);
    g_markers_used++;
    H->log(M, SK_INFO, "tribe: the copy of the host's NPC tribe used up the tribe spawn marker at (%.0f %.0f %.0f), %.1f m off",
           mp.x, mp.y, mp.z, best.d);
}

// BUG-0381: a guest makes each host NPC tribe that has no tribe of this game
// within the mapping's 40 m (guest_state). `p`: the tribe table after the trailer (tag, count,
// count x kTribeInfo). One try per host tribe every 5 s (an unknown species is asked for first).
static std::map<std::pair<int, int>, double> g_npc_tribe_tried; // host tribe position (2 m grid) -> last try
// BUG-1375: a try that found the species missing (asked from the host) waited the full 5 s, though the host's answer
// comes within a round trip: the guest's copy of a new village came 5 s late (campaign_civ_jump: the sync-check's two
// checks 3 s apart saw its five members missing). On: such a try is made again 0.5 s later (want_species asks once per
// 30 s: no flood). Off: 5 s (0.7.15).
static bool g_species_retry_fix = true;
COOP_FIX("tribe.copy_species_retry", "BUG-1375", g_species_retry_fix,
         "tribe: a guest makes its copy of the host's new village right after the host sent the village's species");
constexpr double kNpcTribeRetry = 5.0, kNpcTribeSpeciesRetry = 0.5;
uint32_t g_species_miss_test; // tests (`coop.tribe species_miss <n>`): the next n tries read their species as missing
static std::map<std::pair<int, int>, void*> g_npc_tribe_made;   // ... -> the tribe made for it here
void make_npc_tribes(const uint8_t* p, size_t len, const std::vector<std::pair<net::Vec3, bool>>& htribes,
                            const std::vector<void*>& locals)
{
    uint32_t tag;
    memcpy(&tag, p, 4);
    if (!a_SpawnNpcTribe || tag != kTribeInfoTag || p[4] != htribes.size() || len < 5)
        return;
    double now = H->time_seconds();
    for (size_t i = 0; i < htribes.size() && i < g_guest.local_tribe_of.size(); i++) {
        if (htribes[i].second || g_guest.local_tribe_of[i])
            continue;
        const uint8_t* e = p + 5 + i * kTribeInfo;
        game::Key k;
        memcpy(&k.instance, e, 4);
        memcpy(&k.type, e + 4, 4);
        memcpy(&k.group, e + 8, 4);
        uint8_t arch = e[12];
        if (!k.instance)
            continue; // no member yet: nothing to mirror
        const net::Vec3& hp = htribes[i].first;
        auto cell = std::make_pair((int)std::floor(hp.x / 2), (int)std::floor(hp.z / 2));
        auto made = g_npc_tribe_made.find(cell);
        if (made != g_npc_tribe_made.end()) { // made before (the game may have put it more than 40 m off)
            if (std::find(locals.begin(), locals.end(), made->second) != locals.end()) {
                g_guest.local_tribe_of[i] = made->second;
                continue;
            }
            g_npc_tribe_made.erase(made); // gone (a raid, a load): may be made again
        }
        if (g_npc_tribes_made >= 64)
            continue; // a bound against a loop of tribes made and lost
        auto tried = g_npc_tribe_tried.find(cell);
        if (tried != g_npc_tribe_tried.end() && now - tried->second < kNpcTribeRetry)
            continue;
        g_npc_tribe_tried[cell] = now;
        creations::Key lk = creations::local_species({k.instance, k.type, k.group});
        void* species = game::species_by_key({lk.instance, lk.type, lk.group});
        if (species && g_species_miss_test) { // tests: as if this game lacked it (its answer is here at once)
            g_species_miss_test--;
            species = nullptr;
            H->log(M, SK_INFO, "tribe: the species %08X of the host's NPC tribe %u reads as missing (test)", k.instance, (unsigned)i);
        }
        if (!species) { // a species of the host's world this game lacks: the host sends it
            creations::want_species({k.instance, k.type, k.group});
            g_npc_tribes_failed++;
            if (g_species_retry_fix) // BUG-1375: again as soon as the answer can be here
                g_npc_tribe_tried[cell] = now - kNpcTribeRetry + kNpcTribeSpeciesRetry;
            continue;
        }
        skg::Vector3 pos{hp.x, hp.y, hp.z};
        // cdecl cTribe* SpawnNpcTribe(const Vector3& pos, int archetype, int members, int food, bool, cSpeciesProfile*)
        void* t = skg::cdecl_call<void*>(a_SpawnNpcTribe, &pos, arch == 0xFF ? 0 : (int)arch, 0, 0, 0, species);
        if (!t) {
            g_npc_tribes_failed++;
            continue;
        }
        g_npc_tribes_made++;
        g_guest.local_tribe_of[i] = t;
        note_ours(g_ours_t, t); // BUG-0495
        g_npc_tribe_made[cell] = t;
        use_marker_at(hp); // BUG-0741
        net::Vec3 lp = tribe_pos(t);
        H->log(M, SK_INFO,
               "tribe: the host's NPC tribe %u at (%.0f %.0f %.0f) is not in this game: made it (species %08X, archetype %d) at "
               "%.1f m",
               (unsigned)i, hp.x, hp.y, hp.z, k.instance, arch == 0xFF ? -1 : (int)arch, net::dist(lp, hp));
    }
    if (g_npc_tribe_tried.size() > 256)
        g_npc_tribe_tried.clear();
    if (g_npc_tribe_made.size() > 64)
        g_npc_tribe_made.clear();
}

// BUG-0483: a whole village (tribe, hut, tools, food mat, members) stood in a guest's game only (the
// detector's Tribe:guest_only:tribe and the rest, after the live Creature -> Tribe change of campaign_ct).
// Two ways a guest's game keeps a tribe the host's has not:
// * the guest's own cNpcTribeController forms one (its vt 0x24, 0xCC6440: the nearest spawn marker, the
//   next species of its list, SpawnNpcTribe; docs/re/tribe.md#tribe-npc-forming): the host forms its own at
//   its own time and place and the guests make each of the host's (BUG-0381), so a guest's own
//   forming only adds a village nobody else has. On a guest in a session it does not run.
// * the host's game no longer has the tribe the guest's was mapped to (a tribe gone on the host, or one the
//   guest's game had in its world and the host's not): its members were never touched (the end of a cycle
//   removes only the unknown members of mapped tribes). After 5 s unmapped in complete cycles they go too.
// `coop.fix tribe.guest_tribes off`: the guest's game forms and keeps its own tribes (0.7.4).
static bool g_guest_tribes_fix = true;
COOP_FIX("tribe.guest_tribes", "BUG-0483", g_guest_tribes_fix,
         "tribe: a guest's game forms no NPC tribe of its own, and empties tribes the host no longer has");
uint32_t g_forms_blocked, g_forms_run, g_unmapped_removed;
static std::unordered_map<void*, double> g_unmapped_since;

// BUG-0571: "villages are founded much faster than in the single-player game". The host's NPC tribe controller
// (0xCC6A90, the update of cNpcTribeController) forms the planet's third tribe when its goal history is empty
// (TribeStage +0x40, mGoalProgressHistory: no village allied or conquered yet) as soon as the player tribe's food
// (its vt 0xA4) is over the property 0x4C87E711 (default 100), and then the fourth and the fifth at once (with 3 or 4
// tribes it forms every second until its list is used up): measured the same alone and in a session (2 s after the
// food passed 100, lab 20261105-113252 / 20261105-113623). In a session that food is the whole party's: every player's
// members gather into the one player tribe, and the co-op's own rewards add to it (an epic killed together 80, a
// partner's heal 5), so the threshold came after a fraction of a single player's time (3.8 min in the player's
// session, food under 100 for most of it was the party's). The host's game alone decides (a guest's controller
// forms none, tribe.guest_tribes): it now forms the third tribe on food only when the food is over the threshold
// times the players in the session, the pace of one player's tribe. A goal done still forms it at once, as alone.
// `coop.fix tribe.form_pace off`: the game's own threshold for the party (0.7.7).
bool g_form_pace_fix = true;
COOP_FIX("tribe.form_pace", "BUG-0571", g_form_pace_fix,
         "tribe: new villages appear when the party's food reaches what one player's tribe needs alone, times the players");
uint32_t g_forms_paced;
static double g_pace_checked = -10;
static bool g_pace_hold;
std::string g_pace_last; // tests: the last decision
static bool pace_holds(double now)
{
    if (!g_form_pace_fix || !is_host() || game::mode_id() != modes::kTribe || !C.sess)
        return false;
    if (now - g_pace_checked < 1.0) // (the controller calls again every frame while it is held: decided once a second)
        return g_pace_hold;
    g_pace_checked = now;
    g_pace_hold = false;
    size_t players = C.sess->players().size();
    static uintptr_t a_Stage = H->sym("SporeCoop::cTribeUI_Get"), a_PropInt = H->sym("SporeCoop::App_GetPropInt"),
                     a_Props = H->sym("SporeCoop::TribeProps_ptr");
    void* pt = player_tribe();
    void* stage = a_Stage ? skg::cdecl_call<void*>(a_Stage) : nullptr;
    if (players < 2 || !pt || !stage || !skg::readable((uint8_t*)stage + 0x40, 8))
        return false;
    if (F<void*>(stage, 0x40) != F<void*>(stage, 0x44)) // a goal done: the game's own rule
        return false;
    size_t tribes = 0;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            if (noun == kTribeNoun)
                ++*(size_t*)u;
            return true;
        },
        &tribes);
    if (tribes != 2) // (1: the first neighbour after 25 s; 3 / 4 / 5: the game's rule, the threshold is passed)
        return false;
    int threshold = 100;
    void* props = a_Props ? *(void**)a_Props : nullptr;
    if (a_PropInt && props)
        threshold = skg::cdecl_call<int>(a_PropInt, props, 0x4C87E711u, 100);
    int food = vcall<int>(pt, 0xA4 / 4);
    g_pace_hold = food <= threshold * (int)players;
    char b[128];
    snprintf(b, sizeof b, "food %d, threshold %d x %u players: %s", food, threshold, (unsigned)players,
             g_pace_hold ? "held" : "forms");
    g_pace_last = b;
    return g_pace_hold;
}

// BUG-1233: NpcTribeController_FormTribe 0xCC6440 takes the next tribe's species from the controller's list
// (+0xD0..+0xD4) at index min(size, tribes - 1) (0xCC6521: `cmp size, tribes - 1; jl` keeps the size), not
// min(size - 1, tribes - 1): with as many tribes as species or more, it reads the dword after the list's end and
// passes it + 0x504 to IBakeManager vt 0x1C (0x406EF0 reads [key + 8]). Its only guard before is the property list
// (TribeProps_ptr, key at +0xFC): tribes x 2 <= its count. The list is built per pair of that property list, only for
// the archetypes the species manager has a species for (0xCC6160), or from the saved keys (+0x7C, 0xCC7140), so it can
// hold fewer than count / 2. The game's update (0xCC6A90) calls FormTribe once a second with 3-4 tribes (5 after three
// goals): from then on every call read garbage. Seen in a 4-player campaign (final check 20261117-154911): 454 faults
// the game's handler caught (one a second from the fourth village's fall on), the frame cut off on three test
// commands, and the fourth crashed the host. `coop.fix tribe.form_species off`: the game's own read (0.7.14).
bool g_form_species_fix = true;
COOP_FIX("tribe.form_species", "BUG-1233", g_form_species_fix,
         "tribe: the NPC tribe controller forms no village once its list of species for new villages is used up (it "
         "read past the list and the game could crash)");
uint32_t g_forms_past;
std::string g_form_species_last;

bool form_species_state(void* ctrl, FormSpecies& s)
{
    s = FormSpecies{};
    static uintptr_t a_Props = H->sym("SporeCoop::TribeProps_ptr"), a_Arr = H->sym("App::Property::GetArrayUInt32"),
                     a_Mgr = H->sym("Simulator::cGameNounManager::Get"),
                     a_Data = H->sym("Simulator::cGameNounManager::GetData"),
                     a_Cb1 = H->sym("SporeCoop::MissionNouns_Cb1"), a_Cb2 = H->sym("SporeCoop::MissionNouns_Cb2"),
                     a_Cb3 = H->sym("SporeCoop::TribeNouns_Cb3"), a_Cb4 = H->sym("SporeCoop::TribeNouns_Cb4");
    if (!ctrl || !skg::readable((uint8_t*)ctrl + 0xD0, 0x30) || !a_Props || !a_Arr || !a_Mgr || !a_Data || !a_Cb1 ||
        !a_Cb2 || !a_Cb3 || !a_Cb4)
        return false;
    // the tribe nouns as FormTribe counts them (0xCC6478: GetData with the same callbacks, (+8 - +4) / 4)
    void* nm = skg::cdecl_call<void*>(a_Mgr);
    void* list = nm ? skg::thiscall<void*>(a_Data, nm, a_Cb1, a_Cb2, a_Cb3, a_Cb4, kTribeNoun) : nullptr;
    if (!skg::readable(list, 12))
        return false;
    s.tribes = (int)((F<uintptr_t>(list, 8) - F<uintptr_t>(list, 4)) / 4);
    void* props = *(void**)a_Props;
    int n = 0;
    void* data = nullptr;
    if (!props || !skg::cdecl_call<char>(a_Arr, props, F<uint32_t>(ctrl, 0xFC), &n, &data))
        return false;
    s.slots = n;
    void** lb = F<void**>(ctrl, 0xD0);
    void** le = F<void**>(ctrl, 0xD4);
    s.species = le >= lb ? (int)(le - lb) : -1;
    // FormTribe reads the list only past its property-list guard and with a non-empty list (0xCC650C)
    s.past = s.species > 0 && s.tribes * 2 <= s.slots && s.tribes - 1 >= s.species;
    char b[160];
    snprintf(b, sizeof b, "tribes %d, property list %d (x2 needed %d), species list %d: %s", s.tribes, s.slots,
             s.tribes * 2, s.species, s.past ? "the next species would be read past the list" : "in the list");
    g_form_species_last = b;
    return true;
}

static double g_species_checked = -10;
static bool g_species_hold;
static bool species_holds(void* ctrl, double now)
{
    if (!g_form_species_fix || !a_NpcCtrl)
        return false;
    // the update calls again every frame while it is held (its timer is reset only inside FormTribe): decided once a
    // second; a test command (coop.tribe form) decides now
    if (now - g_species_checked < 1.0 && now >= g_species_checked)
        return g_species_hold;
    g_species_checked = now;
    FormSpecies s;
    g_species_hold = form_species_state(ctrl, s) && s.past;
    // BUG-1238: a list shorter than the villages the property list allows, said once per list (triage)
    static int logged_species = -1, logged_slots = -1;
    if (s.species >= 0 && s.slots > 0 && s.species * 2 < s.slots &&
        (s.species != logged_species || s.slots != logged_slots)) {
        logged_species = s.species;
        logged_slots = s.slots;
        H->log(M, SK_INFO, "tribe: the NPC tribe controller has %d species for %d NPC villages (property list %d)",
               s.species, s.slots / 2, s.slots);
    }
    return g_species_hold;
}
void form_species_recheck() { g_species_checked = -10; }

// BUG-1392 (lab 20261121-003202 tribe_form_species): the decision above is kept for a second, and the game's own update
// calls FormTribe one second (of game time: the timer 0x157E940 FormTribe sets to 1.0, counted down by the frames' dt)
// after the last call. A call that formed a village kept its "in the list" answer, made with one tribe fewer: when the
// next call came 0.998 s of our clock later, it took that answer and the game read past the list once (the host's fault
// at 0x406EFF, FormTribe+0x119, the same signature as BUG-1233). Now a call that reached the game's FormTribe drops the
// answer: the next call decides with the tribes it has (a held call does not reach it: the update's every-frame calls
// still use the second's answer). `coop.fix tribe.form_species_fresh off`: the answer kept for its second (0.7.14).
bool g_form_species_fresh_fix = true;
COOP_FIX("tribe.form_species_fresh", "BUG-1392", g_form_species_fresh_fix,
         "tribe: the check that stops the NPC tribe controller reading past its list of species is made again after each "
         "village it forms (the next call less than a second later could still read past it and the game could crash)");
uint32_t g_forms_fresh; // tests: answers dropped after a call that reached FormTribe
static void form_species_formed()
{
    if (g_form_species_fresh_fix) {
        g_forms_fresh++;
        form_species_recheck();
    }
}

// BUG-1238: with the list used up (BUG-1233) no village formed any more, and on a planet whose list is shorter than its
// property list allows (the final check's campaign planet: 4 species, 5 NPC villages) the stage's fifth goal never
// came: the campaign stood at "no village left to raid (goal bar 4 of 5)". The index min(size, tribes - 1) is one past
// what the code obviously means (min(size - 1, tribes - 1): the last species for every tribe after the list; the game's
// own list repeats a species now and then: two archetypes picked the same one, lab 20261118-004440), so the
// call now sees the list as long as the tribes, its last species repeated: the next village takes the list's last species,
// with the archetype of its own pair of the property list, as many villages as the property list allows. Only for the
// call: the game's list (and the keys it saves, +0x7C) stays as the game built it. Room for the pointers: the list's
// capacity, else the game's own EASTL insert (0xB96750, as 0xCC7140 grows it). Not on a guest
// (tribe.guest_tribes: the host's villages come as copies). `coop.fix tribe.form_last_species off`: no village (BUG-1233).
bool g_last_species_fix = true;
COOP_FIX("tribe.form_last_species", "BUG-1238", g_last_species_fix,
         "tribe: once the list of species for new villages is used up, the next village takes its last species (the "
         "stage's last villages never came and the goal bar could not be filled)");
uint32_t g_forms_last;
static void* form_last_species(void* ctrl, int arg, int size, int tribes)
{
    static uintptr_t a_Insert = H->sym("SporeCoop::EASTL_VectorPtr_InsertValue");
    auto* vec = (uint8_t*)ctrl + 0xD0;
    void** lb = F<void**>(ctrl, 0xD0);
    if (size <= 0 || tribes <= size || tribes > 64 || !lb || F<void**>(ctrl, 0xD4) != lb + size || !skg::readable(lb, size * 4u))
        return nullptr;
    void* last = lb[size - 1];
    // the list as long as the tribes, its last species repeated: FormTribe's index tribes - 1 is then that last one
    for (int n = size; n < tribes; n++) {
        void** le = F<void**>(ctrl, 0xD4);
        if (le < F<void**>(ctrl, 0xD8)) {
            *le = last;
            F<void**>(ctrl, 0xD4) = le + 1;
        } else if (a_Insert) {
            skg::thiscall<void>(a_Insert, vec, (void*)le, (void*)&last); // the game's own grow (new storage, the old freed)
        }
        if (F<void**>(ctrl, 0xD4) != F<void**>(ctrl, 0xD0) + n + 1) { // (no room and no insert: as BUG-1233, nothing)
            F<void**>(ctrl, 0xD4) = F<void**>(ctrl, 0xD0) + size;
            return nullptr;
        }
    }
    g_forms_last++;
    g_forms_run++;
    void* r = o_Form(ctrl, arg);
    form_species_formed(); // BUG-1392
    // back to the game's size (FormTribe neither grows nor moves the list; the slots past it keep the repeated species)
    F<void**>(ctrl, 0xD4) = F<void**>(ctrl, 0xD0) + size;
    if (g_forms_last <= 5)
        H->log(M, SK_INFO, "tribe: the NPC tribe controller's list of species is used up (%s): the village took its last "
                           "species (tribe.form_last_species)", g_form_species_last.c_str());
    return r;
}

void* SK_THISCALL h_Form(void* self, int arg)
{
    if (g_guest_tribes_fix && is_guest() && game::mode_id() == modes::kTribe) {
        if (g_forms_blocked++ < 5)
            H->log(M, SK_INFO, "tribe: this game's NPC tribe controller would form a tribe: not on a guest (tribe.guest_tribes)");
        return nullptr;
    }
    if (species_holds(self, H->time_seconds())) {
        FormSpecies s;
        if (g_last_species_fix && form_species_state(self, s) && s.past && s.species > 0) {
            form_species_recheck(); // the next call decides again (this one forms: the update waits a second now)
            return form_last_species(self, arg, s.species, s.tribes);
        }
        if (g_forms_past++ % 600 == 0)
            H->log(M, SK_INFO, "tribe: the NPC tribe controller would read its next species past its list: no village "
                               "formed (%s) (tribe.form_species)", g_form_species_last.c_str());
        return nullptr;
    }
    if (pace_holds(H->time_seconds())) {
        if (g_forms_paced++ % 600 == 0)
            H->log(M, SK_INFO, "tribe: the NPC tribe controller would form the third tribe: %s (tribe.form_pace)", g_pace_last.c_str());
        return nullptr;
    }
    g_forms_run++;
    void* r = o_Form(self, arg);
    form_species_formed(); // BUG-1392: the tribes may have changed
    return r;
}
void unmapped_tick(const std::vector<void*>& locals, void* pt, double now)
{
    if (!g_guest_tribes_fix)
        return;
    std::unordered_map<void*, double> keep;
    for (void* t : locals) {
        if (t == pt || std::find(g_guest.local_tribe_of.begin(), g_guest.local_tribe_of.end(), t) != g_guest.local_tribe_of.end())
            continue;
        auto it = g_unmapped_since.find(t);
        double since = it == g_unmapped_since.end() ? now : it->second;
        keep[t] = since;
        if (now - since < 5.0)
            continue;
        auto ms = members(t);
        if (!ms.empty()) {
            net::Vec3 p = tribe_pos(t);
            H->log(M, SK_INFO, "tribe: a tribe of this game at (%.0f %.0f %.0f) the host has not: its %u members go (tribe.guest_tribes)",
                   p.x, p.y, p.z, (unsigned)ms.size());
        }
        for (void* c : ms) {
            gm_forget(c);
            remove_member(c);
            g_unmapped_removed++;
        }
    }
    g_unmapped_since.swap(keep);
}

// W2 (BUG-0564, realign.villages): a village of this guest's game the host's whole-planet list has not on two
// lists in a row (realign.cpp) goes with the game's own removal, cGameNounManager::DestroyInstance of the tribe
// (the Civ raid's last step, docs/re/civ.md#civ-tribe-raids: its hut, tools, food mat and bundles go with it).
// BUG-0483 emptied such a tribe of its members only: the tribe, its hut and its map mark stayed. Never the
// player tribe, never a tribe the member sync maps to a host tribe now, never while a cinematic
// plays (an actor of it); the members go first through RemoveMember as unmapped_tick does.
int remove_village(void* t)
{
    if (!t || !is_guest() || game::mode_id() != modes::kTribe || t == player_tribe())
        return 0;
    if (std::find(g_guest.local_tribe_of.begin(), g_guest.local_tribe_of.end(), t) != g_guest.local_tribe_of.end())
        return 0;
    if (game::cinematic_playing())
        return -1;
    for (auto it = g_npc_tribe_made.begin(); it != g_npc_tribe_made.end();) // made for a host tribe gone since
        it = it->second == t ? g_npc_tribe_made.erase(it) : std::next(it);
    for (void* c : members(t)) {
        gm_forget(c);
        remove_member(c);
        g_unmapped_removed++;
    }
    g_unmapped_since.erase(t);
    g_ours_t.erase(t);
    wsync::village_gone(t); // BUG-0942: the game's goal card of this village
    game::destroy_creature(t); // (not an animal: DestroyInstance at once)
    return 1;
}

// BUG-0777: a hut the host's game saw fall falls in this guest's game too, through the game's own blow (the hut's
// combatant TakeDamage vt 0x18, its remaining health and a little more, the attacker a member of the player tribe):
// the game's own defeat follows (the death message, the village's conquered flag +0x556, the goal mission's end).
// 1 dealt, 0 nothing to do, -1 not now (a cinematic plays).
static int fell_hut_by(void* hut, int owner);
int fell_hut(void* hut)
{
    if (!is_guest())
        return 0;
    return fell_hut_by(hut, -1);
}
static int fell_hut_by(void* hut, int owner) // owner >= 0: the attacker a member of that player if there is one
{
    if (!hut || game::mode_id() != modes::kTribe || !skg::readable(hut, 0x160))
        return 0;
    if (F<int>(hut, 0x120 + 0x34) == 2)
        return 0;
    if (game::cinematic_playing())
        return -1;
    void* pt = player_tribe();
    void* att = nullptr;
    if (pt)
        for (void* c : members(pt))
            if (is_citizen(c) && !game::is_dead(c) && (!att || (owner >= 0 && owner_of(c) == owner))) {
                att = c;
                if (owner < 0 || owner_of(c) == owner)
                    break;
            }
    static const float zero[3] = {0, 0, 0}; // the hit direction must not be null
    float hp = F<float>(hut, 0x120 + 0x38);
    vcall<int>((uint8_t*)hut + 0x120, 0x18 / 4, hp + 10.f, 0xFFFFFFFFu, 0, zero, att ? (uint8_t*)att + 0x5A8 : nullptr);
    H->log(M, SK_INFO, "tribe: a hut falls here (health %.1f, attacker %p of player %d): state %d (BUG-0777 / BUG-0879)", hp, att,
           att ? (int)owner_of(att) : -1, F<int>(hut, 0x120 + 0x34));
    return 1;
}

// BUG-0777: a village the host's game conquered: its hut falls here (fell_hut); a village without a standing hut
// gets the game's own conquered flag (cTribe_SetConquered: +0x556, its view effect, its food bundles go), which the
// goal mission's update (0xC2F750) reads to end the mission. 1 done, 0 nothing to do, -1 not now.
int conquer_village(void* t)
{
    if (!t || !is_guest() || game::mode_id() != modes::kTribe || t == player_tribe() || !skg::readable(t, 0x557))
        return 0;
    if (F<uint8_t>(t, 0x556))
        return 0;
    void* hut = skg::readable(t, 0x36C) ? F<void*>(t, 0x368) : nullptr;
    if (hut && skg::readable(hut, 0x160) && F<int>(hut, 0x120 + 0x34) != 2)
        return fell_hut(hut);
    static uintptr_t a_SetConquered = H->sym("SporeCoop::cTribe_SetConquered");
    if (!a_SetConquered)
        return 0;
    if (game::cinematic_playing())
        return -1;
    thiscall<void>(a_SetConquered, t, 1);
    H->log(M, SK_INFO, "tribe: the village %p the host's game conquered is conquered here (hut %p) (BUG-0777)", t, hut);
    return 1;
}

// BUG-0879: guest, the conquered flag of every NPC village as last seen; a village this game conquers while the host
// is away goes to the host
static std::unordered_map<void*, uint8_t> g_conq_seen;
static void away_conquest_tick(double now)
{
    static double next;
    if (now < next || !is_guest())
        return;
    next = now + 0.5;
    void* pt = player_tribe();
    std::unordered_map<void*, uint8_t> seen;
    for (void* t : all_tribes()) {
        if (t == pt || !skg::readable(t, 0x557))
            continue;
        uint8_t f = F<uint8_t>(t, 0x556);
        auto it = g_conq_seen.find(t);
        if (f && it != g_conq_seen.end() && !it->second && g_away_conquest_fix && local_play()) {
            net::Vec3 p = tribe_pos(t);
            net::Writer w;
            w.u8(CMD_CONQUER);
            w.u8(0);
            w.u32(0);
            write_target(w, Target{});
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            C.sess->send_reliable_to(0, net::MSG_TRIBE_CMD, w);
            g_conquests_sent++;
            H->log(M, SK_INFO, "tribe: this game conquered the village at (%.0f %.0f %.0f) while the host is away: sent to the host (BUG-0879)",
                   p.x, p.y, p.z);
            if (C.feed)
                C.feed("your conquest waits: " + C.sess->name_of(0) + " (host) is away, it is made when the host is back");
        }
        seen[t] = f;
    }
    g_conq_seen.swap(seen);
}

// BUG-0879: host, a guest's game conquered the NPC village at p while this game was away: its hut falls here
static void* village_near(const net::Vec3& p)
{
    void* pt = player_tribe();
    void* best = nullptr;
    float bd = 40.f;
    for (void* t : all_tribes()) {
        float d = net::dist(tribe_pos(t), p);
        if (t != pt && d < bd && skg::readable(t, 0x557)) {
            bd = d;
            best = t;
        }
    }
    return best;
}
// 1 conquered here, 0 refused, -1 not now (a cinematic plays) and `last` is false: not counted, tried again
static int conquer_now(uint8_t from, const net::Vec3& p, bool last)
{
    void* best = village_near(p);
    void* hut = best && skg::readable(best, 0x36C) ? F<void*>(best, 0x368) : nullptr;
    int r = best && !F<uint8_t>(best, 0x556) ? fell_hut_by(hut, from) : 0;
    if (r < 0 && !last)
        return -1;
    if (r > 0) {
        g_conquests_done++;
        H->log(M, SK_INFO, "tribe: player %u conquered the village at (%.0f %.0f %.0f) while this game was away: conquered here (BUG-0879)",
               (unsigned)from, p.x, p.y, p.z);
    } else {
        g_conquests_refused++;
        H->log(M, SK_INFO, "tribe: player %u's conquest of the village at (%.0f %.0f %.0f) not made here: %s (BUG-0879)", (unsigned)from,
               p.x, p.y, p.z, !best ? "no village there" : F<uint8_t>(best, 0x556) ? "already conquered" : r < 0 ? "a cinematic plays" : "no standing hut");
    }
    return r;
}
// BUG-1492 (audit TR-5): the village a guest conquered while the host was away fell here without counting for the stage's
// goal: the goal bar counts a village when its cTribeToTribeMission ends, and only the host's own input or a guest's raid
// order (BUG-1068, tribe.guest_mission) makes that mission; the away raid ran in the guest's own game. Now the conquest
// first makes the village's mission (wsync::goal_target, SetTargetTribe outside the frame, as BUG-1068 does at the order)
// and the hut falls 0.5 s later, once the mission is there (its update ends it from the conquered flag: lab
// 20261113-072313-2). The audit's part 2 on the way: a fall refused for a playing cinematic is tried again every second
// for 30 s instead of being dropped (the held commands run all at once). `coop.fix tribe.away_goal off`: at once, no
// mission, a cinematic drops it (0.7.16.1).
static bool g_away_goal_fix = true;
COOP_FIX("tribe.away_goal", "BUG-1492", g_away_goal_fix,
         "tribe: a village a guest conquered while the host was away counts for the stage's goal when the host is back");
struct AwayConquest {
    uint8_t from;
    net::Vec3 p;
    double at, until;
};
static std::vector<AwayConquest> g_away_pending;
static uint32_t g_away_goals, g_away_retries;
void conquer_on_host(uint8_t from, const net::Vec3& p)
{
    if (!g_away_goal_fix) {
        conquer_now(from, p, true);
        return;
    }
    void* best = village_near(p);
    if (best && !F<uint8_t>(best, 0x556) && wsync::goal_target(best)) {
        g_away_goals++;
        H->log(M, SK_INFO, "tribe: the village at (%.0f %.0f %.0f) player %u conquered while this game was away gets its goal mission first (BUG-1492)",
               p.x, p.y, p.z, (unsigned)from);
    }
    double now = H->time_seconds();
    g_away_pending.push_back({from, p, now + 0.5, now + 30.0});
}
// host, every tick in Tribe: the conquests whose mission had its time
static void away_goal_tick(double now)
{
    for (size_t i = 0; i < g_away_pending.size();) {
        AwayConquest& a = g_away_pending[i];
        if (now < a.at) {
            i++;
            continue;
        }
        if (conquer_now(a.from, a.p, now >= a.until) < 0) {
            g_away_retries++;
            a.at = now + 1.0;
            i++;
            continue;
        }
        g_away_pending.erase(g_away_pending.begin() + i);
    }
}

// ---------------- co-op mechanics (docs/re/tribe.md, co-op features) ----------------
// pincer: members of two players hitting the same tribe within 10 s deal 50% more damage
// chief: every player has a chief (its lowest-id adult member, bigger, "★" tag); members near their
//        chief hit 20% harder; a chief about to die next to a partner's member keeps 30% health
// duet: performances of both players for the same tribe fill the social mini-game faster
// healers: a full heal of a partner's member brings 5 food
// epic: now and then an epic creature comes to the village; killed by both players: 80 food
constexpr double kPincerWindow = 10.0;
static uintptr_t a_CitizenTD, a_HealTick, a_PatientOf, a_AddFood, a_FoodFx, a_SessionGet, a_CreateHerd,
    a_HerdEnable, a_CreatureCreate;
static std::unordered_map<void*, std::unordered_map<uint8_t, double>> g_pincer; // enemy tribe -> owner -> time
static std::unordered_map<void*, double> g_pincer_told;
std::unordered_map<uint8_t, void*> g_chief;                              // owner -> chief citizen
std::unordered_map<void*, float> g_chief_scale;                          // chief -> its own scale
// BUG-0422: the departed player's chief (a pointer into this game) and its pincer hits stayed under its id
PLAYER_STATE("tribe", [](uint8_t id) { g_chief.erase(id); for (auto& [t, m] : g_pincer) m.erase(id); },
             [](uint8_t id) -> size_t {
                 size_t n = g_chief.count(id);
                 for (auto& [t, m] : g_pincer)
                     n += m.count(id);
                 return n;
             });
static std::unordered_map<void*, double> g_chief_saved;                         // chief -> last rescue
// BUG-1052: the chief aura's rescue left the saved chief 30 % of +0x5D4 (combatant +0x2C, 1.0: BUG-0745), i.e. 0.3 hit
// points, so it fell at the next blow. Now 30 % of cCombatant::GetMaxHitPoints (game::max_health).
// `coop.fix tribe.rescue_max off`: the field +0x5D4 (0.7.13)
static bool g_fix_rescue_max = true;
COOP_FIX("tribe.rescue_max", "BUG-1052", g_fix_rescue_max, "tribe: a chief saved by a partner's member keeps 30 % of its real maximum health");
uint32_t g_pincer_hits, g_aura_hits, g_chief_rescues, g_duet_ticks, g_heals, g_epics, g_epic_kills;
void* g_epic;
static uint32_t g_epic_serial; // BUG-0951
void* g_epic_herd; // the herd g_epic lives in (ours: quieted when the epic is gone)
static std::unordered_set<uint8_t> g_epic_hitters;
// BUG-0146: only the players' members count as hitters (an NPC tribe's member is owner 0xFE).
// BUG-0121: the epic's herd is reused and quieted, a live epic never goes into a save.
// `coop.tribe fix epic_players|epic_herd 0`: the old behaviour (tests).
bool g_fix_epic_players = true, g_fix_epic_herd = true;
COOP_FIX("tribe.epic_players", "BUG-0146", g_fix_epic_players, "tribe: only players' members count as epic hitters");
COOP_FIX("tribe.epic_herd", "BUG-0121", g_fix_epic_herd, "tribe: the epic's herd is reused and quieted, a live epic is not saved");
static uint32_t g_epic_herd_reused, g_epic_saved_out;
static double g_epic_next, g_last_duet, g_duet_told;

uint16_t member_id(void* c)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        HEntry* it = hm_of(c);
        return it ? it->id : 0;
    }
    GEntry* it = gm_of(c);
    return it ? it->id : 0;
}

uint16_t id_of(void* c) { return member_id(c); }
void* member_of(uint16_t id) // host (tests): the member of this id, listed now
{
    void* c = hm_obj(id);
    return c && game::listed(c) ? c : nullptr;
}
void* mirror_of(uint16_t id) { return member_mirror(id); } // (BUG-0955)

// tests (game bots): a readable key for tribe buildings on screen: hut<tribe index>, "p" for the player's
std::string object_key(void* obj)
{
    if (!g_ok || !obj)
        return "";
    void* pt = player_tribe();
    auto tribes = all_tribes();
    for (size_t i = 0; i < tribes.size(); i++)
        if (F<void*>(tribes[i], 0x368) == obj) // cTribe::mpHut
            return "hut" + std::to_string(i) + (tribes[i] == pt ? "p" : "");
    return "";
}

// the tribe UI's interaction mode (TribeUI+0x390: 0 social, 1 attack; docs/re/tribe.md B), -1 unknown
static int ui_mode()
{
    uintptr_t get = H->sym("SporeCoop::cTribeUI_Get");
    void* ui = get ? skg::cdecl_call<void*>(get) : nullptr;
    return ui && !IsBadReadPtr((uint8_t*)ui + 0x390, 4) ? F<uint8_t>(ui, 0x390) : -1;
}

bool is_chief(void* c)
{
    for (auto& [o, ch] : g_chief)
        if (ch == c)
            return true;
    return false;
}

// health as the host has it: guests keep the last snapshot value (a mirror is never set to 0, so a
// knocked-out member would still look alive there and the machines would pick different chiefs)
static float host_health(void* c)
{
    if (C.sess && C.sess->role() == net::Role::Host)
        return game::health(c);
    GEntry* g = gm_of(c);
    return g ? g->data.hp : game::health(c);
}

// A former chief gets its own size back (spatial vt 0x40 SetScale). BUG-0081: it is called only
// for a citizen that is still a member of the player tribe in the game's own list; before, the only
// check was object_alive, which reads the citizen's own memory and cannot tell a freed one (a guest
// mirror removed by guest_state, a load): the call went through a vtable read from freed memory.
// keep: the chiefs that stay (nullptr: none, the session ends). `coop.tribe fix chiefs 0`: the old check.
bool g_fix_chiefs = true;
COOP_FIX_UNSAFE("tribe.chiefs", "BUG-0081", g_fix_chiefs, "tribe: chiefs checked against the player tribe's own list, not freed memory");
static uint32_t g_chief_restores, g_chief_gone, g_chief_bad_calls;
static void restore_chief_scales(const std::unordered_map<uint8_t, void*>* keep)
{
    std::unordered_set<void*> mem;
    if (void* pt = g_ok && game::mode_id() == modes::kTribe ? player_tribe() : nullptr) {
        void** b = F<void**>(pt, kMembersOff);
        void** e = F<void**>(pt, kMembersOff + 4);
        for (void** it = b; it && it < e && mem.size() < 256; ++it)
            if (*it)
                mem.insert(*it);
    }
    for (auto it = g_chief_scale.begin(); it != g_chief_scale.end();) {
        bool still = false;
        if (keep)
            for (auto& [o, c] : *keep)
                if (c == it->first)
                    still = true;
        if (still) {
            ++it;
            continue;
        }
        bool member = mem.count(it->first) != 0;
        if (g_fix_chiefs ? member : game::object_alive(it->first)) {
            if (!member)
                g_chief_bad_calls++; // the old check let a call into a non-member through (test)
            vcall<void>((uint8_t*)it->first + 0xC0, 0x40 / 4, it->second);
            g_chief_restores++;
        } else
            g_chief_gone++; // gone from the tribe: nothing to restore
        it = g_chief_scale.erase(it);
    }
}

// every machine picks the same chiefs (ids, owners and health come from the host)
static void chiefs_tick()
{
    void* pt = player_tribe();
    std::unordered_map<uint8_t, void*> want;
    std::unordered_map<uint8_t, uint16_t> best;
    if (pt)
        for (void* c : members(pt)) {
            uint8_t o = owner_of(c);
            uint16_t id = member_id(c);
            // a knocked-out member (the game's chieftain waits for its respawn with 0 health) leads nobody
            if (o >= 0xFE || !id || F<uint8_t>(c, kAgeOff) != 1 || !game::object_alive(c) || host_health(c) <= 0)
                continue;
            if (!best.count(o) || id < best[o]) {
                best[o] = id;
                want[o] = c;
            }
        }
    // scale: chiefs stand out (spatial vt 0x40 SetScale; citizen vt 0x80 its desired scale)
    restore_chief_scales(&want);
    for (auto& [o, c] : want)
        if (!g_chief_scale.count(c)) {
            float sc = vcall<float>(c, 0x80 / 4, 0); // GetDesiredScale(int), ret 4
            if (sc <= 0.01f || sc > 10.f)
                sc = 1.f;
            g_chief_scale[c] = sc;
            vcall<void>((uint8_t*)c + 0xC0, 0x40 / 4, sc * 1.3f);
        }
    g_chief = want;
}

static bool near_own_chief(void* c, uint8_t owner)
{
    auto it = g_chief.find(owner);
    if (it == g_chief.end() || !it->second || it->second == c)
        return false;
    // BUG-1053: g_chief is chiefs_tick's table; a hit in the game's update before the next tick may come after the
    // chief left (a fall, drop_chief): its position is read only while the game's list has it (walked here: a hook)
    if (g_cmd_listed_fix && !game::listed(it->second, 0x18EB4B7u)) {
        g_cmd_gone++;
        return false;
    }
    return net::dist(game::position(it->second), game::position(c)) < 15.f;
}

// cCreatureCitizen TakeDamage 0xC26920 (this = citizen+0x5A8): every hit on a citizen
using CTD_t = int(SK_THISCALL*)(void*, float, uint32_t, int, const void*, void*);
static CTD_t o_CitizenTD;
std::unordered_map<void*, EpicBlows> g_epic_blows; // BUG-0515 (diagnostics): an epic -> its blows on members
static bool is_epic_animal(void* a)
{
    static uintptr_t is_epic = H->sym("SporeCoop::cCreatureAnimal_IsEpic");
    return a && is_epic && !IsBadReadPtr(a, 8) && game::noun_id(a) == 0x18EB45E && thiscall<bool>(is_epic, a);
}
// BUG-0685 (docs/re/tribe.md#tribe-guest-game-over): a guest's mirrors have the host's members' health (the snapshot),
// the host's game decides who falls (tribe.ko plays it). A blow of the guest's own game that took a mirror to 0 health
// made that game's own decisions on it: a mirror that was the last member with health of the guest's player tribe (the
// host's tribe down to its chief) brought up the guest's own game over at once (the input lock, 12 s later the caption
// "Alas! Your population has fallen below the minimum ...", its OK reloads the guest's own last save) while the host
// played on; with the input lock up tribe.gameover's guard (BUG-0172/0288) then refused the mirror the host's member
// needed again (campaign 20261107-141346, Carol: `t: 1 missing on the guest (t4)` for good). tribe.mirror_blow: a
// blow of a guest's game never takes a mirror below kMirrorBlowFloor (it falls when the host's member does), also
// not while the host is away (local_play: the guest's game runs on its own then). Off: the game's blow (0.7.10).
static bool g_mirror_blow_fix = true;
COOP_FIX("tribe.mirror_blow", "BUG-0685", g_mirror_blow_fix,
         "tribe: a blow of a guest's own game never kills a mirror of the host's member (no game over of its own there)");
constexpr float kMirrorBlowFloor = 0.3f; // (the game's own floor of a chief that still has its tribe)
static uint32_t g_mirror_blows_kept;
static int SK_THISCALL h_CitizenTD(void* comb, float dmg, uint32_t pol, int type, const void* pos, void* att)
{
    if (civ::hit_is_cosmetic(att)) // BUG-0477: a Civ guest's copy of a host's shot hurts nobody
        return 0;
    if (g_mirror_blow_fix && is_guest() && active() && !local_play()) { // BUG-0685
        void* victim = (uint8_t*)comb - 0x5A8;
        GEntry* it = gm_of(victim);
        float hp = it ? F<float>(victim, 0x5E0) : 0.f;
        if (it && hp > 0 && dmg > hp - kMirrorBlowFloor) {
            if (g_mirror_blows_kept++ < 5)
                H->log(M, SK_INFO, "tribe: a blow of this game on mirror #%u (%.1f of its %.1f health) leaves it %.1f: the host's "
                       "game decides when it falls (tribe.mirror_blow, BUG-0685)", it->id, dmg, hp, std::min(hp, kMirrorBlowFloor));
            dmg = std::max(0.f, hp - kMirrorBlowFloor);
        }
    }
    if (is_host() && active()) {
        void* victim = (uint8_t*)comb - 0x5A8;
        void* pt = player_tribe();
        void* vt = F<void*>(victim, kOwnerTribeOff);
        void* ac = att && !IsBadReadPtr(att, 4) ? vcall<void*>(att, 0xC / 4) : nullptr;
        HEntry* ah = hm_of(ac);
        if (ac && !ah && is_epic_animal(ac)) {
            auto& b = g_epic_blows[ac];
            b.n++;
            b.last = dmg;
            b.max = std::max(b.max, dmg);
        }
        uint8_t ao = ah ? ah->data.owner : 0xFF;
        double now = H->time_seconds();
        // WP-D1 (BUG-1473): all.party_difficulty: a hit between the players' tribe and an NPC (another tribe's member, an
        // animal, an epic). The attacker is ours when it is a member of the player tribe (members are citizens: ah)
        {
            using party::Side;
            Side vs = vt == pt && pt ? Side::Player : vt ? Side::Npc : Side::Other;
            Side as = !ac ? Side::Other : ah && pt && F<void*>(ac, kOwnerTribeOff) == pt ? Side::Player : Side::Npc;
            dmg = party::hit(dmg, vs, as);
        }
        if (ao < 0xFE && vt && vt != pt) { // our member hits another tribe
            auto& hits = g_pincer[vt];
            hits[ao] = now;
            int players = 0;
            for (auto& [o, t] : hits)
                if (now - t < kPincerWindow)
                    players++;
            if (players >= 2 && settings::on(settings::kTribePincer)) { // F3 (BUG-0768): tribe.pincer
                dmg *= 1.5f;
                g_pincer_hits++;
                if (now - g_pincer_told[vt] > 60 && C.notice) {
                    g_pincer_told[vt] = now;
                    C.notice("pincer attack: +50% damage");
                }
            }
            if (settings::on(settings::kTribeAura) && near_own_chief(ac, ao)) { // tribe.chief_aura
                dmg *= 1.2f;
                g_aura_hits++;
            }
        }
        // a chief about to fall next to a partner's member: the tribe protects it
        if (vt == pt && is_chief(victim) && settings::on(settings::kTribeAura)) {
            // BUG-1052 (tribe.rescue_max): 30 % of its real maximum (game::max_health), as the heal hook (BUG-0745)
            float hp = F<float>(victim, 0x5E0), mx = g_fix_rescue_max ? game::max_health(victim) : F<float>(victim, 0x5D4);
            uint8_t o = owner_of(victim);
            if (dmg >= hp && mx > 0 && now - g_chief_saved[victim] > 60) {
                for (void* m : members(pt)) {
                    uint8_t mo = owner_of(m);
                    if (mo < 0xFE && mo != o && net::dist(game::position(m), game::position(victim)) < 15.f) {
                        dmg = std::max(0.f, hp - mx * 0.3f);
                        g_chief_saved[victim] = now;
                        g_chief_rescues++;
                        if (C.notice)
                            C.notice(C.sess->name_of(mo) + " saved " + C.sess->name_of(o) + "'s chief");
                        break;
                    }
                }
            }
        }
    }
    return o_CitizenTD(comb, dmg, pol, type, pos, att);
}

static void bonus_food(int n)
{
    void* pt = player_tribe();
    if (!pt || !a_AddFood)
        return;
    thiscall<void>(a_AddFood, pt, n);
    if (a_FoodFx)
        game::tribe_food_visuals(pt, (float)n); // BUG-0585: not without the tribe's food mat
}

// heal tick 0xDB9B20 (cdecl: arg1 healer, *arg6 patient): a full heal of a partner's member
// BUG-0745: the patient's maximum is cCombatant::GetMaxHitPoints (game::max_health); the field +0x5D4
// (combatant +0x2C) holds 1.0, so the bonus never fired. `coop.fix tribe.heal_max off`: that field (tests)
static bool g_fix_heal_max = true;
COOP_FIX("tribe.heal_max", "BUG-0745", g_fix_heal_max, "tribe: the healer bonus compares the patient's health with its real maximum");
using HealTick_t = bool(SK_CDECL*)(void*, void*, void*, void*, void*, void**, void*, float, void*, void*, void*, void*);
static HealTick_t o_HealTick;
static bool SK_CDECL h_HealTick(void* healer, void* a2, void* a3, void* a4, void* a5, void** target, void* a7, float a8,
                                void* a9, void* a10, void* a11, void* a12)
{
    void* pat = nullptr;
    float hp0 = 0, mx = 0;
    if (is_host() && active() && target && a_PatientOf) {
        pat = skg::cdecl_call<void*>(a_PatientOf, *target);
        if (pat) {
            hp0 = F<float>(pat, 0x5E0);
            mx = g_fix_heal_max ? game::max_health(pat) : F<float>(pat, 0x5D4);
        }
    }
    bool r = o_HealTick(healer, a2, a3, a4, a5, target, a7, a8, a9, a10, a11, a12);
    if (pat && mx > 0 && hp0 < mx && F<float>(pat, 0x5E0) >= mx) {
        uint8_t ho = owner_of(healer), po = owner_of(pat);
        if (ho < 0xFE && po < 0xFE && ho != po) {
            bonus_food(5);
            if (g_heals++ % 4 == 0 && C.notice)
                C.notice(C.sess->name_of(ho) + " healed " + C.sess->name_of(po) + "'s tribe member: +5 food");
        }
    }
    return r;
}

// duet: performances (BT node 0xB46E8803) of both players for the same tribe
// BUG-0748 (the duet's test): `coop.fix tribe.duet off` leaves the performances to the game alone (the test's twin)
bool g_duet_fix = true;
COOP_FIX("tribe.duet", "BUG-0748", g_duet_fix, "tribe: performances of two players' members for the same village fill its social mini-game faster (the duet)");
std::vector<Performer> performers()
{
    std::vector<Performer> v;
    void* pt = player_tribe();
    if (!pt || !a_SessionGet)
        return v;
    for (void* c : members(pt)) {
        uint8_t o = owner_of(c);
        uint8_t* ai = o < 0xFE ? F<uint8_t*>(c, 0xB4C) : nullptr;
        if (!ai || IsBadReadPtr(ai, 0x604) || F<uint32_t>(ai, 0x1D8) != 0xB46E8803u)
            continue;
        uint8_t* blk = F<uint8_t*>(ai, 0x600);
        if (!blk || IsBadReadPtr(blk, 0x1AC))
            continue;
        uint8_t* ss = skg::cdecl_call<uint8_t*>(a_SessionGet, F<int>(blk, 0x1A8));
        if (!ss || IsBadReadPtr(ss, 0xF0))
            continue;
        HEntry* h = hm_of(c);
        v.push_back({h ? h->id : (uint16_t)0, o, F<void*>(ss, 0xC), F<void*>(ss, 0xE8), F<int>(ss, 4)});
    }
    return v;
}
static void duet_tick(double now)
{
    if (!g_duet_fix || now - g_last_duet < 0.5 || !a_SessionGet || !settings::on(settings::kTribeDuet)) // F3: tribe.duet
        return;
    g_last_duet = now;
    void* pt = player_tribe();
    if (!pt)
        return;
    std::unordered_map<void*, std::unordered_set<uint8_t>> perf; // target tribe -> owners
    std::unordered_map<void*, void*> ui_of;
    for (void* c : members(pt)) {
        uint8_t o = owner_of(c);
        uint8_t* ai = o < 0xFE ? F<uint8_t*>(c, 0xB4C) : nullptr;
        if (!ai || IsBadReadPtr(ai, 0x604) || F<uint32_t>(ai, 0x1D8) != 0xB46E8803u)
            continue;
        uint8_t* blk = F<uint8_t*>(ai, 0x600);
        if (!blk || IsBadReadPtr(blk, 0x1AC))
            continue;
        uint8_t* ss = skg::cdecl_call<uint8_t*>(a_SessionGet, F<int>(blk, 0x1A8));
        if (!ss || IsBadReadPtr(ss, 0xF0))
            continue;
        void* target = F<void*>(ss, 0xC);
        if (!target)
            continue;
        perf[target].insert(o);
        ui_of[target] = F<void*>(ss, 0xE8);
    }
    for (auto& [target, owners] : perf) {
        if (owners.size() < 2)
            continue;
        void* ui = ui_of[target];
        if (ui && !IsBadReadPtr(ui, 0x6C)) {
            float& p = F<float>(ui, 0x68);
            if (p < 0.97f)
                p = std::min(0.97f, p + 0.04f); // +8% a second on top of the performance
            g_duet_ticks++;
        }
        if (now - g_duet_told > 60 && C.notice) {
            g_duet_told = now;
            C.notice("duet: friendship grows faster");
        }
    }
}

// a herd of ours for epics: the epic archetype, no nest, not the avatar's, target size 0 (the
// game's own herds have a target size). prof: of this species (null: any). Checked in the object list.
bool is_epic_herd_of_ours(void* o)
{
    return game::object_alive(o) && skg::readable(o, 0x21C) && F<uint32_t>(o, 0x88) == kEpicArchetype &&
           !game::herd_nest(o) && !F<uint8_t>(o, 0x84) && F<int32_t>(o, 0xF4) == 0;
}

static void* epic_herd_find(void* prof)
{
    struct Q {
        void* prof;
        void* found = nullptr;
    } q{prof};
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* q = (Q*)u;
            if (noun != game::kHerdNoun || !is_epic_herd_of_ours(o) || (q->prof && game::herd_species(o) != q->prof))
                return true;
            q->found = o;
            return false;
        },
        &q);
    return q.found;
}

static bool herd_listed(void* h)
{
    struct Q {
        void* want;
        bool found = false;
    } q{h};
    if (!h)
        return false;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* q = (Q*)u;
            if (o == q->want && noun == game::kHerdNoun)
                q->found = true;
            return !q->found;
        },
        &q);
    return q.found;
}

// the epic is gone (killed, removed before a save, the session ended): its herd spawns nothing.
// Target size 0 holds; the game enables the herd again after its member died (lane 7: disabled at
// the kill, enabled again within seconds; a disable later stayed), so `enabled` is best effort.
static void epic_herd_release()
{
    if (g_fix_epic_herd && g_epic_herd && herd_listed(g_epic_herd))
        game::herd_quiet(g_epic_herd);
    g_epic_herd = nullptr;
}

// epic: a giant of a species on the planet comes to the village (host)
// BUG-0515 (players: "giants only deal 8 damage"): the co-op epic's herd was made by CreateHerd with the
// epic archetype and nothing else: its mDamageMultiplier (+0x158) stayed 1.0, while the game's own epic herd
// of the planet has 15.0 (lab, the 'tribe' fixture's epic, archetype 0xEC4C89F8): the co-op epic hit members
// for 1.5-8 (a normal animal's blows). The herd gets the game's epic herd's multiplier (15 when the planet has
// no epic of its own).
// BUG-0563: the co-op epic took the species of the animal nearest the village: after the live Creature -> Tribe
// change that was the guest's own creature (its puppet of the Creature stage, species of the guest's creation, near
// the host's village where the guest stood): the host fought the guest's creature as an epic monster. The epic's
// species is never a puppet's, a proxy's or a player's creature.
uint32_t g_epic_skipped;
constexpr float kEpicDamageMult = 15.f;
static float game_epic_damage_mult()
{
    struct U {
        float m = 0;
    } u;
    game::for_each_object(
        [](void* o, uint32_t noun, void* up) {
            auto* u = (U*)up;
            if (noun != 0x18EB45E || o == g_epic || !game::object_alive(o) || !is_epic_animal(o))
                return true;
            void* h = game::herd_of(o);
            if (h && skg::readable(h, 0x21C) && !is_epic_herd_of_ours(h) && F<float>(h, 0x158) > 1.f && F<float>(h, 0x158) < 100.f) {
                u->m = F<float>(h, 0x158);
                return false;
            }
            return true;
        },
        &u);
    return u.m > 1.f ? u.m : kEpicDamageMult;
}

bool spawn_epic()
{
    void* pt = player_tribe();
    if (!pt || !a_CreateHerd || !a_HerdEnable || !a_CreatureCreate || g_epic)
        return false;
    net::Vec3 tp = tribe_pos(pt);
    // a species already on the planet (the guests have its model): the nearest animal
    void* prof = nullptr;
    struct Pick {
        net::Vec3 tp;
        void* prof = nullptr;
        float bd = 1e9f;
        std::vector<void*> players; // BUG-0563: the players' own species (their creatures of the Creature stage)
        uint32_t skipped = 0;
    } pick{tp};
    for (auto& pl : C.sess->players())
        if (void* sp = creations::player_species(pl.id))
            pick.players.push_back(sp);
    game::for_each_object(
        [](void* a, uint32_t noun, void* u) {
            auto* pk = (Pick*)u;
            if (noun != 0x18EB45E || !game::object_alive(a))
                return true;
            if (game::is_puppet(a) || npc::is_proxy(a) ||
                std::find(pk->players.begin(), pk->players.end(), game::species_profile(a)) != pk->players.end()) {
                pk->skipped++; // a puppet / proxy of ours, or a player's species: never the epic (BUG-0563)
                return true;
            }
            float d = net::dist(game::position(a), pk->tp);
            if (d < pk->bd && game::species_profile(a)) {
                pk->bd = d;
                pk->prof = game::species_profile(a);
            }
            return true;
        },
        &pick);
    prof = pick.prof;
    if (!prof)
        return false;
    g_epic_skipped += pick.skipped;
    float len = std::sqrt(tp.x * tp.x + tp.y * tp.y + tp.z * tp.z);
    // 60 m from the village, along a tangent of the planet
    net::Vec3 up = len > 1 ? net::Vec3{tp.x / len, tp.y / len, tp.z / len} : net::Vec3{0, 0, 1};
    net::Vec3 side = std::fabs(up.z) < 0.9f ? net::Vec3{up.y, -up.x, 0} : net::Vec3{0, up.z, -up.y};
    float sl = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
    net::Vec3 p{tp.x + side.x / sl * 60.f, tp.y + side.y / sl * 60.f, tp.z + side.z / sl * 60.f};
    float pp[3] = {p.x, p.y, p.z};
    void* nmgr = game::noun_manager();
    // BUG-0121: one quiet epic herd of the species is reused (also one a save brought back), not a
    // new herd per epic that stayed in the world and in every save
    void* herd = g_fix_epic_herd ? epic_herd_find(prof) : nullptr;
    if (herd)
        g_epic_herd_reused++;
    else
        herd = nmgr ? thiscall<void*>(a_CreateHerd, nmgr, pp, prof, 0, false, 0, false) : nullptr;
    if (!herd)
        return false;
    F<uint32_t>(herd, 0x88) = kEpicArchetype; // an archetype with isEpic = 1
    F<uint32_t>(herd, 0xF0) = 0;
    F<float>(herd, 0x158) = game_epic_damage_mult(); // mDamageMultiplier, BUG-0515
    if (g_fix_epic_herd)
        F<float>(herd, 0x34) = pp[0], F<float>(herd, 0x38) = pp[1], F<float>(herd, 0x3C) = pp[2]; // its home
    thiscall<void>(a_HerdEnable, herd, true);
    void* c = skg::cdecl_call<void*>(a_CreatureCreate, pp, prof, 1, herd, false, false);
    if (g_fix_epic_herd)
        F<int32_t>(herd, 0xF4) = 0; // mTargetHerdSize: the game adds no more epics to it
    if (!c) {
        if (g_fix_epic_herd)
            game::herd_quiet(herd);
        return false;
    }
    g_epic = c;
    g_epic_serial = game::object_serial(c); // BUG-0951
    g_epic_herd = herd;
    g_epic_hitters.clear();
    g_epics++;
    if (C.notice)
        C.notice("an epic creature comes! together: 80 food");
    return true;
}

// the epic is still an animal of the game's object list (BUG-0171: object_alive reads the
// creature's own memory and cannot tell a freed one; g_epic is a raw pointer kept for minutes)
bool epic_listed()
{
    struct Q {
        void* want;
        bool found = false;
    } q{g_epic};
    if (!g_epic)
        return false;
    game::for_each_object(
        [](void* o, uint32_t noun, void* u) {
            auto* q = (Q*)u;
            if (o == q->want)
                q->found = true;
            return !q->found;
        },
        &q);
    return q.found;
}

static void epic_tick(double now)
{
    if (g_epic && ((g_kept_refs_fix ? !kept_alive(g_epic, g_epic_serial, 0x18EB45Eu) // (BUG-0951: listed, the same object)
                                    : (!epic_listed() || !game::object_alive(g_epic))) ||
                   game::is_dead(g_epic))) {
        int food = g_epic_hitters.size() >= 2 ? 80 : 30;
        bonus_food(food);
        g_epic_kills++;
        autosave::milestone("an epic killed"); // E2 (BUG-0864)
        if (C.notice)
            C.notice(std::string("epic creature down: +") + std::to_string(food) + " food");
        g_epic = nullptr;
        epic_herd_release();
        g_epic_next = now + 600;
    }
    if (!g_epic_next)
        g_epic_next = now + 480; // the first one after 8 minutes together
    if (!g_epic && now > g_epic_next && C.sess->players().size() >= 2 && settings::on(settings::kTribeEpic)) { // F3
        g_epic_next = now + 600;
        spawn_epic();
    }
}

// the epic's hitters (animals go through the game's creature damage filter)
void note_animal_hit(void* animal, void* attacker_creature)
{
    HEntry* ah = animal && animal == g_epic ? hm_of(attacker_creature) : nullptr;
    if (ah) {
        uint8_t o = ah->data.owner;
        if (!g_fix_epic_players || (o < 0xFE && net::is_player(*C.sess, o)))
            g_epic_hitters.insert(o);
    }
}

// BUG-0340: every player's chief (the player's character in Tribe) could stand on another's: the
// members the game spawns for a tribe (its hut, the co-op split of new members, a rejoin) appear at
// one spot, and two chiefs standing in each other blocked each other. On the host, two chiefs that
// both stand still less than kChiefApart apart for a second are parted: the later owner's chief goes
// to its own spot on a ring round the village (kChiefRing m from the tribe's centre, a quarter turn
// per owner, on this game's ground). The guests follow through the snapshots.
// `coop.fix tribe.spread_chiefs off`: chiefs stay where they are.
static bool g_spread_fix = true;
COOP_FIX("tribe.spread_chiefs", "BUG-0340", g_spread_fix, "tribe: two players' chiefs standing in each other are parted onto a ring round the village");
constexpr float kChiefApart = 1.5f, kChiefRing = 12.f;
static std::map<std::pair<uint8_t, uint8_t>, double> g_stack_since;
static uint32_t g_spread_moves;

static net::Vec3 ring_spot(const net::Vec3& centre, uint8_t owner)
{
    float r = std::sqrt(centre.x * centre.x + centre.y * centre.y + centre.z * centre.z);
    if (r < 1.f)
        return centre;
    net::Vec3 up{centre.x / r, centre.y / r, centre.z / r};
    net::Vec3 a = std::fabs(up.y) < 0.9f ? net::Vec3{0, 1, 0} : net::Vec3{1, 0, 0};
    net::Vec3 e1{up.y * a.z - up.z * a.y, up.z * a.x - up.x * a.z, up.x * a.y - up.y * a.x};
    float l = std::sqrt(e1.x * e1.x + e1.y * e1.y + e1.z * e1.z);
    e1 = {e1.x / l, e1.y / l, e1.z / l};
    net::Vec3 e2{up.y * e1.z - up.z * e1.y, up.z * e1.x - up.x * e1.z, up.x * e1.y - up.y * e1.x};
    float t = 6.2831853f * (float)(owner % 8) / 4.f + (owner >= 4 ? 0.785f : 0.f);
    float cs = std::cos(t) * kChiefRing, sn = std::sin(t) * kChiefRing;
    net::Vec3 p{centre.x + e1.x * cs + e2.x * sn, centre.y + e1.y * cs + e2.y * sn, centre.z + e1.z * cs + e2.z * sn};
    float g = game::ground_radius(p), pr = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (g > 1.f && pr > 1.f)
        p = {p.x * g / pr, p.y * g / pr, p.z * g / pr};
    return p;
}

// a chief put on its spot also gets a walk order there (the game's own order, as a player's walk on
// the host): a member only put there walked back to where it stood before (seen: 9 m in 6 s)
static void put_chief(void* c, const net::Vec3& spot)
{
    game::teleport(c, spot, game::orientation(c));
    void* rec = thiscall<void*>(a_DoAction, c, 4, (void*)nullptr, (void*)nullptr);
    float p[3] = {spot.x, spot.y, spot.z};
    if (rec)
        thiscall<void>(a_SetValueVector3, (uint8_t*)rec + 0x39, (const float*)p);
}

static void spread_chiefs(double now)
{
    void* pt = player_tribe();
    if (!g_spread_fix || !pt)
        return;
    net::Vec3 centre = tribe_pos(pt);
    std::vector<std::pair<uint8_t, void*>> ch(g_chief.begin(), g_chief.end());
    std::sort(ch.begin(), ch.end());
    for (size_t i = 0; i < ch.size(); i++)
        for (size_t j = i + 1; j < ch.size(); j++) {
            auto key = std::make_pair(ch[i].first, ch[j].first);
            void *a = ch[i].second, *b = ch[j].second;
            bool stacked = game::object_alive(a) && game::object_alive(b) &&
                           net::dist(game::position(a), game::position(b)) < kChiefApart &&
                           drive::speed(game::velocity(a)) < 0.3f && drive::speed(game::velocity(b)) < 0.3f;
            if (!stacked) {
                g_stack_since.erase(key);
                continue;
            }
            auto it = g_stack_since.emplace(key, now).first;
            if (now - it->second < 1.0)
                continue;
            put_chief(b, ring_spot(centre, ch[j].first));
            g_spread_moves++;
            g_stack_since.erase(key);
            H->log(M, SK_INFO, "tribe: %s's chief stood in %s's: put on its own spot round the village",
                   C.sess->name_of(ch[j].first).c_str(), C.sess->name_of(ch[i].first).c_str());
        }
}

static void place_chiefs(double now)
{
    void* pt = player_tribe();
    if (!g_place_fix || !pt || g_place_due == 0 || now < g_place_due)
        return;
    if (now > g_place_until) {
        g_place_due = 0;
        return;
    }
    g_place_due = now + 2.0; // looked at again every 2 s inside the window (a guest's members come late)
    net::Vec3 centre = tribe_pos(pt);
    std::vector<std::pair<uint8_t, void*>> ch(g_chief.begin(), g_chief.end());
    std::sort(ch.begin(), ch.end());
    for (size_t j = 1; j < ch.size(); j++) {
        void* b = ch[j].second;
        if (!game::object_alive(b))
            continue;
        g_place_first.emplace(b, game::position(b)); // (the first look of this window)
        bool close = false;
        for (size_t i = 0; i < ch.size() && !close; i++) {
            void* a = ch[i].second;
            if (i == j || !game::object_alive(a))
                continue;
            close = net::dist(game::position(a), game::position(b)) <
                    game::bounding_radius(a) + game::bounding_radius(b) + 1.f;
        }
        if (!close)
            continue;
        // a chief that walks, or walked since it was first looked at in this window, moves by its player's
        // orders: not a chief stacked at the arrival (seen: walked next to the host's chief and put 13.8 m away)
        if (drive::speed(game::velocity(b)) >= 0.3f || net::dist(game::position(b), g_place_first[b]) > 1.5f) {
            if (g_placed_now.insert(b).second)
                g_place_walking++;
            continue;
        }
        if (g_placed_now.count(b))
            continue; // once per arrival
        g_placed_now.insert(b);
        put_chief(b, ring_spot(centre, ch[j].first));
        g_placed++;
        H->log(M, SK_INFO, "tribe: %s's chief stood next to another one at the arrival: put on its own spot round the village "
                           "(t %.3f)", C.sess->name_of(ch[j].first).c_str(), H->time_seconds());
    }
}

// BUG-0505: the grab behaviour (node 0x0462854D, tick 0xDAB4A0) claims one of the tribe's grab spots of the
// tool's type (SporeCoop::cTribe_ToolLayout: a ring ~7 m round the tool for the fixture's creature) and walks
// to it (node state 1: wait for the walk's arrival, no time limit). A spot next to another object (seen: 2.2 m
// from the fire pit, the member's radius 2.87) is never reached: the walk stops 1.5 m short and the member
// stands there for good, for the host's own orders as for a guest's (a second order with the same tool makes
// the same record and does not restart the node). Here a player tribe member standing still for 4 s in that
// state within 3 m of its walk's goal is set onto the goal facing the tool, and the game's behaviour goes on
// (arrival, the grab animation, SetSpecializedTool). `coop.fix tribe.grab_spot off`: it stands there.
uint32_t bt_node(void* c);
uint32_t cur_anim(void* c);
bool g_grab_spot_fix = true;
COOP_FIX("tribe.grab_spot", "BUG-0505", g_grab_spot_fix,
         "tribe: a member that cannot reach its spot at a tool rack is set onto it and takes the tool");
constexpr uint32_t kGrabNode = 0x0462854D;
uint32_t g_grab_spot_moves;
struct GrabWatch {
    net::Vec3 pos;
    double since;
};
static std::unordered_map<void*, GrabWatch> g_grab_watch;
static double g_grab_next;

// the leaf behaviour node's memory (the 6th argument of its enter / tick; docs/re/tribe.md#tribe-orders)
static uint32_t* leaf_mem(void* c)
{
    uint8_t* ai = F<uint8_t*>(c, 0xB4C);
    if (!ai || !skg::readable(ai + 0x1D0, 0x20))
        return nullptr;
    int depth = *(int*)(ai + 0x1DC);
    if (depth < 1 || depth > 15)
        return nullptr;
    uint32_t* m = (uint32_t*)(ai + 0x1D0 + 4 * (0xF + 0x2B * (depth - 1)));
    return skg::readable(m, 0x20) ? m : nullptr;
}

// the walk's goal of a citizen (spatial c+0xC0: list +0x1F4..+0x1F8 of 0x3C-byte goals, else +0x208)
static bool walk_goal(void* c, net::Vec3& g)
{
    uint8_t* sp = (uint8_t*)c + 0xC0;
    if (!skg::readable(sp + 0x1F4, 0x80))
        return false;
    uint8_t *lb = *(uint8_t**)(sp + 0x1F4), *le = *(uint8_t**)(sp + 0x1F8);
    float* p = (float*)(lb == le ? sp + 0x208 : le - 0x3C);
    if (!skg::readable(p, 12))
        return false;
    g = {p[0], p[1], p[2]};
    return true;
}

// a rotation whose forward (+Y) points from `at` toward `to` and whose up (+Z) is the planet's up at `at`
static net::Quat facing(const net::Vec3& at, const net::Vec3& to)
{
    float l = std::sqrt(at.x * at.x + at.y * at.y + at.z * at.z);
    if (l < 1.f)
        return {0, 0, 0, 1};
    net::Vec3 u{at.x / l, at.y / l, at.z / l}, d{to.x - at.x, to.y - at.y, to.z - at.z};
    float k = d.x * u.x + d.y * u.y + d.z * u.z;
    net::Vec3 f{d.x - k * u.x, d.y - k * u.y, d.z - k * u.z};
    float fl = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    if (fl < 1e-3f)
        return upright(at);
    f = {f.x / fl, f.y / fl, f.z / fl};
    net::Vec3 r{f.y * u.z - f.z * u.y, f.z * u.x - f.x * u.z, f.x * u.y - f.y * u.x}; // right = forward x up
    // the matrix with the columns r, f, u to a quaternion
    float m00 = r.x, m01 = f.x, m02 = u.x, m10 = r.y, m11 = f.y, m12 = u.y, m20 = r.z, m21 = f.z, m22 = u.z;
    float tr = m00 + m11 + m22;
    net::Quat q;
    if (tr > 0) {
        float s = std::sqrt(tr + 1.f) * 2;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.f + m00 - m11 - m22) * 2;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        float s = std::sqrt(1.f + m11 - m00 - m22) * 2;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        float s = std::sqrt(1.f + m22 - m00 - m11) * 2;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return q;
}

static void grab_spot_tick(double now)
{
    if (!g_grab_spot_fix || now < g_grab_next)
        return;
    g_grab_next = now + 0.5;
    void* pt = player_tribe();
    std::unordered_set<void*> seen;
    for (void* c : pt ? members(pt) : std::vector<void*>{}) {
        if (!game::object_alive(c) || game::is_dead(c) || bt_node(c) != kGrabNode)
            continue;
        uint32_t* m = leaf_mem(c);
        net::Vec3 goal;
        if (!m || m[0] != 1 || (int)m[6] < 0 || !m[1] || !walk_goal(c, goal))
            continue; // state 1: walking to the claimed spot m[6] of the tool m[1]
        seen.insert(c);
        net::Vec3 p = game::position(c);
        auto it = g_grab_watch.find(c);
        if (it == g_grab_watch.end() || net::dist(it->second.pos, p) > 0.3f || drive::speed(game::velocity(c)) > 0.3f) {
            g_grab_watch[c] = {p, now};
            continue;
        }
        float d = net::dist(p, goal);
        if (now - it->second.since < 4.0 || d > 3.f || d < 0.05f)
            continue;
        net::Vec3 tp = goal;
        game::object_position((void*)(uintptr_t)m[1], tp);
        H->log(M, SK_INFO, "tribe: member #%u stood %.1f s %.2f m short of its grab spot: set onto it at (%.1f %.1f %.1f), %.2f m over "
               "the ground (BUG-0505, BUG-1104)", (unsigned)member_id(c), now - it->second.since, d, goal.x, goal.y, goal.z,
               drive::over_ground(goal));
        game::teleport(c, goal, facing(goal, tp));
        g_grab_spot_moves++;
        g_grab_watch.erase(c);
        H->log(M, SK_INFO, "tribe: member #%u stood %.1f m short of its spot at a tool for 4 s: set onto it", member_id(c), d);
    }
    for (auto it = g_grab_watch.begin(); it != g_grab_watch.end();)
        it = seen.count(it->first) ? std::next(it) : g_grab_watch.erase(it);
}

// BUG-0531 (docs/re/tribe.md#tribe-conquer-caption): a village the player tribe conquers (its hut down to health 0) or
// allies plays TRG_TotemPole_Conquer / TRG_TotemPole_Social on the host, then TRG_Upgrade (the tribe's new level), and
// their captions wait for the host's OK with the game time standing (input lock, paused): every member, the guests' too, stands and takes no order (a guest's
// order changes the order record only) until the host clicks it away. A host who looks elsewhere or is away stops
// everybody's tribe for good (play_tribe: minutes). With guests in the session the caption closes itself after
// kVillageCaptionWait s (the game's EndCinematic, as the OK / Enter does) and everybody is told, as civ.caption
// does for a capture (BUG-0187). `coop.fix tribe.conquer_caption off`: the wait for the host's OK (0.7.10).
static bool g_conquer_caption_fix = true;
COOP_FIX("tribe.conquer_caption", "BUG-0531", g_conquer_caption_fix,
         "tribe: the host's caption of a conquered or allied village closes itself after 8 s when guests wait");
// the registry ids (FNV-1 of the lower-case names): TRG_TotemPole_Conquer, TRG_TotemPole_Social, TRG_Upgrade
constexpr uint32_t kVillageCines[] = {0x5418B038, 0x6A4BB900, 0x94D17D9F};
static bool village_cine(uint32_t id)
{
    return std::find(std::begin(kVillageCines), std::end(kVillageCines), id) != std::end(kVillageCines);
}
constexpr double kVillageCaptionWait = 8.0;
// BUG-0983: a guest's game plays the same captions when the realign conquers the village there too (BUG-0777) and
// stood paused (input lock, game time) until the guest clicked it, while the host's tribe went on after 8 s. Now a
// guest's village caption closes itself after the same 8 s. `coop.fix tribe.conquer_caption_guest off`: it waits for
// the guest's click (0.7.12).
static bool g_guest_caption_fix = true;
COOP_FIX("tribe.conquer_caption_guest", "BUG-0983", g_guest_caption_fix,
         "tribe: a guest's caption of a village conquered in the session closes itself after 8 s, as the host's does");
static uint32_t g_village_cine, g_village_captions_closed;
static double g_village_cine_since;
static bool g_village_cine_ending;
// tests (BUG-1101, `coop.tribe caption_race <msg hex> <send|post> <before|after>`): the guest's next forced end of a
// village caption also sends (MessageSend, at once) or posts (MessagePost, the game's next message pump) that message
// of the caption's buttons, before or after its own EndCinematic: a player's OK in the same moment
uint32_t g_race_msg;
bool g_race_post, g_race_before;
static void caption_race(bool before)
{
    if (!g_race_msg || before != g_race_before)
        return;
    uint32_t msg = g_race_msg;
    g_race_msg = 0;
    uintptr_t get = H->sym("App::IMessageManager::Get");
    void* mm = get ? skg::cdecl_call<void*>(get) : nullptr;
    static uint8_t data[0x40];
    if (!mm)
        return;
    if (g_race_post)
        skg::vcall<void>(mm, 0x18 / 4, msg, (void*)nullptr, (void*)nullptr, 0);
    else
        skg::vcall<void>(mm, 0x14 / 4, msg, (void*)data, 0);
    H->log(M, SK_INFO, "tribe: tests: the caption's message %08X %s %s the forced end (now %08X, BUG-1101)", msg,
           g_race_post ? "posted" : "sent", before ? "before" : "after", world::cinematic_id());
}
static void village_caption_tick(double now)
{
    uint32_t id = world::cinematic_id();
    bool village = village_cine(id);
    bool host = is_host();
    bool guests = C.sess->players().size() >= 2;
    if (!village) {
        g_village_cine = 0;
        g_village_cine_ending = false;
        return;
    }
    if (id != g_village_cine) {
        bool chained = g_village_cine != 0; // TRG_Upgrade right after the conquest: told once
        g_village_cine = id;
        g_village_cine_since = now;
        g_village_cine_ending = false;
        H->log(M, SK_INFO, "tribe: the village cinematic %08X plays on the %s (BUG-0531)", id, host ? "host" : "guest");
        if (host && guests && C.notice && !chained)
            C.notice(C.sess->name_of(C.sess->my_id()) + " watches a cinematic: the tribe waits for the OK" +
                     (g_conquer_caption_fix ? " (or " + std::to_string((int)kVillageCaptionWait) + " s)" : ""));
    }
    if (!(host ? g_conquer_caption_fix : g_guest_caption_fix) || !guests || g_village_cine_ending ||
        now - g_village_cine_since < kVillageCaptionWait)
        return;
    g_village_cine_ending = true;
    H->run_outside_frame(
        host ? +[](void*) {
            uint32_t now_id = world::cinematic_id();
            if (!village_cine(now_id) || game::mode_id() != modes::kTribe ||
                !world::end_cinematic("in Tribe after 8 s with guests waiting (BUG-0531)"))
                return;
            g_village_captions_closed++;
            if (C.notice)
                C.notice("the caption closed by itself: the tribe goes on");
        }
             : +[](void*) { // BUG-0983: the guest's own game only (no notice: the host's tribe did not wait)
                   if (!village_cine(world::cinematic_id()) || game::mode_id() != modes::kTribe)
                       return;
                   caption_race(true);
                   if (!world::end_cinematic("in Tribe after 8 s on a guest (BUG-0983)"))
                       return;
                   g_village_captions_closed++;
                   caption_race(false);
               },
        nullptr);
}

static void coop_tick(double now)
{
    chiefs_tick();
    if (!is_host())
        village_caption_tick(now); // BUG-0983
    if (is_host()) {
        village_caption_tick(now);
        grab_spot_tick(now);
        spread_chiefs(now);
        place_chiefs(now);
        duet_tick(now);
        epic_tick(now);
    }
}

static void coop_init()
{
    a_CitizenTD = H->sym("Simulator::cCreatureCitizen::TakeDamage");
    a_HealTick = H->sym("SporeCoop::BT_HealTick");
    a_PatientOf = H->sym("SporeCoop::BT_ValidTarget");
    a_AddFood = H->sym("SporeCoop::cTribe_AddFood");
    a_FoodFx = H->sym("Simulator::cTribe::UpdateFoodVisuals");
    a_SessionGet = H->sym("SporeCoop::SocialSession_Get");
    a_CreateHerd = H->sym("Simulator::cGameNounManager::CreateHerd");
    a_HerdEnable = H->sym("Simulator::cHerd::SetEnabled");
    a_CreatureCreate = H->sym("Simulator::cCreatureAnimal::Create");
    spaceext::on_save(before_save); // space_allies' save hook (the one hook on SaveGame)
    if (a_CitizenTD)
        H->hook_create(M, (void*)a_CitizenTD, (void*)h_CitizenTD, (void**)&o_CitizenTD);
    if (a_HealTick && a_PatientOf)
        H->hook_create(M, (void*)a_HealTick, (void*)h_HealTick, (void**)&o_HealTick);
    H->log(M, SK_INFO, "tribe co-op: citizen damage %s, heal %s, duet %s, epic %s", a_CitizenTD ? "on" : "off",
           a_HealTick && a_PatientOf ? "on" : "off", a_SessionGet ? "on" : "off",
           a_CreateHerd && a_HerdEnable && a_CreatureCreate ? "on" : "off");
}

// a member that changed hands (a gift) stayed selected on the giver's machine, so the giver could
// still order the partner's member (play_tribe.py): drop members of others from the own selection
static uint32_t g_unselected;
static void drop_foreign_selection()
{
    void* pt = player_tribe();
    if (!pt)
        return;
    uint8_t me = C.sess->my_id();
    for (void* c : members(pt)) {
        uint8_t o = owner_of(c);
        if (o < 0xFE && o != me && F<uint8_t>(c, kSelectedOff)) {
            vcall<int>((uint8_t*)c + 0xC0, 0x54 / 4, 0); // SetSelected(false)
            g_unselected++;
        }
    }
}

FrameTool g_frame_tool;
static void frame_tool_run()
{
    FrameTool f = g_frame_tool;
    g_frame_tool.armed = false;
    void* t = f.ix >= 0 && f.ix < (int)g_guest.local_tribe_of.size() ? g_guest.local_tribe_of[f.ix] : nullptr;
    net::Vec3 c = t ? tribe_pos(t) : net::Vec3{};
    float l = std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
    H->log(M, SK_INFO, "tribe: tests: frametool tribe %d (mapped to %p) type %d", f.ix, t, f.type);
    H->run_outside_frame([](void*) {}, nullptr);
    if (l < 1.f)
        return;
    net::Vec3 p{c.x + 0.3f * f.metres, c.y, c.z};
    void* tool = make_tool(f.ix, f.type, p);
    H->log(M, SK_INFO, "tribe: tests: frametool made %p", tool);
}

// BUG-0693: a guest's command (an order, a baby bought, a gift) that reaches a host out of the Tribe mode (an editor, a
// menu, a load) is held and run when the host is back in the same Tribe world (net/held_cmds.h). Off: dropped (0.7.10).
static bool g_cmd_hold_fix = true;
COOP_FIX("tribe.cmd_hold", "BUG-0693", g_cmd_hold_fix,
         "tribe: a guest's command made while the host is in an editor or a menu runs when the host is back");
// C3 (BUG-1142): the one held-command pump of busy_model.h (Tribe's row: held, run after kSettle s back in the stage)
static busy_model::HeldPump g_held;
static const busy_model::Row& held_row() { return *busy_model::row_of(stage_epoch::index_of(modes::kTribe)); }
PLAYER_STATE("tribe.held_cmds", [](uint8_t id) { g_held.forget(id); }, // BUG-0422 / BUG-0693: a leaver's held commands
             [](uint8_t id) -> size_t { return g_held.held_of(id); });
static uint32_t g_cmd_world; // BUG-0693: the save this game last played the stage in (modes::current_world)

TribeHunt g_th;
static void tribe_hunt_tick()
{
    if (g_th.left <= 0 || !g_th.target)
        return;
    g_th.left--;
    void* t = player_tribe();
    if (!t)
        return;
    void* c = spawn_member(t, 1);
    g_th.tries++;
    if (!c)
        return;
    if ((uint8_t*)c == g_th.target) {
        g_th.hit = c;
        g_th.hit_serial = game::object_serial(c);
        g_th.left = 0;
        H->log(M, SK_INFO, "tribe reuse: the freed citizen address %p holds a new member of the player tribe (serial %u, try %d)",
               c, g_th.hit_serial, g_th.tries);
        return;
    }
    vcall<void>(t, 0x9C / 4, c, 1);
}
std::string tribe_hunt_status()
{
    char b[400];
    bool l = g_th.hit && game::listed(g_th.hit, kCitizenNoun) && game::object_serial(g_th.hit) == g_th.hit_serial;
    net::Vec3 q = l ? game::position(g_th.hit) : net::Vec3{};
    GEntry* it = g_guest.mirrors.find(g_th.id);
    net::Vec3 want = it ? it->data.pos : net::Vec3{};
    GEntry* gi = l ? gm_of(g_th.hit) : nullptr;
    snprintf(b, sizeof b, "tribe reuse target=%p mirror=%u left=%d tries=%d hit=%p alive=%d own_tribe=%d pos=(%.2f %.2f %.2f) "
             "host_member=(%.2f %.2f %.2f) bound_as=%d stale=%u refused=%u ring=%d ring_order=%d kept_stale=%u\n",
             (void*)g_th.target, g_th.id, g_th.left, g_th.tries, g_th.hit, (int)l,
             (int)(l && tribe_holding(g_th.hit) == player_tribe()), q.x, q.y, q.z, want.x, want.y, want.z,
             gi ? (int)gi->id : -1, g_guest.stale, g_guest.remove_refused,
             l ? (int)ring_shown(g_th.hit) : -1, (int)(g_ring.target && g_ring.target == g_th.target), g_kept_stale);
    return b;
}

// BUG-0947 (docs/re/tribe.md#tribe-village-load-stamp): a village has two ground stamps: the one made with it
// (0xC9DEE0) and the tribe's own (cTribe +0x19D4, SporeCoop::cTribe_ApplyGroundStamp 0xC91890: 50 m wide). The live
// Creature -> Tribe change and every load make the tribe's stamp (a load: for a village saved without it); the NPC
// tribe controller's forming and SpawnNpcTribe (a guest's copy of the host's) do not. So the games that saw a village
// made had one stamp there and a game that loaded the world later (a guest's rejoin, a host's reload) two: its ground
// 50 m round the village 5-8 m off (lab 20261112-094334: 7.95 m). The same stamp applied live gives the loaded ground
// exactly (lab: the host stamped it by hand, 0.00 m at every point). Fix: every game in Tribe gives a village without
// it the tribe's stamp through the game's own call, a second after it appeared. Off: the village keeps one stamp.
static bool g_village_stamp_fix = true;
COOP_FIX("tribe.village_load_stamp", "BUG-0947", g_village_stamp_fix,
         "tribe: a village made during play gets the ground stamp a load of the world gives it (a rejoining guest's ground round it was 5-8 m off)");
static void village_stamps_tick(double now)
{
    static double last;
    static std::unordered_map<void*, double> seen; // tribe -> first seen without its stamp
    if (now - last < 0.5 && now >= last)
        return;
    last = now;
    if (!g_village_stamp_fix || !a_TribeGroundStamp || game::mode_id() != modes::kTribe) {
        seen.clear();
        return;
    }
    std::unordered_map<void*, double> next;
    for (void* t : all_tribes()) {
        if (!skg::readable((uint8_t*)t + kTribeStampOff, 4) || F<uint32_t>(t, kTribeStampOff) || !F<void*>(t, kTribeStampPropOff))
            continue;
        auto it = seen.find(t);
        double since = it == seen.end() ? now : it->second;
        if (now - since < 1.0) { // (the live change makes it itself right after the village)
            next[t] = since;
            continue;
        }
        thiscall<void>(a_TribeGroundStamp, t);
        net::Vec3 p = tribe_pos(t);
        H->log(M, SK_INFO, "tribe: the village at (%.0f %.0f %.0f) made during play gets the ground stamp a load gives it: id %u (BUG-0947)",
               p.x, p.y, p.z, F<uint32_t>(t, kTribeStampOff));
    }
    seen.swap(next);
}

void tick(double now)
{
    tribe_hunt_tick();
    if (is_guest() || g_go_paused)
        gameover_guest_tick(now); // BUG-0960 (also outside Tribe: the wait ends there)
    if (is_guest())
        pause_net_tick(now); // BUG-1101
    foreign_note_tick(now); // BUG-1087
    if (g_frame_tool.armed && is_guest())
        frame_tool_run();
    village_stamps_tick(now); // BUG-0947
    species_tick(now);        // BUG-1271 (also after the session: the members the mod changed go back)
    if (game::mode_id() == modes::kTribe) {
        hand_repair_tick(now); // BUG-1335
        if (g_ok)
            theme_tick(); // BUG-1496
    }
    if (!active())
        return;
    g_cmd_world = modes::current_world();
    // BUG-0693: the commands that came while this game was away run once the stage settled again (a load, an editor)
    g_held.tick(held_row(), game::mode_id() == modes::kTribe, modes::current_world(), now, [](const net::HeldCommands::Cmd& c) {
        net::Reader r(c.bytes.data(), c.bytes.size());
        H->log(M, SK_INFO, "tribe: a command of player %u held while away runs now", c.from);
        host_command(c.from, r);
    });
    drop_foreign_selection();
    away_conquest_tick(now); // BUG-0879 (guest)
    if (is_host() && game::mode_id() == modes::kTribe)
        away_goal_tick(now); // BUG-1492
    if (is_host())
        host_tick(now);
    else if (is_guest()) {
        away_tree_end_tick(); // BUG-1084
        guest_tick(now);
        held_items_tick(now); // BUG-0906
    }
    coop_tick(now);
}

uint32_t g_rx, g_rx_skipped, g_resets;

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (!g_ok)
        return;
    if (type == net::MSG_TRIBE_STATE) {
        g_rx++;
        if (!is_guest() || game::mode_id() != modes::kTribe)
            g_rx_skipped++;
    }
    if (type == net::MSG_TRIBE_STATE && is_guest() && game::mode_id() == modes::kTribe && !g_state_hold)
        guest_state(r);
    else if (type == net::MSG_TRIBE_CMD && is_host() && game::mode_id() == modes::kTribe)
        host_command(from, r);
    else if (type == net::MSG_TRIBE_CMD && is_host() && g_cmd_hold_fix && r.left()) { // BUG-0693: run when back
        g_held.hold(held_row(), from, r.cur(), r.left(), g_cmd_world, H->time_seconds());
        H->log(M, SK_INFO, "tribe: a command of player %u held: this game is not in Tribe (mode %08X)", from, game::mode_id());
    } else if (type == net::MSG_TRIBE_ANIM && is_guest()) { // C7 (BUG-1160): the host's members' animations and waits
        uint8_t kind = r.u8();
        if (!r.ok() || !wire::tribe_anim::is_tribe_kind(kind)) {
            g_anim_rx_bad++;
            return;
        }
        g_anim_rx_new++;
        on_member_anims(kind, r); // (it decides per kind whether it applies outside Tribe: wire::tribe_anim::any_mode)
    }
}

// BUG-0217: forget the co-op mechanics' pointers of a world that is gone (no game call: the
// objects are freed). The epic and its herd are no longer ours to destroy or quiet.
// tribe.chiefs gates only the chief and pincer part; the epic part has its own switch
// (tribe.epic_herd) and runs whatever tribe.chiefs says (BUG-0214: one bug per switch)
static void forget_world_tables()
{
    g_epic_blows.clear(); // BUG-0515 diagnostics: keys are animals of the world left
    if (g_fix_chiefs) {
        g_chief_scale.clear();
        g_chief.clear();
        g_chief_saved.clear();
        g_pincer.clear();
        g_pincer_told.clear();
    }
    g_epic = nullptr;
    g_epic_herd = nullptr;
    g_epic_hitters.clear();
    if (g_fix_epic_herd)
        g_epic_next = 0; // BUG-0067: the first epic of the next session comes after 8 minutes again
}

// the co-op mechanics' tables of game pointers (chiefs, pincer, epic): a new session or world
// must not inherit them (BUG-0081: a former chief's size was restored through a freed pointer)
static void clear_coop_tables(bool restore)
{
    if (restore) {
        if (g_fix_chiefs)
            restore_chief_scales(nullptr); // the chiefs are plain members again after the session
        // the co-op epic ends with the session (BUG-0121: not left in the solo game and its saves)
        if (g_fix_epic_herd && g_epic && game::mode_id() == modes::kTribe && epic_listed())
            game::destroy_creature(g_epic);
        epic_herd_release();
    }
    forget_world_tables();
}

// BUG-0121: the host saves (pause menu, quit, the world sent to a joining guest): a live co-op epic
// and its herd's spawning must not go into the save, where a load brought it back as an untracked
// epic. The epic leaves without a reward; the next one comes a minute later.
void before_save()
{
    if (!g_ok || !g_fix_epic_herd || !g_epic || game::mode_id() != modes::kTribe)
        return;
    if (epic_listed())
        game::destroy_creature(g_epic);
    g_epic = nullptr;
    g_epic_hitters.clear();
    epic_herd_release();
    g_epic_saved_out++;
    double now = H->time_seconds();
    if (g_epic_next < now + 60)
        g_epic_next = now + 60;
    if (C.notice && is_host())
        C.notice("the epic creature went away");
}

void on_session_end()
{
    took_clear(); // BUG-1522
    g_test_gone.clear();
    g_away_pending.clear(); // BUG-1492
    g_held.clear(); // BUG-0693
    g_hold_seen.clear(); // BUG-0906
    for (auto& [c, t] : g_detached) // BUG-0667: the bodies stay in this world; the game tears them down later
        if (game::object_alive(c))
            restore_body_tribe(c, t);
    g_detached.clear();
    g_bt_frozen.clear();
    g_bt_freeze_at.clear();
    g_bt_local.clear(); // (g_left_tribe stays until the world goes: its citizens are torn down later)
    theme_session_end(); // BUG-0568, BUG-1496: the relay's slots stop here
    party_forget(); // BUG-0646
    baby_forget(); // BUG-0671
    clear_coop_tables(true);
    g_host.split_for.clear();
    g_host.members.clear();
    g_guest.mirrors.for_each([](GEntry& e) { // BUG-0946: no air hold left on a mirror (a pointer compare only)
        if (e.data.air)
            game::air_hold(e.ref.p, false);
    });
    g_guest.mirrors.clear();
    g_npc_tribe_made.clear(); // BUG-0381
    g_npc_tribe_tried.clear();
    g_host.dealt = 0;
    if (g_world_map_fix)
        g_guest.local_tribe_of.clear(); // BUG-0672: the old world's tribes are freed; the next tribe state maps the new ones
}
LIFECYCLE_HOOK("tribe.session", lifecycle::kSessionEnd, -50, [](const lifecycle::Ctx&) { on_session_end(); }, true); // C2 (BUG-1141): end_session

// a world of the Tribe stage is gone (a load, a received world, the stage left, the game-over
// bounce back): every pointer of it is forgotten, with no game call (BUG-0217: no longer through
// on_session_end, whose restoring clear would touch the old world if the order ever changed)
void on_world_change()
{
    took_clear(); // BUG-1522: the taken members' tribes are freed with the world
    g_test_gone.clear();
    g_away_pending.clear(); // BUG-1492 (a conquest of the old world)
    theme_forget(); // BUG-0568
    party_forget(); // BUG-0646
    baby_forget(); // BUG-0671
    g_resets++;
    g_mclaims_serial = 0; // E1 (BUG-1154): the claims of the world loaded now come from the store again
    g_member_claim.clear();
    g_member_flags.clear();
    g_members_matched = false;
    forget_world_tables(); // every citizen of the old world is freed: nothing to restore
    g_detached.clear(); // (BUG-0667: freed with the world)
    g_left_tribe.clear();
    g_bt_local.clear();
    g_host.split_for.clear();
    g_host.members.clear();
    g_guest.mirrors.for_each([](GEntry& e) { // BUG-0946: no air hold left on a mirror (a pointer compare only)
        if (e.data.air)
            game::air_hold(e.ref.p, false);
    });
    g_guest.mirrors.clear();
    g_npc_tribe_made.clear(); // BUG-0381
    g_npc_tribe_tried.clear();
    g_host.dealt = 0;
}

// BUG-0217: which Tribe world is open. Before, any frame without the stage's local entity (every
// editor: no adapter) counted as a world change, so an editor visit dropped the live co-op epic
// untracked into the share save that follows. C2 (BUG-1141): world_id.h is the one answer (the stage
// counter: a load, a received world, another stage; leaving Tribe for a mode that is not an editor;
// another player tribe object or save file in the stage); coop.cpp fires the hook below.
uint32_t g_world_checks_fired;

// C2 (BUG-1141): the Tribe world that was open changed or closed (world_id.h fires it; an editor
// visit keeps it, decision 2)
LIFECYCLE_HOOK("tribe.world", lifecycle::kWorldChange, -50, [](const lifecycle::Ctx& c) {
    if (c.from_stage != stage_epoch::index_of(modes::kTribe))
        return;
    g_world_checks_fired++;
    if (g_ok)
        H->log(M, SK_INFO, "tribe: the world changed (%s, epoch %u)", world_id::why_name((world_id::Why)c.why),
               game::stage_epoch(modes::kTribe));
    on_world_change();
}, true);
// the Tribe world's root object for world_id.h (coop.cpp): the player tribe, null outside Tribe
const void* world_anchor() { return g_ok && game::mode_id() == modes::kTribe ? player_tribe() : nullptr; }


bool center(net::Vec3& pos)
{
    void* t = g_ok ? player_tribe() : nullptr;
    if (!t)
        return false;
    pos = tribe_pos(t);
    return true;
}

void for_each_owned(void (*fn)(void* citizen, uint8_t owner, void* user), void* user)
{
    void* t = active() ? player_tribe() : nullptr;
    if (!t)
        return;
    for (void* c : members(t)) {
        uint8_t o = owner_of(c);
        if (o != 0xFF && o != 0xFE)
            fn(c, o, user);
    }
}

void for_each_tracked(void (*fn)(const TrackMember& m, void* user), void* user)
{
    if (!active())
        return;
    if (is_host()) {
        g_host.members.for_each([&](HEntry& e_) {
            void* const c = e_.ref.p;
            HMember& hm = e_.data;
            if (hm.owner < 0xFE && hm_ok(e_))
                fn({e_.id, hm.owner, c, nullptr, drive::speed(game::velocity(c)), 0, nullptr}, user);
        });
        return;
    }
    double now = H->time_seconds();
    g_guest.mirrors.for_each([&](GEntry& e_) {
        const uint16_t id = e_.id;
        GMember& g = e_.data;
        if (g.owner >= 0xFE || !mirror_ok(e_) || now - g.seen > 3.0) // (BUG-0955)
            return;
        fn({id, g.owner, e_.ref.p, &g.aim, g.aim_speed, (uint16_t)g.ds.teleports, &g.ds.drawn}, user);
    });
}

std::string member_brief(void* c)
{
    if (!c || !game::object_alive(c) || !skg::readable(c, 0x1040))
        return "gone";
    char b[160];
    snprintf(b, sizeof b, "bt=%08X hp=%.0f dead=%d age=%u tool=%d item=%d leader=%d", bt_node(c), game::health(c),
             (int)game::is_dead(c), F<uint8_t>(c, kAgeOff), F<int>(c, kHeldToolOff), F<int>(c, kHeldItemOff), (int)is_leader(c));
    return b;
}

void for_each_diag(void (*fn)(const char* key, const net::Vec3& p, void* user), void* user)
{
    if (!active() || is_host())
        return;
    double now = H->time_seconds();
    char k[8];
    g_guest.mirrors.for_each([&](GEntry& e_) {
        const uint16_t id = e_.id;
        GMember& g = e_.data;
        if (!mirror_ok(e_) || now - g.seen > 3.0 || g.buf.empty()) // (BUG-0955)
            return;
        snprintf(k, sizeof k, "a%u", (unsigned)id);
        fn(k, g.aim, user);
        snprintf(k, sizeof k, "r%u", (unsigned)id);
        fn(k, g.buf.raw_lead(now), user);
        snprintf(k, sizeof k, "l%u", (unsigned)id);
        fn(k, net::Vec3{(float)g.buf.late_behind(now), g.buf.lead_off() ? 1.f : 0.f, g.ds.last_err}, user);
    });
}

static std::string audit_status()
{
    char b[400];
    snprintf(b, sizeof b,
             "tribe audit 0.7.17: away_goal fix=%d missions=%u pending=%u retries=%u (BUG-1492); elsewhere_anims fix=%d "
             "dropped=%u (BUG-1495); theme_end ended=%u later=%u lost=%u (BUG-1496); air_released=%u (BUG-1493)\n",
             (int)g_away_goal_fix, g_away_goals, (unsigned)g_away_pending.size(), g_away_retries, (int)g_elsewhere_anims_fix,
             g_elsewhere_anims_dropped, g_theme_ended, g_theme_later_n, g_theme_lost, g_air_released);
    return b + took_status();
}

std::string status()
{
    char b[1024];
    if (C.sess && C.sess->role() == net::Role::Host)
        snprintf(b, sizeof b, "tribe(host): members=%u cmds_done=%u rejected=%u gifts=%u unselected=%u bodies_sent=%u village_captions_closed=%u member_moves=%u/%u cmd_gone=%u guest_missions=%u\n",
                 (unsigned)g_host.members.size(), g_cmds_done, g_cmds_rejected, g_gifts, g_unselected, g_host.bodies_sent, g_village_captions_closed,
                 g_member_moves, g_member_moves_unowned, g_cmd_gone, g_guest_missions);
    if (C.sess && C.sess->role() == net::Role::Host) { // E1 (BUG-1154)
        char c[200];
        snprintf(c, sizeof c, "claims: fix=%d serial=%u members=%u matched=%d from_disk=%u held=%u moved=%u\n", (int)g_owners_fix,
                 g_mclaims_serial, (unsigned)g_member_claim.size(), (int)g_members_matched, g_mclaims_read, g_mclaims_held,
                 g_mclaims_moved);
        strncat(b, c, sizeof b - strlen(b) - 1);
    } else
        snprintf(b, sizeof b,
                 "tribe(guest): mirrors=%u spawned=%u removed=%u cmds_sent=%u rx=%u skipped=%u resets=%u "
                 "local_tribes=%u player_tribe=%p unselected=%u incomplete=%u older_chunks=%u ai_skipped=%u/%u away_orders=%u local=%d mirror_walks=%u timed=%u "
                 "npc_tribes_made=%u failed=%u mirror_grounded=%u ours=%u/%u save_stripped=%u/%u forms_blocked=%u forms_run=%u unmapped_removed=%u bodies_detached=%u teardown_restored=%u slot_null_skipped=%u teardown_tribe_back=%u teardown_bt_emptied=%u map_stale_skipped=%u join_bodies=%u mirrors_grown=%u away_trees_ended=%u\n",
                 (unsigned)g_guest.mirrors.size(), g_guest.spawned, g_guest.removed, g_cmds_sent, g_rx, g_rx_skipped, g_resets,
                 (unsigned)all_tribes().size(), player_tribe(), g_unselected, g_guest.snap.incomplete, g_guest.snap.older, g_bt_skipped, g_bt_calls, g_away_orders, (int)local_play(),
                 g_mirror_walks, g_mirror_timed, g_npc_tribes_made, g_npc_tribes_failed, g_mirror_grounded, (unsigned)g_ours_c.size(),
                 (unsigned)g_ours_t.size(), g_save_stripped, g_save_saves, g_forms_blocked, g_forms_run, g_unmapped_removed, g_bodies_detached, g_teardown_restored, g_slot_null_skipped, g_teardown_tribe_back,
                 g_teardown_bt_emptied, g_map_stale_skipped, g_join_bodies, g_mirrors_grown, g_away_trees_ended);
    std::string out = b;
    // C1 / C4 (BUG-1140, BUG-1143): the tables and the snapshot receiver
    // BUG-1460: dead mirrors kept still (tribe.body_still), make-way calls of tribe-less citizens left out (makeway_null)
    snprintf(b, sizeof b, "tribe bodies: still fix=%d stilled=%u | makeway_null fix=%d skipped=%u\n", (int)g_body_still_fix,
             g_bodies_stilled, (int)g_makeway_null_fix, g_makeway_null_skipped.load(std::memory_order_relaxed));
    out += b;
    out += "tribe registry: " + g_host.members.status() + "; " + g_guest.mirrors.status() + "\n";
    snprintf(b, sizeof b, "tribe snapshot rx: chunks=%u bad=%u older=%u cut=%u incomplete=%u abandoned=%u complete=%u generation=%u\n",
             g_guest.snap.chunks_in, g_guest.snap.bad, g_guest.snap.older, g_guest.snap.cut, g_guest.snap.incomplete, g_guest.snap.abandoned,
             g_guest.snap.complete, g_guest.snap.generation);
    out += b;
    if (!g_last_cmd.empty())
        out += "tribe last order: " + g_last_cmd + "\n";
    out += "tribe ui mode=" + std::to_string(ui_mode()) + " (0 social, 1 attack)\n";
    if (!g_last_local.empty())
        out += "tribe last local order: #" + std::to_string(g_local_orders) + " " + g_last_local + "\n";
    snprintf(b, sizeof b, "tribe game over wait: host_up=%d why=%u hut_ups=%u sent=%u paused=%d pauses=%u resumes=%u (BUG-0960, BUG-1081); host pauses told %u (BUG-1086); foreign clicks %u told %u (BUG-1087); body cards off %u (BUG-0961); unsent holds ended %u (BUG-0962)\n",
             (int)g_go_host_up, (unsigned)g_go_why, g_go_hut_ups, g_go_sent, (int)g_go_paused, g_go_pauses, g_go_resumes, g_hp_ups,
             g_foreign_clicks, g_foreign_told, g_body_cards, g_hold_only_ended);
    out += b;
    snprintf(b, sizeof b, "tribe anim msg: fix=%d sent_new=%u sent_old=%u rx_new=%u rx_bad=%u (BUG-1160)\n", (int)g_anim_msg_fix,
             g_anim_sent_new, g_anim_sent_old, g_anim_rx_new, g_anim_rx_bad);
    out += b;
    out += audit_status(); // 0.7.17 WP-B1
    snprintf(b, sizeof b, "tribe pause net: releases=%u rebalanced=%u standing=%.0f s (BUG-1101)\n", g_net_releases,
             g_net_rebalanced, g_net_since ? H->time_seconds() - g_net_since : 0.0);
    out += b;
    out += "tribe " + species_status(); // BUG-1271
    std::string chiefs;
    for (auto& [o, c] : g_chief)
        chiefs += " " + std::to_string(o) + ":#" + std::to_string(member_id(c));
    snprintf(b, sizeof b,
             "tribe co-op: pincer=%u aura=%u chief_rescues=%u duet=%u heals=%u epics=%u epic_kills=%u epic=%p "
             "chiefs=%s spread=%u placed=%u place_walking=%u\n",
             g_pincer_hits, g_aura_hits, g_chief_rescues, g_duet_ticks, g_heals, g_epics, g_epic_kills, g_epic,
             chiefs.c_str(), g_spread_moves, g_placed, g_place_walking);
    out += b;
    // the chiefs' size in the game (spatial GetScale / their own scale) and the lifetime checks
    std::string scales;
    {
        game::LiveScope live; // BUG-0919: a chief of the last tick is asked only when the game's list has it now
        for (auto& [o, c] : g_chief) {
            auto it = g_chief_scale.find(c);
            char sb[32];
            bool ok = !npc::entries_listed() || game::listed(c);
            snprintf(sb, sizeof sb, ok ? " %u:%.2f" : " %u:gone", o,
                     ok && it != g_chief_scale.end() && it->second > 0 ? game::scale(c) / it->second : 0.f);
            scales += sb;
        }
    }
    snprintf(b, sizeof b,
             "tribe lifetime: chief_restores=%u chief_gone=%u chief_bad_calls=%u spawns_blocked=%u mirror_out_passed=%u fix_chiefs=%d "
             "fix_gameover=%d knocked_out=%u ko_replaced=%u mirror_blows_kept=%u epic_herd_reused=%u epic_saved_out=%u chief_scale=%s\n",
             g_chief_restores, g_chief_gone, g_chief_bad_calls, g_spawns_blocked, g_mirror_out_passed, (int)g_fix_chiefs, (int)g_fix_gameover,
             g_ko_shown, g_ko_replaced, g_mirror_blows_kept, g_epic_herd_reused, g_epic_saved_out, scales.c_str());
    out += b;
    snprintf(b, sizeof b, "tribe hand bone: fix=%d waits=%u repairs=%u fx_skips=%u (BUG-1335)\n", (int)game::fx_bone_fix(),
             g_hand_waits, g_hand_repairs, game::fx_bone_skips());
    return out + b;
}

bool near_member(const net::Vec3& p, float radius)
{
    struct Ctx { const net::Vec3* p; float r; uint8_t me; bool hit; } ctx{&p, radius, (uint8_t)(C.sess ? C.sess->my_id() : 0), false};
    for_each_owned([](void* c, uint8_t owner, void* user) {
        auto* q = (Ctx*)user;
        if (!q->hit && owner == q->me && net::dist(game::position(c), *q->p) < q->r)
            q->hit = true;
    }, &ctx);
    return ctx.hit;
}

std::vector<net::Vec3> local_member_positions()
{
    std::vector<net::Vec3> v;
    std::pair<std::vector<net::Vec3>*, uint8_t> ctx{&v, C.sess ? C.sess->my_id() : 0};
    for_each_owned([](void* c, uint8_t owner, void* user) {
        auto* p = (std::pair<std::vector<net::Vec3>*, uint8_t>*)user;
        if (owner == p->second)
            p->first->push_back(game::position(c));
    }, &ctx);
    return v;
}

// the citizen's current behaviour-tree node (ai = c+0xB4C, node id at ai+0x1D8; docs/re/tribe.md B)
uint32_t bt_node(void* c)
{
    uint8_t* ai = F<uint8_t*>(c, 0xB4C);
    return ai && skg::readable(ai + 0x1D8, 4) ? *(uint32_t*)(ai + 0x1D8) : 0;
}

uint32_t cur_anim(void* c)
{
    uint32_t an = 0;
    return game::drawn_state(c, nullptr, &an) ? an : 0;
}

// BUG-0664 (docs/re/tribe.md#tribe-sync-bodies): the sync-check compares members, not bodies. The host
// (BUG-0620) keeps a dead member's body in g_hm while the host's game keeps it, but a guest spawns no mirror for a body
// (it only lets a mirror it already has lie there): a guest that joined or loaded the world while a body lay on the
// host had none, and the check said "t: 1 missing on the guest" (long_tribe on rel/0.7.9, after its restart and its
// rejoin). Both sides leave bodies out of the check (BUG-0664).

std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    game::LiveScope live; // BUG-0919 (one walk of the object list)
    if (C.sess && C.sess->role() == net::Role::Host) {
        g_host.members.for_each([&](HEntry& e_) {
            void* const c = e_.ref.p;
            HMember& h = e_.data;
            if ((npc::entries_listed() ? game::listed(c) : game::object_alive(c)) && h.body_since == 0)
                v.push_back({"t" + std::to_string(e_.id), game::position(c), game::orientation(c)});
        });
    } else {
        g_guest.mirrors.for_each([&](GEntry& e_) {
            const uint16_t id = e_.id;
            GMember& g = e_.data;
            if (mirror_ok(e_) && !g.body) // (BUG-0955)
                v.push_back({"t" + std::to_string(id), game::position(e_.ref.p), game::orientation(e_.ref.p)});
        });
    }
    return v;
}

int own_selected()
{
    if (!C.sess || !C.sess->connected() || game::mode_id() != modes::kTribe)
        return 0;
    int n = 0;
    uint8_t me = C.sess->my_id();
    if (C.sess->role() == net::Role::Host) {
        g_host.members.for_each([&](HEntry& e_) {
            void* const c = e_.ref.p;
            HMember& hm = e_.data;
            if (hm.owner == me && F<uint8_t>(c, kSelectedOff))
                n++;
        });
    } else
        g_guest.mirrors.for_each([&](GEntry& e_) {
            GMember& m = e_.data;
            if (e_.ref.p && m.owner == me && F<uint8_t>(e_.ref.p, kSelectedOff))
                n++;
        });
    return n;
}

int give_selected(uint8_t to)
{
    if (!C.sess || !C.sess->connected() || game::mode_id() != modes::kTribe || to == C.sess->my_id())
        return 0;
    uint8_t me = C.sess->my_id();
    std::vector<uint16_t> ids;
    if (C.sess->role() == net::Role::Host) {
        g_host.members.for_each([&](HEntry& e_) {
            void* const c = e_.ref.p;
            HMember& hm = e_.data;
            if (hm.owner == me && F<uint8_t>(c, kSelectedOff))
                ids.push_back(e_.id);
        });
        give_on_host(me, to, ids);
    } else {
        g_guest.mirrors.for_each([&](GEntry& e_) {
            const uint16_t id = e_.id;
            GMember& m = e_.data;
            if (e_.ref.p && m.owner == me && F<uint8_t>(e_.ref.p, kSelectedOff))
                ids.push_back(id);
        });
        send_cmd(CMD_GIVE, ids, to, {}, {});
    }
    return (int)ids.size();
}

} // namespace tribe
