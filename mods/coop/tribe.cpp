#include "tribe.h"

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
#include "game.h"
#include "modes.h"
#include "npc.h"
#include "savestrip.h"
#include "space_ext.h"
#include "stage_epoch.h"
#include "net/ids.h"
#include "net/interp.h"
#include "net/validate.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "fields.h"

using skg::thiscall;
using skg::vcall;

namespace tribe {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_ok;

// ---- layout (docs/re/tribe.md) ----
constexpr uint32_t kTribeNoun = 0x18C6D19;
constexpr uintptr_t kMembersOff = 0x340;   // cTribe: vector<cCreatureCitizen*>
constexpr uintptr_t kTribePosOff = 0x124;  // cTribe spatial position
constexpr uintptr_t kOwnerTribeOff = 0x1014; // cCreatureCitizen::mpOwnerTribe
constexpr uintptr_t kAgeOff = 0xB34;       // 0 baby, 1 adult
constexpr uintptr_t kSelectedOff = 0x12C;  // spatial+0x6C IsSelected
static uintptr_t a_CitizenVt; // Simulator::cCreatureCitizen::vftable
constexpr uint32_t kTreeNoun = 0x6ABEE7E;    // cTreeData (planet flora: fruit trees), not a cGameData
static uintptr_t a_TreeVt;    // Simulator::cTreeData::vftable (main vtable)

static uintptr_t a_GetPlayerTribe, a_DoAction, a_SetValueVector3, a_BTTick, a_DoActionForObjects, a_WalkSelected,
    a_SetSelected, a_CreateCitizen, a_AddMember, a_SpawnNpcTribe, a_MaxPop;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
bool active() { return g_ok && C.sess && C.sess->connected() && game::mode_id() == modes::kTribe; }
// BUG-0151: while the host is away (main menu) nothing simulates or orders the guest's members (the
// host sends no states and drops orders outside Tribe): the guest's own game runs their AI and its
// orders until the host is back. `coop.tribe fix away 0`: the old frozen tribe (tests).
static bool g_fix_away = true;
COOP_FIX("tribe.away", "BUG-0151", g_fix_away, "tribe: while the host is away the guest's own game runs its members");
static uint32_t g_away_orders;
static bool local_play() { return g_fix_away && is_guest() && C.host_away && C.host_away(); }

static void* player_tribe()
{
    void* nm = game::noun_manager();
    return nm ? thiscall<void*>(a_GetPlayerTribe, nm) : nullptr;
}

static bool is_citizen(void* c) { return c && !IsBadReadPtr(c, 8) && *(uintptr_t*)c == a_CitizenVt; }

static std::vector<void*> members(void* t)
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

static std::vector<void*> all_tribes()
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

static net::Vec3 tribe_pos(void* t)
{
    auto& p = F<float[3]>(t, kTribePosOff);
    return {p[0], p[1], p[2]};
}

// ---- planet trees (docs/re/tribe.md, "Trees"): the flora object keeps a sorted vector of patches,
// each patch has three fixed_vectors of intrusive_ptr<cTreeData>; a tree's position is at +0x10.
// Gather orders on a fruit tree name a cTreeData, which is no cGameData: it is found by position.
static bool is_tree(void* t) { return t && !IsBadReadPtr(t, 0x20) && *(uintptr_t*)t == a_TreeVt; }

template <typename Fn>
static void for_each_tree(Fn fn)
{
    static uintptr_t get = H->sym("SporeCoop::Flora_Get");
    uint8_t* fl = get ? skg::cdecl_call<uint8_t*>(get) : nullptr;
    if (!fl || IsBadReadPtr(fl + 0x1E5F0, 8))
        return;
    uint8_t* b = F<uint8_t*>(fl, 0x1E5F0);
    uint8_t* e = F<uint8_t*>(fl, 0x1E5F4);
    constexpr size_t kPatch = 0x19A8;
    if (!b || e < b || (size_t)(e - b) % kPatch || (size_t)(e - b) / kPatch > 20000)
        return;
    for (uint8_t* el = b; el < e; el += kPatch) {
        uint8_t* rec = el + 8;
        for (uintptr_t vo : {0x44u, 0x9Cu, 0xF4u}) {
            void** tb = F<void**>(rec, vo);
            void** te = F<void**>(rec, vo + 4);
            if (!tb || te < tb || te - tb > 64 || IsBadReadPtr(tb, (te - tb) * 4))
                continue;
            for (void** it = tb; it < te; ++it)
                if (is_tree(*it))
                    fn(*it, net::Vec3{F<float>(*it, 0x10), F<float>(*it, 0x14), F<float>(*it, 0x18)});
        }
    }
}

static void* find_tree(const net::Vec3& pos, float max_dist)
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

// ---------------- host ----------------
struct HMember {
    uint16_t id;
    uint8_t owner;
    uint32_t bhv_anim = 0; // BUG-0569: the last animation its behaviour tree started
    void* tribe = nullptr; // BUG-0620: the tribe whose member list held it last
    double body_since = 0; // BUG-0620: when it left that list dead (its body), 0: in a list
};
// BUG-0620 (docs/re/tribe.md#tribe-member-body): a member that dies for good leaves its tribe's member list
// (+0x1014 = 0) while the game keeps its body (the dead flag +0xB5E, in the object list) for 10-20 s more; the host's
// snapshot dropped it at once and the guest removed its mirror: the body lay on the host only (the detector's
// `Tribe:unmatched:citizen`). With tribe.member_body the host sends the body with its last tribe (health 0: the guest's
// mirror plays the death, tribe.ko) while the game keeps it, at most kBodyMax s. Off: dropped at once (0.7.8).
static bool g_member_body_fix = true;
COOP_FIX("tribe.member_body", "BUG-0620", g_member_body_fix,
         "tribe: the body of a member that died lies in every player's game as long as in the host's");
constexpr double kBodyMax = 60.0;
constexpr uint8_t kBodyBit = 0x80; // in a snapshot entry's age byte: a body (the guest takes it out of its tribe too)
// guest: mirrors taken out of their tribe as bodies (destroyed on removal) -> the tribe they were taken out of
static std::unordered_map<void*, void*> g_detached;
static uintptr_t a_BTTerminate; // BUG-0667: SporeCoop::Citizen_BTTerminate (optional)
static uint32_t g_bodies_detached;
// BUG-0667 (docs/re/tribe.md#tribe-body-teardown): a detached body's mirror was destroyed (DestroyInstance) with its
// +0x1014 cleared. The citizen's teardown (0xC1FFE0) shuts its behaviour tree down, and the exit of a node still active
// there reads the tribe: the eat node 0x0461300F's exit 0xDB0C40 releases its food mat spot through
// cTribe_ReleaseSlot(citizen+0x1014 = null) and faulted in cTribe_ToolLayout's map lookup (0xAC9C7D, data 0x39C). A
// mirror's tree runs only while the host is away (local_play), so the node stayed active from then until the destroy
// (long_tribe on rel/0.7.9, the guest froze after the fault). The game's own RemoveMember destroys a member with its
// tribe still set. tribe.body_teardown: the body gets its tribe back right before it is destroyed (when that tribe
// still exists), and a slot release with no tribe is skipped. Off: destroyed with no tribe, no guard (0.7.9 before).
static bool g_body_teardown_fix = true;
COOP_FIX("tribe.body_teardown", "BUG-0667", g_body_teardown_fix,
         "tribe: a guest destroys a dead member's body with its tribe set (its behaviour tree's exits need it), never faults there");
static uint32_t g_teardown_restored, g_slot_null_skipped;
// guest: the mirror leaves its tribe's member and selectable lists as the host's body left its own (not selectable, not
// counted as a member), the object stays until the host's body goes
static void detach_body(void* c)
{
    void* t = F<void*>(c, 0x1014);
    if (!t || g_detached.count(c))
        return;
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
    if (g_body_teardown_fix && a_BTTerminate)
        skg::cdecl_call<void>(a_BTTerminate, c);
    F<void*>(c, 0x1014) = nullptr;
    g_detached[c] = t;
    g_bodies_detached++;
}
static uint32_t g_bodies_sent;
static std::unordered_map<void*, HMember> g_hm;
static std::unordered_map<uint16_t, void*> g_hm_by_id;
static uint16_t g_next_id = 1;
static uint32_t g_player_members_seen = 0;
static std::vector<uint8_t> g_split_for; // player ids the player tribe was split for
// BUG-0671 (below, h_BabyBorn)
using BabyBorn_t = void(SK_THISCALL*)(void*, float, float, float);
static BabyBorn_t o_BabyBorn;
static std::unordered_map<void*, uint8_t> g_born_for; // host: a new member made for a guest -> its owner
static double g_last_send;
static uint16_t g_cycle;
static uint32_t g_cmds_done, g_cmds_rejected;
static std::string g_last_cmd; // the last order (guest: sent, host: done or why it was rejected), tests
static std::string g_last_local; // the host's own last order (tests)
static uint32_t g_local_orders;
static uint32_t g_gifts;

// ---------------- guest ----------------
static float g_test_lift = 0.f; // tests (BUG-0329): `coop.tribe lift <m>` on a guest
struct GMember {
    void* c = nullptr;
    uint16_t id = 0; // the host's member id
    uint8_t tribe = 0, owner = 0xFE, age = 1;
    bool body = false; // BUG-0620: the host's member is a body out of its tribe's list
    net::Vec3 pos, vel;
    net::Quat rot;
    float hp = 0;
    double seen = 0;
    drive::State ds;
    net::SnapshotBuffer buf; // BUG-0365 (tribe.mirror_time): the host's states by its clock
    net::Vec3 aim;           // coop.track: where the driving aimed this frame
    float aim_speed = 0;     // and the host's speed of that state
    float walk_sp = 0;       // BUG-0619: the speed the smoothed target's mirror is walked at (eased in)
    double walk_t = 0;
    bool jittery = false;    // BUG-0619 (diagnostics)
    float lead_sp = -1;      // BUG-0619: the raw lead's speed while the smoothed target drives it (-1: none)
    double held_since = 0, held_said = -1e9; // BUG-0386 (diagnostics)
    double anim_rx_at = 0, anim_fix_at = 0; // BUG-0569: the last relayed start, the last snapshot play
};
static std::unordered_map<uint16_t, GMember> g_gm;
// BUG-0365 (see mirror_drive)
static bool g_mirror_walk_fix = true, g_mirror_time_fix = true;
COOP_FIX("tribe.mirror_walk", "BUG-0365", g_mirror_walk_fix, "tribe: a guest's mirror of a walking member walks on the host's path instead of stop-and-go and surges");
COOP_FIX("tribe.mirror_time", "BUG-0365", g_mirror_time_fix, "tribe: a guest's mirrors follow the host's states by its clock (an interpolation buffer), not by their arrival");
static uint32_t g_mirror_walks, g_mirror_timed;
// BUG-0444: the mirrors kept their own distance from the planet's centre (own_height, BUG-0329) and were never
// put onto this game's ground: that is the game's own locomotion, and a guest's game does not update its
// citizens for 20-40 s after a cold join (game time paused, no BehaviorTreeTick) or while it is paused. A member
// the host walked up a slope then was placed on the host's path at its old height, deeper and deeper under the
// guest's ground (2.6-3.3 m for ~20 s in tribe_coop's hunt; 9 m in 60 m up a hill, tribe_mirror_ground).
// tribe.mirror_ground: the ground policy of the animal proxies (npc.proxy_ground, BUG-0399, driver.h): a new
// mirror is made on this game's ground when the host's position is under it, a walking one is placed onto it
// (WalkPlace::ground, and out_of_ground for a mirror deeper than its 3 m), a standing one found more than 0.25 m
// under it is teleported onto it (drive::step's ground). `coop.fix tribe.mirror_ground off`: own height only.
static bool g_mirror_ground_fix = true;
COOP_FIX("tribe.mirror_ground", "BUG-0444", g_mirror_ground_fix, "tribe: a guest's mirrors of members are made and kept on this game's ground, never left under it (as the animal proxies)");
static uint32_t g_mirror_grounded; // diagnostics (coop.tribe): standing mirrors put out of the ground
constexpr double kMirrorDelay = 0.2;  // s: the interpolation delay (the host sends every 0.12 s or more)
// tests (BUG-0481): `coop.tribe delay <s>` on a guest: a planted late mirror (play_tribe's sync check must fail)
static double g_test_delay = kMirrorDelay;
constexpr float kMirrorWalkMax = 8.f; // farther off: drive::step (it teleports at 8 m)
// BUG-0619: a member's lead jump up to this is smoothed (a start known late over a mobile network: 2.9-4 m at 9 m/s;
// the lead's cap kMaxLead x 9 m/s = 5.4 m)
constexpr float kMirrorSmoothMax = 6.f; // m
constexpr float kMirrorWalkAcc = 40.f; // m/s2 (BUG-0619)
constexpr float kMirrorWalkOver = 3.f; // m/s: walked at most this faster than the lead itself (BUG-0619)
constexpr double kPathDt = 0.1;       // s: the interpolated path's velocity is taken over this
static std::unordered_map<void*, uint16_t> g_gid_of;
static std::unordered_set<uint16_t> g_cycle_ids;
static net::CycleTracker g_cyc;                    // the cycle being received and its chunks (BUG-0612)
static uint32_t g_incomplete;                      // cycles whose cleanup was skipped (a chunk missing)
// BUG-0612 (the older-cycle part of BUG-0425's umbrella): a late chunk of an older snapshot cycle started that cycle
// again (the newer cycle's chunks forgotten, older states over newer ones). Off: 0.7.8 (any other cycle restarts).
bool g_cycle_order_fix = true;
COOP_FIX("net.cycle_order", "BUG-0612", g_cycle_order_fix, "net: a late chunk of an older Tribe / Civ snapshot cycle is dropped, not taken as a new cycle");
static uint32_t g_spawned, g_removed, g_cmds_sent;
static std::vector<void*> g_local_tribe_of; // host tribe index -> local tribe
// BUG-0672 (tribe.world_map): the mapping outlived the world it was made in. After a load (a warm reset, a received
// world) the guest's wobj tool kind made the host's tool in tribe index i before the new world's first tribe state had
// mapped it again: tribe_of_index gave the old world's freed tribe (its species +0x18CC already released), and the
// tool's SetPosition (0xC9DB80) re-laid that tribe's spots (0xC98940 type 6), which read the species' size: the fault
// at 0xCE6AE7 (data 0x554). On: the mapping is dropped with the world and an entry is used only while it is one of
// this world's tribes. Off: 0.7.9.
static bool g_world_map_fix = true;
COOP_FIX("tribe.world_map", "BUG-0672", g_world_map_fix,
         "tribe: right after a world load the guest never builds the host's tool buildings into a tribe of the old world");
static uint32_t g_map_stale_skipped;
static bool g_state_hold; // tests (BUG-0672): the guest ignores the host's tribe states (the mapping is not renewed)

uint8_t owner_of(void* c)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        auto it = g_hm.find(c);
        return it == g_hm.end() ? 0xFF : it->second.owner;
    }
    auto it = g_gid_of.find(c);
    if (it == g_gid_of.end())
        return 0xFF;
    return g_gm[it->second].owner;
}

// ---------------- hooks ----------------
using BTTick_t = int(SK_CDECL*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static BTTick_t o_BTTick;
static uint32_t g_bt_skipped, g_bt_calls; // diagnostics (coop.tribe): mirrors' AI ticks skipped / all ticks
// tests (BUG-0667): `coop.tribe localbt <member id> 1|0` on a guest: that mirror's tree runs as while the host is away
static std::unordered_set<void*> g_bt_local;
// BUG-0667: cTribe_ReleaseSlot (a behaviour node's exit giving a layout spot back) with no tribe is skipped
using ReleaseSlot_t = void(SK_THISCALL*)(void*, int, int);
static ReleaseSlot_t o_ReleaseSlot;
static void SK_THISCALL h_ReleaseSlot(void* t, int type, int slot)
{
    if (!t && g_body_teardown_fix) {
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
static void SK_THISCALL h_SetTribe(void* c, void* t)
{
    if (g_body_teardown_fix && is_guest()) {
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
                auto gi = g_gid_of.find(c);
                H->log(M, SK_INFO, "tribe: SetTribe(null) on %p (mirror #%d, old tribe %p, dead %u, node %08X); stack:%s (BUG-0667)", c,
                       gi == g_gid_of.end() ? -1 : (int)gi->second, old, F<uint8_t>(c, 0xB5E),
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
    if (g_body_teardown_fix && !F<void*>(c, 0x1014) && (is_guest() || g_left_tribe.count(c) || g_detached.count(c))) {
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
    if (g_body_teardown_fix && g_detached.count((void*)self)) { // BUG-0667: a detached body's tree stays ended
        g_bt_skipped++;
        return 0;
    }
    if (is_guest() && is_citizen((void*)self) && game::mode_id() == modes::kTribe && !local_play() &&
        !g_bt_local.count((void*)self)) {
        g_bt_skipped++;
        return 0;
    }
    return o_BTTick(a0, a1, a2, a3, self, a5, a6, a7, a8);
}

// a player may select only their own members of the player tribe
using SetSelected_t = int(SK_THISCALL*)(void*, int);
static SetSelected_t o_SetSelected;
static int SK_THISCALL h_SetSelected(void* spatial, int sel)
{
    if ((sel & 0xFF) && active()) {
        void* c = (uint8_t*)spatial - 0xC0;
        uint8_t owner = owner_of(c);
        if (owner != 0xFF && owner != C.sess->my_id())
            sel = 0;
    }
    return o_SetSelected(spatial, sel);
}

struct Target {
    uint8_t kind = 0; // 0 none, 1 citizen id, 2 object by noun + position, 3 tree by position
    uint16_t id = 0;
    uint32_t noun = 0;
    net::Vec3 pos;
};

static Target encode_target(void* t)
{
    Target r;
    if (!t)
        return r;
    auto it = g_gid_of.find(t);
    if (it != g_gid_of.end()) {
        r.kind = 1;
        r.id = it->second;
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

static void write_target(net::Writer& w, const Target& t)
{
    w.u8(t.kind);
    w.u16(t.id);
    w.u32(t.noun);
    w.f32(t.pos.x); w.f32(t.pos.y); w.f32(t.pos.z);
}

static Target read_target(net::Reader& r)
{
    Target t;
    t.kind = r.u8();
    t.id = r.u16();
    t.noun = r.u32();
    t.pos.x = r.f32(); t.pos.y = r.f32(); t.pos.z = r.f32();
    return t;
}

enum : uint8_t { CMD_ACTION = 0, CMD_WALK = 1, CMD_GIVE = 2, CMD_BABY = 3 };

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
        auto f = g_gid_of.find((uint8_t*)*it - 0xC0);
        if (f != g_gid_of.end())
            ids.push_back(f->second);
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
// `coop.fix tribe.target_ring off`: no ring on the guest (0.7.6).
static bool g_ring_fix = true;
COOP_FIX("tribe.target_ring", "BUG-0570", g_ring_fix, "tribe: the guest sees the red circle under the enemy it ordered an attack on");
static constexpr int kActionAttack = 7; // DoActionForAllSelected(7, target) of a right click on an enemy
static struct {
    void* target = nullptr;
    std::vector<uint16_t> ids;
    double since = 0;
} g_ring;
static uint32_t g_rings_drawn = 0;
static uintptr_t a_RingDraw, a_TribeUI;
using RingsUpdate_t = void(SK_THISCALL*)(void*);
static RingsUpdate_t o_RingsUpdate;

static void ring_order(int action, void* target, const std::vector<uint16_t>& ids)
{
    if (action == kActionAttack && target && !ids.empty() && (is_citizen(target) || game::noun_id(target) == 0x18EB45E)) {
        g_ring.target = target;
        g_ring.ids = ids;
        g_ring.since = H->time_seconds();
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
    if (!t || !g_ring_fix || !a_RingDraw || !is_guest() || game::mode_id() != modes::kTribe)
        return;
    if (H->time_seconds() - g_ring.since > 120 || !game::object_alive(t) || game::is_destroyed(t) || game::is_dead(t) ||
        F<float>(t, 0x5E0) <= 0) {
        g_ring.target = nullptr;
        return;
    }
    void* by = nullptr;
    for (uint16_t id : g_ring.ids) {
        auto it = g_gm.find(id);
        if (it != g_gm.end() && it->second.c && F<uint8_t>(it->second.c, kSelectedOff)) {
            by = it->second.c;
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
static bool ring_shown(void* t)
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
    for (auto& [id, m] : g_gm)
        if (m.c && F<uint8_t>(m.c, kSelectedOff) && m.owner == C.sess->my_id())
            ids.push_back(id);
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
// the guest sets the host member's scale on its mirror. `coop.fix tribe.scale off`: own sizes (0.7.3.6).
static bool g_scale_fix = true;
COOP_FIX("tribe.scale", "BUG-0475", g_scale_fix, "tribe: a guest's mirror of a tribe member gets the host member's size");
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
        for (void* c : members(t)) {
            auto it = g_hm.find(c);
            if (it != g_hm.end())
                cb(it->second.id, c, u);
        }
}

static void* member_mirror(uint16_t id)
{
    auto it = g_gm.find(id);
    return it != g_gm.end() && it->second.c && game::object_alive(it->second.c) ? it->second.c : nullptr;
}

// ---- tools (BUG-0514, docs/re/tribe.md#tribe-tools-guest) ----
// A member's tool (+0x102C mSpecializedTool: 1..9 the racks' weapons and instruments, 9 heal, 0xB the chief's
// staff) is its own game's: the host's game runs the guest's members (a GrabTool order of a guest is done
// there), the guest's mirror never took the tool, so the guest saw its members walk to the rack and come
// back empty-handed (no weapon in hand, no heal order: InputAction 0x28 takes only members with tool 9).
// The held tool is a replicated field of the members (its own kind: its own switch); the guest sets it on
// its mirror with the game's SetSpecializedTool. `coop.fix tribe.tool_hold off`: mirrors keep their own.
constexpr uintptr_t kToolsOff = 0x36C;    // cTribe: vector<cTribeTool*> mTools
constexpr uintptr_t kHeldToolOff = 0x102C; // cCreatureCitizen: mSpecializedTool
static uintptr_t a_SetChieftain; // BUG-0652
static uint16_t member_id(void* c);
static uintptr_t a_SetSpecializedTool, a_ToolModelReset, a_SetToolPurchased, a_ToolLayout, a_IsLeader;
static bool g_tool_hold_fix = true;
COOP_FIX("tribe.tool_hold", "BUG-0514", g_tool_hold_fix, "tribe: a guest's mirror of a member holds the tool the host's member took (weapon, instrument, heal)");
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
static uint32_t g_tool_sets, g_leader_tools_kept;
static bool is_leader(void* c) { return a_IsLeader && is_citizen(c) && thiscall<bool>(a_IsLeader, c); }
static float read_held_tool(void* c) { return is_citizen(c) ? (float)F<int>(c, kHeldToolOff) : 0.f; }
static void apply_held_tool(void* c, float v)
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
    void* old = F<void*>(tribe, 0x11C);
    thiscall<void>(a_SetChieftain, tribe, c);
    if (g_leaders_set++ < 5)
        H->log(M, SK_INFO, "tribe: member #%u leads its tribe here too, as in the host's game (the leader was %s, BUG-0652)",
               member_id(c), old ? ("#" + std::to_string(member_id(old))).c_str() : "none");
}
static const fields::Field kHeldTool{fields::F_TOOL, "tool", read_held_tool, apply_held_tool, 0.01f};
static const fields::Field kToolFields[] = {kHeldTool};

static int tool_type(void* tool) { return vcall<int>(tool, 0x58 / 4); }

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
static net::Quat upright(const net::Vec3& p)
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

// ---- every tribe's tools (BUG-0523 / BUG-0524, docs/re/tribe.md#tribe-tools-all) ----
constexpr uintptr_t kToolDragOff = 0x34 + 0x72; // the tool's spatial (+0x34) +0x72: the palette is dragging it
static std::vector<void*> tribe_tools(void* t)
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
static void* g_test_dragged; // tests: the tool `coop.tribe buy` drags

// this game's tribe of the host's index i (the host: its own list; a guest: the mapping of the last tribe state)
static void* tribe_of_index(int i)
{
    if (i < 0)
        return nullptr;
    if (is_host()) {
        auto ts = all_tribes();
        return i < (int)ts.size() && i < 32 ? ts[i] : nullptr;
    }
    void* t = i < (int)g_local_tribe_of.size() ? g_local_tribe_of[i] : nullptr;
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
            for (size_t i = 0; i < g_local_tribe_of.size(); i++)
                if (g_local_tribe_of[i] == t) {
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

static void* make_tool_in(void* t, int type, const net::Vec3& pos)
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

static uint32_t g_tools_bought;
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
// BUG-0531: the food a member carries (its cGameBundleContainer at c+0x38: bundle vector +4..+8, amount at
// bundle +0x134), what the gather node's 0xCEE4E0 sums
static float carried(void* c)
{
    void** b = F<void**>(c, 0x3C);
    void** e = F<void**>(c, 0x40);
    float sum = 0;
    if (!b || e < b || e - b > 32 || !skg::readable(b, (e - b) * 4))
        return 0;
    for (void** it = b; it < e; ++it)
        if (*it && skg::readable(*it, 0x138))
            sum += *(float*)((uint8_t*)*it + 0x134);
    return sum;
}
static std::string ai_line(void* c)
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
static void* SK_THISCALL h_Form(void* self, int arg); // BUG-0483 (below)
using Form_t = void*(SK_THISCALL*)(void*, int);
static Form_t o_Form;
static uintptr_t a_Form, a_NpcCtrl;
static void theme_hooks_install(); // BUG-0568 (the Tribe theme music relay, below)
static void party_hooks_install(); // BUG-0646 (the fire pit party music relay, below)
static void party_forget();
static void SK_THISCALL h_BabyBorn(void*, float, float, float); // BUG-0671 (below)
static void baby_tick(double now);
static void baby_forget();
static void save_start();
static void save_end();

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    fields::add_kind({fields::K_TRIBE_MEMBER, "tribe_member", &g_scale_fix, kMemberFields, 2, member_each, member_mirror,
                      [] { return active(); }});
    fields::add_kind({fields::K_TRIBE_TOOL, "tribe_member_tool", &g_tool_hold_fix, kToolFields, 1, member_each, member_mirror,
                      [] { return active(); }});
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
    a_TribeUI = H->sym("SporeCoop::TribeUI_ptr");
    if (uintptr_t a = H->sym("SporeCoop::TribeTargetRings_Update"))
        H->hook_create(M, (void*)a, (void*)h_RingsUpdate, (void**)&o_RingsUpdate);
    a_BTTerminate = H->sym("SporeCoop::Citizen_BTTerminate"); // BUG-0667 (optional)
    if (uintptr_t a = H->sym("SporeCoop::cTribe_ReleaseSlot")) // BUG-0667 (optional)
        H->hook_create(M, (void*)a, (void*)h_ReleaseSlot, (void**)&o_ReleaseSlot);
    if (uintptr_t a = H->sym("Simulator::cCreatureCitizen::SetTribe")) // BUG-0667 (optional)
        H->hook_create(M, (void*)a, (void*)h_SetTribe, (void**)&o_SetTribe);
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
    coop_init();
    savestrip::add_filter(save_filter); // BUG-0495
    savestrip::add_save_hooks(save_start, save_end);
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
// chief that stands is put on its spot now (as spread_chiefs). Off: 0.7.8 (walking chiefs too).
static bool g_place_still_fix = true;
COOP_FIX("tribe.place_still", "BUG-0610", g_place_still_fix, "tribe: the arrival's chief placing leaves a walking chief alone (it was teleported up to 22 m)");
static uint32_t g_place_walking = 0; // chiefs left alone because they walked
static double g_place_due = 0, g_place_until = 0;
static uint32_t g_placed;
static std::unordered_set<void*> g_placed_now; // chiefs placed in this arrival's window
static std::unordered_map<void*, net::Vec3> g_place_first; // BUG-0610: where each chief stood at its first look
static void arrival_place_due(double now)
{
    g_placed_now.clear();
    g_place_first.clear();
    g_place_due = now + 3.0;   // the split and the chiefs settle first
    g_place_until = now + 60.0; // a player still loading: the guests' games come in later
}
constexpr size_t kEntry = 2 + 1 + 1 + 1 + 12 + 12 + 16 + 4; // one member in a tribe state chunk
constexpr uint32_t kStateTrailer = 0x54435354u; // BUG-0365: a chunk's trailer, magic + the host's ms
constexpr size_t kTrailerSize = 8;

// BUG-0381: an NPC tribe the host's game formed during play (cNpcTribeController, 0xCC6440: "a new
// tribe has appeared") is formed by the host's game only: the guests' games never formed it (2 of 5
// tribes on every guest for minutes in a 4-player campaign) and the host's members of it had no
// tribe to be mirrored into on a guest (sync-check "t: 27 missing on the guest"). The host appends
// each tribe's species and archetype to its state chunks (after the clock trailer); a guest makes a
// host NPC tribe it has no tribe for (Simulator::SpawnNpcTribe at the host's tribe position, no
// members: the mirrors are spawned into it as for any tribe). Off: the guest has only the tribes its
// own game formed.
static bool g_npc_tribes_fix = true;
COOP_FIX("tribe.npc_tribes", "BUG-0381", g_npc_tribes_fix,
         "tribe: a guest makes the host's NPC tribes its own game did not form (their members were missing)");
constexpr uint32_t kTribeInfoTag = 0x49425254u; // "TRBI": the tribe table's species / archetype after the trailer
constexpr size_t kTribeInfo = 12 + 1;           // species key (instance, type, group) + archetype (0xFF: none)
constexpr uintptr_t kTribeArchetypeOff = 0x550; // cTribe: the archetype SpawnNpcTribe stores (< 0x20)
static uint32_t g_npc_tribes_made, g_npc_tribes_failed;
struct TribeInfo {
    game::Key key{};
    uint8_t arch = 0xFF;
};
static TribeInfo tribe_info(void* t)
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
// BUG-0569: a guest saw its village's members stand still while the host's fought, gathered, danced and built.
// The behaviour tree of a guest's mirror does not run (h_BTTick: the host's member decides), and the animations of
// the members' actions are started by those trees (SporeCoop::CreaturePlayAnim: fights, gathering, the grab, the
// social dances; game.cpp's behaviour animation listener). The mirrors played only what their locomotion picks
// (idle, walk). As for the animals (npc.anims, BUG-0464): the host reports every behaviour animation of a member
// it tracks and sends them once a cycle to the guests (MSG_NPC_ANIM kind 2: u8 n, n x {u16 member id, u32 anim, u8 how,
// u8 a, u32 b}); once a second the animation each member plays now when its tree started it (kind 3: u8 n, n x
// {u16 id, u32 anim}) for a mirror made later or a lost message. A guest plays the same call on the mirror.
// Older guests ignore both kinds (npc.cpp reads kinds 0 and 1 only). `coop.fix tribe.mirror_anims off`: nothing
// sent, nothing played (0.7.7).
static bool g_mirror_anims_fix = true;
COOP_FIX("tribe.mirror_anims", "BUG-0569", g_mirror_anims_fix,
         "tribe: a guest sees the members of every village fight, gather, dance and build as on the host");
struct MemberAnim {
    uint16_t id;
    uint32_t anim;
    uint8_t how, a;
    int32_t b;
};
static std::vector<MemberAnim> g_member_anim_out;
static uint32_t g_anim_relay_sent, g_anim_relay_played, g_anim_relay_failed, g_anim_relay_rx, g_anim_snap_played;
static double g_member_snap_at;

void on_behaviour_anim(void* c, uint32_t anim, int how, int a, int b)
{
    if (!g_ok || !is_host() || game::mode_id() != modes::kTribe)
        return;
    auto it = g_hm.find(c);
    if (it == g_hm.end())
        return;
    it->second.bhv_anim = anim; // (also with the fix off: `coop.tribe anims` shows it to the tests)
    if (g_mirror_anims_fix && g_member_anim_out.size() < 256)
        g_member_anim_out.push_back({it->second.id, anim, (uint8_t)how, (uint8_t)(a & 0xFF), (int32_t)b});
}

static void send_member_anims(double now)
{
    if (!g_mirror_anims_fix) {
        g_member_anim_out.clear();
        return;
    }
    const size_t per = (net::kMaxPacket - 64) / 12;
    for (size_t i = 0; i < g_member_anim_out.size(); i += per) {
        size_t n = std::min(per, g_member_anim_out.size() - i);
        net::Writer w;
        w.u8(2); // started now
        w.u8((uint8_t)n);
        for (size_t j = 0; j < n; j++) {
            const MemberAnim& e = g_member_anim_out[i + j];
            w.u16(e.id); w.u32(e.anim); w.u8(e.how); w.u8(e.a); w.u32((uint32_t)e.b);
        }
        for (auto& p : C.sess->players())
            if (p.id != 0)
                C.sess->send_unreliable_to(p.id, net::MSG_NPC_ANIM, w);
        g_anim_relay_sent += (uint32_t)n;
    }
    g_member_anim_out.clear();
    if (now - g_member_snap_at < 1.0)
        return;
    g_member_snap_at = now;
    std::vector<std::pair<uint16_t, uint32_t>> v;
    for (auto& [c, m] : g_hm) {
        uint32_t cur = 0;
        if (m.bhv_anim && game::object_alive(c) && game::drawn_state(c, nullptr, &cur) && cur && cur == m.bhv_anim)
            v.push_back({m.id, cur});
    }
    const size_t per2 = (net::kMaxPacket - 64) / 6;
    for (size_t i = 0; i < v.size(); i += per2) {
        size_t n = std::min(per2, v.size() - i);
        net::Writer w;
        w.u8(3); // what plays now
        w.u8((uint8_t)n);
        for (size_t j = 0; j < n; j++) {
            w.u16(v[i + j].first);
            w.u32(v[i + j].second);
        }
        for (auto& p : C.sess->players())
            if (p.id != 0)
                C.sess->send_unreliable_to(p.id, net::MSG_NPC_ANIM, w);
    }
}

// BUG-0568: "no music in the guest's Tribe game". The Tribe stage's music of the action (the hunt / fight theme,
// procedural sound 0xAD1A17D, and the other themes of the behaviours 0xDB1060 / 0xDB7120 / 0xDBAAF0 / 0xDBB7D0) is
// started by the player tribe's members' behaviour trees through the stage's theme music manager (TribeStage +0x1C8,
// 24 slots of {track, distance}: SporeCoop::TribeTheme_Play / _Move / _Stop). A guest's members are mirrors without a
// behaviour tree (h_BTTick): its game never started one (lab: the host's game 7 plays of 0xAD1A17D in a 40 s fight, the
// guest's none, out/lab/20261105-115203). The stage's own sounds (ambience, the mode's group) were the same on both.
// The host relays its theme plays, their moves (at most every 0.3 s a slot) and stops to the guests (MSG_NPC_ANIM kind 4
// {u8 host slot, u32 sound, vec3}, kind 6 {u8 slot} reliable, kind 5 {u8 slot, vec3} unreliable); a guest plays them on its
// own theme manager (the game's call: the guest's own volume, position and music settings). Older guests ignore the
// kinds. `coop.fix tribe.theme_music off`: nothing sent or played (0.7.7).
static bool g_theme_fix = true;
COOP_FIX("tribe.theme_music", "BUG-0568", g_theme_fix, "tribe: a guest hears the stage's hunt and fight music of its tribe as the host does");
using ThemePlay_t = int(SK_THISCALL*)(void*, uint32_t, float, float, float);
using ThemeMove_t = void(SK_THISCALL*)(void*, int, float, float, float);
using ThemeStop_t = void(SK_THISCALL*)(void*, int);
static ThemePlay_t o_ThemePlay;
static ThemeMove_t o_ThemeMove;
static ThemeStop_t o_ThemeStop;
static bool g_theme_relaying; // guest: our own call of the game's functions
static double g_theme_moved_at[24];
static int g_theme_local[24]; // guest: host slot -> this game's slot (0 none)
static uint32_t g_theme_sent, g_theme_rx, g_theme_played;

static void theme_send(uint8_t kind, const net::Writer& w)
{
    for (auto& p : C.sess->players())
        if (p.id != 0) {
            if (kind == 5)
                C.sess->send_unreliable_to(p.id, net::MSG_NPC_ANIM, w);
            else
                C.sess->send_reliable_to(p.id, net::MSG_NPC_ANIM, w);
        }
    g_theme_sent++;
}
static bool theme_host() { return g_ok && g_theme_fix && C.sess && is_host() && game::mode_id() == modes::kTribe; }
static int SK_THISCALL h_ThemePlay(void* mgr, uint32_t sound, float x, float y, float z)
{
    int slot = o_ThemePlay(mgr, sound, x, y, z);
    if (slot > 0 && slot < 24 && !g_theme_relaying && theme_host()) {
        net::Writer w;
        w.u8(4); w.u8((uint8_t)slot); w.u32(sound); w.f32(x); w.f32(y); w.f32(z);
        theme_send(4, w);
        g_theme_moved_at[slot] = H->time_seconds();
    }
    return slot;
}
static void SK_THISCALL h_ThemeMove(void* mgr, int slot, float x, float y, float z)
{
    o_ThemeMove(mgr, slot, x, y, z);
    if (slot > 0 && slot < 24 && !g_theme_relaying && theme_host()) {
        double now = H->time_seconds();
        if (now - g_theme_moved_at[slot] < 0.3)
            return;
        g_theme_moved_at[slot] = now;
        net::Writer w;
        w.u8(5); w.u8((uint8_t)slot); w.f32(x); w.f32(y); w.f32(z);
        theme_send(5, w);
    }
}
static void SK_THISCALL h_ThemeStop(void* mgr, int slot)
{
    o_ThemeStop(mgr, slot);
    if (slot > 0 && slot < 24 && !g_theme_relaying && theme_host()) {
        net::Writer w;
        w.u8(6); w.u8((uint8_t)slot);
        theme_send(6, w);
    }
}
static void theme_hooks_install()
{
    uintptr_t p = H->sym("SporeCoop::TribeTheme_Play"), m = H->sym("SporeCoop::TribeTheme_Move"),
              st = H->sym("SporeCoop::TribeTheme_Stop");
    if (!p || !m || !st)
        return;
    H->hook_create(M, (void*)p, (void*)h_ThemePlay, (void**)&o_ThemePlay);
    H->hook_create(M, (void*)m, (void*)h_ThemeMove, (void**)&o_ThemeMove);
    H->hook_create(M, (void*)st, (void*)h_ThemeStop, (void**)&o_ThemeStop);
}
static void* theme_mgr()
{
    static uintptr_t a_Stage = H->sym("SporeCoop::cTribeUI_Get");
    void* stage = a_Stage ? skg::cdecl_call<void*>(a_Stage) : nullptr;
    return stage && skg::readable((uint8_t*)stage + 0x1C8, 4) ? F<void*>(stage, 0x1C8) : nullptr;
}
static void theme_forget() { memset(g_theme_local, 0, sizeof g_theme_local); }
static void on_theme(uint8_t kind, net::Reader& r)
{
    uint8_t slot = r.u8();
    float x = 0, y = 0, z = 0;
    uint32_t sound = kind == 4 ? r.u32() : 0;
    if (kind != 6) {
        x = r.f32(); y = r.f32(); z = r.f32();
    }
    if (!r.ok() || slot == 0 || slot >= 24)
        return;
    g_theme_rx++;
    void* mgr = theme_mgr();
    if (!g_theme_fix || !mgr || !o_ThemePlay)
        return;
    g_theme_relaying = true;
    int& local = g_theme_local[slot];
    if (kind == 4) {
        if (local)
            o_ThemeStop(mgr, local);
        local = o_ThemePlay(mgr, sound, x, y, z);
        if (local)
            g_theme_played++;
    } else if (kind == 5 && local) {
        o_ThemeMove(mgr, local, x, y, z);
    } else if (kind == 6 && local) {
        o_ThemeStop(mgr, local);
        local = 0;
    }
    g_theme_relaying = false;
}

// BUG-0646: "the guest does not hear the music of the dance around the fire". The fire pit party (CitizenAction 12 Party
// on the own fire pit, the game behaviour cTribeGameBehaviorParty, vtable 0x147BEB0) plays its music straight through
// Audio::PlayProceduralAudio(0x19DC06DF, a new track, the fire pit's position) from its tick 0xD793F0 once every
// partying member stands at its slot, and stops it (Audio::StopAudio) in its exit 0xD78F80; not through the theme
// manager, so the BUG-0568 relay never saw it. The behaviour runs on the host's game only (a guest's party order goes to
// the host; its members are mirrors): lab 20261107-102952, the host's game played 0x19DC06DF (caller 0xD79E81), the
// guest's none. The host relays the play and the stop (MSG_NPC_ANIM kind 7 {u32 host track, u32 sound, vec3}, kind 8
// {u32 host track}, reliable); a guest plays the same sound on a track of its own (the game's call: its own volume and
// settings) unless its own game plays the party music itself. Older guests ignore the kinds.
// `coop.fix tribe.party_music off`: nothing sent or played.
static bool g_party_fix = true;
COOP_FIX("tribe.party_music", "BUG-0646", g_party_fix, "tribe: a guest hears the music of the dance at its tribe's fire pit as the host does");
static constexpr uint32_t kPartyMusic = 0x19DC06DF;
using PlayProc_t = void(SK_CDECL*)(uint32_t, uint32_t, float, float, float);
using StopAudio_t = void(SK_CDECL*)(uint32_t, int);
static PlayProc_t o_PlayProc;
static StopAudio_t o_StopAudio;
static uintptr_t a_AudioGet;
static bool g_party_relaying;            // guest: our own call of the game's functions
static uint32_t g_party_host_track;      // host: the party music's track now (0 none)
static uint32_t g_party_rx_track, g_party_local; // guest: the host's track and this game's track for it
static uint32_t g_party_own;             // guest: a party music track its own game plays
static uint32_t g_party_sent, g_party_rx, g_party_played, g_party_stopped;

static bool party_host() { return g_ok && g_party_fix && C.sess && is_host() && game::mode_id() == modes::kTribe; }
static void party_send(uint8_t kind, uint32_t track, uint32_t sound, float x, float y, float z)
{
    net::Writer w;
    w.u8(kind); w.u32(track);
    if (kind == 7) {
        w.u32(sound); w.f32(x); w.f32(y); w.f32(z);
    }
    for (auto& p : C.sess->players())
        if (p.id != 0)
            C.sess->send_reliable_to(p.id, net::MSG_NPC_ANIM, w);
    g_party_sent++;
}
static void SK_CDECL h_PlayProc(uint32_t sound, uint32_t track, float x, float y, float z)
{
    o_PlayProc(sound, track, x, y, z);
    if (sound != kPartyMusic || g_party_relaying || !g_ok)
        return;
    if (party_host()) {
        g_party_host_track = track;
        party_send(7, track, sound, x, y, z);
    } else if (is_guest()) {
        g_party_own = track;
    }
}
static void SK_CDECL h_StopAudio(uint32_t track, int how)
{
    o_StopAudio(track, how);
    if (!track || g_party_relaying || !g_ok)
        return;
    if (track == g_party_host_track) {
        g_party_host_track = 0;
        if (party_host())
            party_send(8, track, 0, 0, 0, 0);
    } else if (track == g_party_own) {
        g_party_own = 0;
    }
}
static void party_hooks_install()
{
    uintptr_t p = H->sym("Audio::PlayProceduralAudio"), s = H->sym("Audio::StopAudio");
    a_AudioGet = H->sym("Audio::AudioSystem::Get");
    if (!p || !s || !a_AudioGet)
        return;
    H->hook_create(M, (void*)p, (void*)h_PlayProc, (void**)&o_PlayProc);
    H->hook_create(M, (void*)s, (void*)h_StopAudio, (void**)&o_StopAudio);
}
static void party_stop_local()
{
    if (g_party_local && o_StopAudio) {
        g_party_relaying = true;
        o_StopAudio(g_party_local, 0);
        g_party_relaying = false;
        g_party_stopped++;
    }
    g_party_local = g_party_rx_track = 0;
}
static void party_forget()
{
    party_stop_local(); // (the audio system outlives the world: a track left playing would play on)
    g_party_host_track = g_party_own = 0;
}
static void on_party(uint8_t kind, net::Reader& r)
{
    uint32_t track = r.u32(), sound = 0;
    float x = 0, y = 0, z = 0;
    if (kind == 7) {
        sound = r.u32(); x = r.f32(); y = r.f32(); z = r.f32();
    }
    if (!r.ok() || !track)
        return;
    g_party_rx++;
    if (!g_party_fix || !o_PlayProc)
        return;
    if (kind == 8) {
        if (track == g_party_rx_track)
            party_stop_local();
        return;
    }
    if (sound != kPartyMusic || g_party_own) // only the party music; its own game plays it already
        return;
    party_stop_local();
    void* as = skg::cdecl_call<void*>(a_AudioGet);
    if (!as)
        return;
    uint32_t mine = thiscall<uint32_t>(F<uintptr_t>(F<void*>(as, 0), 0x20), as); // a new track (AudioSystem vt 0x20)
    if (!mine)
        return;
    g_party_relaying = true;
    o_PlayProc(sound, mine, x, y, z);
    g_party_relaying = false;
    g_party_local = mine;
    g_party_rx_track = track;
    g_party_played++;
}

// guest: MSG_NPC_ANIM kinds 2 / 3 (npc.cpp passes them on after the kind byte)
void on_member_anims(uint8_t kind, net::Reader& r)
{
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
        g_anim_relay_rx++;
        if (!g_mirror_anims_fix)
            continue;
        auto it = g_gm.find(id);
        void* c = it == g_gm.end() ? nullptr : it->second.c;
        if (!c || !game::object_alive(c) || game::is_dead(c))
            continue;
        GMember& g = it->second;
        if (kind == 3) { // only when the mirror plays another one, at most every 2 s, not right after a start
            uint32_t cur = 0;
            if (now - g.anim_fix_at < 2.0 || now - g.anim_rx_at < 1.5 || !game::drawn_state(c, nullptr, &cur) || cur == anim)
                continue;
            g.anim_fix_at = now;
            if (game::play_behaviour_anim(c, anim, 0, 1, -1))
                g_anim_snap_played++;
            continue;
        }
        g.anim_rx_at = now;
        if (game::play_behaviour_anim(c, anim, how, a, b))
            g_anim_relay_played++;
        else
            g_anim_relay_failed++;
    }
}

static void host_tick(double now)
{
    if (now - g_last_send < 0.12)
        return;
    g_last_send = now;
    baby_tick(now); // BUG-0671
    void* pt = player_tribe();
    auto tribes = all_tribes();
    std::vector<uint8_t> players;
    for (auto& p : C.sess->players())
        players.push_back(p.id);
    std::sort(players.begin(), players.end());
    if (players != g_split_for && pt) {
        arrival_place_due(now); // BUG-0340: a player arrived or rejoined
        // the players changed: split the player tribe again, in member order
        g_split_for = players;
        g_player_members_seen = 0;
        for (void* c : members(pt)) {
            auto it = g_hm.find(c);
            if (it != g_hm.end())
                it->second.owner = players[g_player_members_seen++ % players.size()];
        }
    }
    std::unordered_set<void*> alive;
    struct Entry {
        uint16_t id;
        uint8_t tribe, owner, age;
        void* c;
        bool body = false; // BUG-0620
    };
    std::vector<Entry> entries;
    for (size_t ti = 0; ti < tribes.size() && ti < 32; ti++) {
        bool player = tribes[ti] == pt;
        for (void* c : members(tribes[ti])) {
            alive.insert(c);
            auto it = g_hm.find(c);
            if (it == g_hm.end()) {
                // the player tribe is split between the players in turns; others belong to nobody
                uint8_t owner = player && !players.empty() ? players[g_player_members_seen++ % players.size()] : 0xFE;
                auto bf = g_born_for.find(c); // BUG-0671: a member a guest bought is that guest's
                if (bf != g_born_for.end()) {
                    if (player && net::is_player(*C.sess, bf->second))
                        owner = bf->second;
                    g_born_for.erase(bf);
                }
                // BUG-0154: never a live id, never 0
                it = g_hm.emplace(c, HMember{net::alloc_id(g_next_id, g_hm_by_id), owner}).first;
                g_hm_by_id[it->second.id] = c;
            }
            it->second.tribe = tribes[ti];
            it->second.body_since = 0;
            entries.push_back({it->second.id, (uint8_t)ti, it->second.owner, F<uint8_t>(c, kAgeOff), c});
        }
    }
    for (auto& [c, hm] : g_hm) { // BUG-0620: the bodies of members that left their list dead
        if (!g_member_body_fix || alive.count(c) || !game::object_alive(c) || !game::is_dead(c))
            continue;
        auto ti = std::find(tribes.begin(), tribes.end(), hm.tribe);
        if (ti == tribes.end() || ti - tribes.begin() >= 32)
            continue;
        if (hm.body_since == 0)
            hm.body_since = now;
        if (now - hm.body_since > kBodyMax)
            continue;
        alive.insert(c);
        g_bodies_sent++;
        entries.push_back({hm.id, (uint8_t)(ti - tribes.begin()), hm.owner, F<uint8_t>(c, kAgeOff), c, true});
    }
    for (auto it = g_hm.begin(); it != g_hm.end();) {
        if (!alive.count(it->first)) {
            g_hm_by_id.erase(it->second.id);
            it = g_hm.erase(it);
        } else
            ++it;
    }
    // chunks that fit one packet; every chunk carries the tribe table. A chunk larger than a packet
    // is dropped by the connection: with 24 members per chunk (fixed until 2026-10-23) the first
    // chunk (the player tribe first) never arrived once the planet had 24 citizens, and the guest
    // removed those mirrors at the end of every cycle (play_tribe.py: the guest's tribe emptied)
    g_cycle++;
    size_t nt = std::min<size_t>(tribes.size(), 32);
    std::vector<TribeInfo> infos; // BUG-0381
    if (g_npc_tribes_fix)
        for (size_t ti = 0; ti < nt; ti++)
            infos.push_back(tribes[ti] == pt ? TribeInfo{} : tribe_info(tribes[ti]));
    size_t head = 2 + 1 + 1 + 1 + nt * 13 + 1 + kTrailerSize + (infos.empty() ? 0 : 4 + 1 + nt * kTribeInfo);
    size_t per = std::max<size_t>(1, (net::kMaxPacket - 64 - head) / kEntry);
    size_t chunks = std::max<size_t>(1, (entries.size() + per - 1) / per);
    for (size_t k = 0; k < chunks; k++) {
        net::Writer w;
        w.u16(g_cycle);
        w.u8((uint8_t)k);
        w.u8((uint8_t)chunks);
        w.u8((uint8_t)std::min<size_t>(tribes.size(), 32));
        for (size_t ti = 0; ti < tribes.size() && ti < 32; ti++) {
            net::Vec3 p = tribe_pos(tribes[ti]);
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.u8(tribes[ti] == pt ? 1 : 0);
        }
        size_t from = k * per, to = std::min(entries.size(), from + per);
        w.u8((uint8_t)(to - from));
        for (size_t i = from; i < to; i++) {
            const Entry& e = entries[i];
            net::Vec3 p = game::position(e.c), v = game::velocity(e.c);
            net::Quat q = game::orientation(e.c);
            w.u16(e.id); w.u8(e.tribe); w.u8(e.owner); w.u8((uint8_t)(e.age | (e.body ? kBodyBit : 0)));
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.f32(v.x); w.f32(v.y); w.f32(v.z);
            w.f32(q.x); w.f32(q.y); w.f32(q.z); w.f32(q.w);
            w.f32(e.body ? 0.f : game::health(e.c)); // (a body: dead, BUG-0620)
        }
        w.u32(kStateTrailer); // BUG-0365: the host's clock (guests before 0.7.3.4 stop reading before it)
        w.u32((uint32_t)(now * 1000.0));
        if (!infos.empty()) { // BUG-0381: after the trailer (guests before it stop reading at the trailer)
            w.u32(kTribeInfoTag);
            w.u8((uint8_t)infos.size());
            for (auto& i : infos) {
                w.u32(i.key.instance); w.u32(i.key.type); w.u32(i.key.group);
                w.u8(i.arch);
            }
        }
        for (auto& p : C.sess->players())
            if (p.id != 0) {
                for (auto& i : infos) // the host's own creation among them: its records first (creations.h)
                    if (i.key.instance)
                        creations::host_species_out(p.id, {i.key.instance, i.key.type, i.key.group});
                C.sess->send_unreliable_to(p.id, net::MSG_TRIBE_STATE, w);
            }
    }
    send_member_anims(now); // BUG-0569
}

static void* resolve_target(const Target& t)
{
    if (t.kind == 1) {
        auto it = g_hm_by_id.find(t.id);
        return it == g_hm_by_id.end() ? nullptr : it->second;
    }
    if (t.kind == 2 && t.noun)
        return game::find_object(t.noun, t.pos, 8.f);
    if (t.kind == 3)
        return find_tree(t.pos, 1.f);
    return nullptr;
}

// host: members of `from` change hands (a gift); owners travel with the snapshots
static void give_on_host(uint8_t from, uint8_t to, const std::vector<uint16_t>& ids)
{
    void* pt = player_tribe();
    int n = 0;
    for (uint16_t id : ids) {
        auto it = g_hm_by_id.find(id);
        auto hm = it == g_hm_by_id.end() ? g_hm.end() : g_hm.find(it->second);
        if (hm == g_hm.end() || hm->second.owner != from || F<void*>(hm->first, kOwnerTribeOff) != pt)
            continue;
        hm->second.owner = to;
        n++;
    }
    g_gifts += n;
    if (n && C.notice)
        C.notice(C.sess->name_of(from) + " gave " + std::to_string(n) + (n == 1 ? " tribe member to " : " tribe members to ") +
                 C.sess->name_of(to));
}

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
static bool g_guest_baby_fix = true;
COOP_FIX("tribe.guest_baby", "BUG-0671", g_guest_baby_fix, "tribe: a tribe member the guest buys at its hut is born in the host's game and is the guest's");
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
};
static std::vector<BabyAsk> g_baby_asks;   // host: guests' babies its game did not make yet
static std::vector<BabyMade> g_baby_made;  // host: babies its own game made lately that no guest claimed
static uint32_t g_babies_sent, g_babies_made, g_babies_paired, g_babies_local;
static uintptr_t g_baby_from; // the last caller (tests: which path made it)
static bool baby_near(const net::Vec3& a, const net::Vec3& b)
{
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz < kBabyNear * kBabyNear;
}
static void baby_give(void* c, uint8_t from)
{
    auto it = g_hm.find(c);
    if (it != g_hm.end())
        it->second.owner = from;
    else
        g_born_for[c] = from;
}
// host: the new members of the player tribe that one call of the game's function made
static std::vector<void*> baby_call(void* stage, const net::Vec3& p)
{
    std::vector<void*> out;
    void* pt = player_tribe();
    auto before = pt ? members(pt) : std::vector<void*>{};
    o_BabyBorn(stage, p.x, p.y, p.z);
    if (pt)
        for (void* c : members(pt))
            if (std::find(before.begin(), before.end(), c) == before.end())
                out.push_back(c);
    return out;
}
static void SK_THISCALL h_BabyBorn(void* stage, float x, float y, float z)
{
    g_baby_from = (uintptr_t)__builtin_return_address(0);
    bool on = g_ok && g_guest_baby_fix && game::mode_id() == modes::kTribe && stage;
    if (on && is_guest() && !local_play() && !g_gm.empty()) {
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
    for (void* c : baby_call(stage, p)) {
        auto a = std::find_if(g_baby_asks.begin(), g_baby_asks.end(), [&](const BabyAsk& k) { return baby_near(k.pos, p); });
        if (a != g_baby_asks.end()) { // the guest's baby, made here by its relayed effect
            baby_give(c, a->from);
            g_baby_asks.erase(a);
            g_babies_paired++;
        } else {
            g_baby_made.push_back({c, p, now});
        }
    }
}
static void baby_on_host(uint8_t from, const net::Vec3& pos)
{
    if (!o_BabyBorn || !std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z) || std::fabs(pos.x) > 1e6 ||
        std::fabs(pos.y) > 1e6 || std::fabs(pos.z) > 1e6 || g_baby_asks.size() >= 16) {
        g_cmds_rejected++;
        return;
    }
    auto m = std::find_if(g_baby_made.begin(), g_baby_made.end(), [&](const BabyMade& k) { return baby_near(k.pos, pos); });
    if (m != g_baby_made.end()) { // the host's game made it already (the relayed effect came first)
        if (game::object_alive(m->c))
            baby_give(m->c, from);
        g_baby_made.erase(m);
        g_babies_paired++;
        return;
    }
    g_baby_asks.push_back({from, pos, H->time_seconds()});
}
static void baby_tick(double now)
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
        for (void* c : baby_call(stage, k.pos)) { // none came: the host's game makes it (null at the cap: none)
            baby_give(c, k.from);
            g_babies_made++;
        }
    }
}
static void baby_forget()
{
    g_born_for.clear();
    g_baby_asks.clear();
    g_baby_made.clear();
}

static void host_command(uint8_t from, net::Reader& r)
{
    uint8_t kind = r.u8(), n = r.u8();
    std::vector<uint16_t> ids;
    for (int i = 0; i < n && r.ok(); i++)
        ids.push_back(r.u16());
    int action = (int)r.u32();
    Target tgt = read_target(r);
    net::Vec3 pos;
    pos.x = r.f32(); pos.y = r.f32(); pos.z = r.f32();
    if (!r.ok())
        return;
    if (kind == CMD_BABY) { // BUG-0671
        if (g_guest_baby_fix && from != 0 && game::mode_id() == modes::kTribe)
            baby_on_host(from, pos);
        else
            g_cmds_rejected++;
        return;
    }
    if (kind == CMD_GIVE) {
        if (action >= 0 && action <= 0xFF && net::is_player(*C.sess, (uint8_t)action))
            give_on_host(from, (uint8_t)action, ids);
        else
            g_cmds_rejected++;
        return;
    }
    // CitizenAction values of the game's own orders are 0..28 (docs/re/tribe.md 2)
    if (kind == CMD_ACTION && (action < 0 || action > 28)) {
        g_cmds_rejected++;
        return;
    }
    if (kind != CMD_ACTION && kind != CMD_WALK)
        return;
    void* pt = player_tribe();
    void* target = kind == CMD_ACTION ? resolve_target(tgt) : nullptr;
    char b[160];
    snprintf(b, sizeof b, "from=%u kind=%u n=%u action=%d target=%u/%u/%X (%.1f %.1f %.1f) -> %p", from, kind,
             (unsigned)ids.size(), action, tgt.kind, tgt.id, tgt.noun, tgt.pos.x, tgt.pos.y, tgt.pos.z, target);
    g_last_cmd = b;
    if (kind == CMD_ACTION && tgt.kind != 0 && !target) {
        g_cmds_rejected++;
        g_last_cmd += " rejected: no such target here";
        return; // the object is gone on the host
    }
    for (uint16_t id : ids) {
        auto it = g_hm_by_id.find(id);
        if (it == g_hm_by_id.end()) {
            g_cmds_rejected++;
            continue;
        }
        void* c = it->second;
        auto hm = g_hm.find(c);
        if (hm == g_hm.end() || hm->second.owner != from || F<void*>(c, kOwnerTribeOff) != pt ||
            !game::object_alive(c)) {
            g_cmds_rejected++;
            continue;
        }
        if (kind == CMD_WALK) {
            void* rec = thiscall<void*>(a_DoAction, c, 4, (void*)nullptr, (void*)nullptr);
            float p[3] = {pos.x, pos.y, pos.z};
            if (rec)
                thiscall<void>(a_SetValueVector3, (uint8_t*)rec + 0x39, (const float*)p);
        } else
            thiscall<void*>(a_DoAction, c, action, target, (void*)nullptr);
        g_cmds_done++;
    }
}

// ---------------- guest: mirrors ----------------
// The game-over caption ("Alas! Your population has fallen below the minimum...") comes when no
// member of the player tribe has health left. A member added while it is up has no species fields
// (+0xB20, +0xB54) and the OK (bounce back) faulted at 0xC0DAE3 on it, then the game froze
// (BUG-0172). No member is added to the player tribe then. `coop.tribe fix gameover 0`: the old way.
static bool g_fix_gameover = true;
COOP_FIX("tribe.gameover", "BUG-0172", g_fix_gameover, "tribe: no member is added during the game-over caption");
static uint32_t g_spawns_blocked;
// BUG-0288: a guest's player tribe holds mirrors only. When every mirror had played its death (the
// host's tribe knocked out) the guard above refused every new mirror for good, also after the host
// bounced back and the dead mirrors were removed (an empty tribe has no member with health): the
// guests missed the host's whole new tribe (4-player campaign, "t: 16 missing on the guest"). The
// game-over caption comes on the host only (the guest's game shows none, its input lock stays 0):
// on a guest the guard holds only while its own game has a caption or cinematic up (the input lock).
// `coop.fix tribe.mirror_out off`: the guard as before.
static bool g_fix_mirror_out = true;
COOP_FIX("tribe.mirror_out", "BUG-0288", g_fix_mirror_out,
         "tribe: a guest mirrors the host's members after a game over (its own tribe is not out)");
static uint32_t g_mirror_out_passed;
static bool player_tribe_out(void* t)
{
    if (!g_fix_gameover || !t || t != player_tribe())
        return false;
    if (g_fix_mirror_out && is_guest() && coop_ui::input_lock_now() <= 0) {
        bool none = true;
        for (void* c : members(t))
            if (game::health(c) > 0)
                none = false;
        if (none)
            g_mirror_out_passed++;
        return false;
    }
    for (void* c : members(t))
        if (game::health(c) > 0)
            return false;
    return true;
}

static void* spawn_member(void* t, uint8_t age)
{
    if (player_tribe_out(t)) {
        g_spawns_blocked++;
        return nullptr;
    }
    // SpawnMember (vt 0x98) returns null at the population cap: then create + add directly
    void* c = vcall<void*>(t, 0x98 / 4, (int)age);
    if (!c) {
        c = thiscall<void*>(a_CreateCitizen, t, (int)age);
        if (c)
            thiscall<void>(a_AddMember, t, c, 1);
    }
    return c;
}

// BUG-0667: a detached body's tribe back into its +0x1014 for its teardown, when that tribe is still one of the world's
static void restore_body_tribe(void* c, void* t)
{
    if (!g_body_teardown_fix || !t || F<void*>(c, kOwnerTribeOff))
        return;
    auto tribes = all_tribes();
    if (std::find(tribes.begin(), tribes.end(), t) == tribes.end())
        return;
    F<void*>(c, kOwnerTribeOff) = t;
    g_teardown_restored++;
}

static void remove_member(void* c)
{
    void* t = F<void*>(c, kOwnerTribeOff);
    if (t)
        vcall<void>(t, 0x9C / 4, c, 1); // RemoveMember(c, destroy)
    else if (auto it = g_detached.find(c); it != g_detached.end()) { // BUG-0620: a body out of its tribe: the object
        void* bt = it->second;
        g_detached.erase(it);
        restore_body_tribe(c, bt); // BUG-0667
        game::destroy_instance(c);
    }
}

// BUG-0495: the objects the mod makes on a guest (the members it spawns as mirrors of the host's, and the
// NPC tribes it makes for the host's with SpawnNpcTribe, BUG-0381) are the host's world, not the guest's:
// they never go into the guest's saves (CLAUDE.md 2). A savestrip filter writes a null reference for them,
// and while the save is written they leave the lists that hold them (a tribe's member vector +0x340 and
// selectable vector +0x354, citizen+0xC0) so no null is read back there on load (BUG-0470's rule). A made
// tribe goes out with its hut (+0x368), its tools (+0x36C, +0x380) and its food mat. Members of the guest's
// own game bound as mirrors stay (they came with the loaded world). `coop.fix tribe.save_strip off`: saved
// like the game's own (0.7.4).
static bool g_fix_save_strip = true;
COOP_FIX("tribe.save_strip", "BUG-0495", g_fix_save_strip, "saves: a Tribe guest's mirrors of the host's members and tribes are kept out of its saves");
constexpr uint32_t kCitizenNoun = 0x18EB4B7, kHutNoun = 0x1E4DAAE, kToolNoun = 0x18C8F0C, kFoodMatNoun = 0x629BAFE;
static std::unordered_map<void*, uint32_t> g_ours_c, g_ours_t; // spawned mirrors / made tribes -> serial
static std::unordered_set<void*> g_strip_now;                  // this save's objects of ours
static uint32_t g_save_stripped, g_save_saves;
static void note_ours(std::unordered_map<void*, uint32_t>& m, void* o)
{
    if (m.size() > 512) // (old worlds' pointers)
        for (auto it = m.begin(); it != m.end();)
            it = game::object_alive(it->first) && game::object_serial(it->first) == it->second ? std::next(it) : m.erase(it);
    m[o] = game::object_serial(o);
}
static bool ours_in(const std::unordered_map<void*, uint32_t>& m, void* o)
{
    auto it = m.find(o);
    return it != m.end() && game::object_alive(o) && game::object_serial(o) == it->second;
}
struct TookT {
    void* t;
    uintptr_t off;
    void* p;
};
static std::vector<TookT> g_took_t;
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
    if (!g_fix_save_strip || !is_guest() || game::mode_id() != modes::kTribe)
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
                    g_took_t.push_back({t, off, *it});
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
    for (auto& t : g_took_t) { // back at the end of the list (the capacity is the one they left)
        void**& e = F<void**>(t.t, t.off + 4);
        if (game::object_alive(t.t) && e < F<void**>(t.t, t.off + 8))
            *e++ = t.p;
    }
    g_took_t.clear();
    g_strip_now.clear();
}
static bool save_filter(void* o, uint32_t noun)
{
    if (!g_fix_save_strip || g_strip_now.empty() ||
        (noun != kCitizenNoun && noun != kTribeNoun && noun != kHutNoun && noun != kToolNoun && noun != kFoodMatNoun) ||
        !g_strip_now.count(o))
        return false;
    g_save_stripped++;
    return true;
}

// BUG-0381 (tribe.npc_tribes): a guest makes each host NPC tribe that has no tribe of this game
// within the mapping's 40 m (guest_state). `p`: the tribe table after the trailer (tag, count,
// count x kTribeInfo). One try per host tribe every 5 s (an unknown species is asked for first).
static std::map<std::pair<int, int>, double> g_npc_tribe_tried; // host tribe position (2 m grid) -> last try
static std::map<std::pair<int, int>, void*> g_npc_tribe_made;   // ... -> the tribe made for it here
static void make_npc_tribes(const uint8_t* p, size_t len, const std::vector<std::pair<net::Vec3, bool>>& htribes,
                            const std::vector<void*>& locals)
{
    uint32_t tag;
    memcpy(&tag, p, 4);
    if (!g_npc_tribes_fix || !a_SpawnNpcTribe || tag != kTribeInfoTag || p[4] != htribes.size() || len < 5)
        return;
    double now = H->time_seconds();
    for (size_t i = 0; i < htribes.size() && i < g_local_tribe_of.size(); i++) {
        if (htribes[i].second || g_local_tribe_of[i])
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
                g_local_tribe_of[i] = made->second;
                continue;
            }
            g_npc_tribe_made.erase(made); // gone (a raid, a load): may be made again
        }
        if (g_npc_tribes_made >= 64)
            continue; // a bound against a loop of tribes made and lost
        auto tried = g_npc_tribe_tried.find(cell);
        if (tried != g_npc_tribe_tried.end() && now - tried->second < 5.0)
            continue;
        g_npc_tribe_tried[cell] = now;
        creations::Key lk = creations::local_species({k.instance, k.type, k.group});
        void* species = game::species_by_key({lk.instance, lk.type, lk.group});
        if (!species) { // a species of the host's world this game lacks: the host sends it
            creations::want_species({k.instance, k.type, k.group});
            g_npc_tribes_failed++;
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
        g_local_tribe_of[i] = t;
        note_ours(g_ours_t, t); // BUG-0495
        g_npc_tribe_made[cell] = t;
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
//   its own time and place and the guests make each of the host's (tribe.npc_tribes), so a guest's own
//   forming only adds a village nobody else has. On a guest in a session it does not run.
// * the host's game no longer has the tribe the guest's was mapped to (a tribe gone on the host, or one the
//   guest's game had in its world and the host's not): its members were never touched (the end of a cycle
//   removes only the unknown members of mapped tribes). After 5 s unmapped in complete cycles they go too.
// `coop.fix tribe.guest_tribes off`: the guest's game forms and keeps its own tribes (0.7.4).
static bool g_guest_tribes_fix = true;
COOP_FIX("tribe.guest_tribes", "BUG-0483", g_guest_tribes_fix,
         "tribe: a guest's game forms no NPC tribe of its own, and empties tribes the host no longer has");
static uint32_t g_forms_blocked, g_forms_run, g_unmapped_removed;
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
static bool g_form_pace_fix = true;
COOP_FIX("tribe.form_pace", "BUG-0571", g_form_pace_fix,
         "tribe: new villages appear when the party's food reaches what one player's tribe needs alone, times the players");
static uint32_t g_forms_paced;
static double g_pace_checked = -10;
static bool g_pace_hold;
static std::string g_pace_last; // tests: the last decision
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

static void* SK_THISCALL h_Form(void* self, int arg)
{
    if (g_guest_tribes_fix && g_npc_tribes_fix && is_guest() && game::mode_id() == modes::kTribe) {
        if (g_forms_blocked++ < 5)
            H->log(M, SK_INFO, "tribe: this game's NPC tribe controller would form a tribe: not on a guest (tribe.guest_tribes)");
        return nullptr;
    }
    if (pace_holds(H->time_seconds())) {
        if (g_forms_paced++ % 600 == 0)
            H->log(M, SK_INFO, "tribe: the NPC tribe controller would form the third tribe: %s (tribe.form_pace)", g_pace_last.c_str());
        return nullptr;
    }
    g_forms_run++;
    return o_Form(self, arg);
}
static void unmapped_tick(const std::vector<void*>& locals, void* pt, double now)
{
    if (!g_guest_tribes_fix)
        return;
    std::unordered_map<void*, double> keep;
    for (void* t : locals) {
        if (t == pt || std::find(g_local_tribe_of.begin(), g_local_tribe_of.end(), t) != g_local_tribe_of.end())
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
            g_gid_of.erase(c);
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
    if (std::find(g_local_tribe_of.begin(), g_local_tribe_of.end(), t) != g_local_tribe_of.end())
        return 0;
    if (game::cinematic_playing())
        return -1;
    for (auto it = g_npc_tribe_made.begin(); it != g_npc_tribe_made.end();) // made for a host tribe gone since
        it = it->second == t ? g_npc_tribe_made.erase(it) : std::next(it);
    for (void* c : members(t)) {
        g_gid_of.erase(c);
        remove_member(c);
        g_unmapped_removed++;
    }
    g_unmapped_since.erase(t);
    g_ours_t.erase(t);
    game::destroy_creature(t); // (not an animal: DestroyInstance at once)
    return 1;
}

static void guest_state(net::Reader& r)
{
    uint16_t cycle = r.u16();
    uint8_t chunk = r.u8(), chunks = r.u8(), nt = r.u8();
    if (!r.ok() || !net::cycle_chunk_ok(chunk, chunks))
        return;
    bool started = false;
    if (!g_cyc.begin(cycle, g_cycle_order_fix, &started))
        return; // a late chunk of an older cycle
    if (started)
        g_cycle_ids.clear();
    std::vector<std::pair<net::Vec3, bool>> htribes;
    for (int i = 0; i < nt && r.ok(); i++) {
        net::Vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        htribes.push_back({p, r.u8() != 0});
    }
    if (!r.ok())
        return;
    // host tribe index -> local tribe (the player tribe directly, others by position)
    auto locals = all_tribes();
    void* pt = player_tribe();
    g_local_tribe_of.assign(htribes.size(), nullptr);
    for (size_t i = 0; i < htribes.size(); i++) {
        if (htribes[i].second) {
            g_local_tribe_of[i] = pt;
            continue;
        }
        float best = 40.f;
        for (void* t : locals) {
            float d = net::dist(tribe_pos(t), htribes[i].first);
            if (t != pt && d < best) {
                best = d;
                g_local_tribe_of[i] = t;
            }
        }
    }
    double now = H->time_seconds();
    uint8_t n = r.u8();
    // BUG-0365: the host's clock after the entries (none from a host before 0.7.3.4)
    bool timed = false;
    uint32_t host_ms = 0;
    // BUG-0381: the tribe table's species / archetype after the trailer (a host with tribe.npc_tribes)
    size_t extra = r.ok() && r.left() >= n * kEntry + kTrailerSize ? r.left() - n * kEntry - kTrailerSize : 0;
    bool info = extra >= 5 && extra == 5 + (size_t)r.cur()[n * kEntry + kTrailerSize + 4] * kTribeInfo;
    if (r.ok() && (r.left() == n * kEntry + kTrailerSize || info)) {
        const uint8_t* t = r.cur() + n * kEntry;
        uint32_t magic;
        memcpy(&magic, t, 4);
        memcpy(&host_ms, t + 4, 4);
        timed = magic == kStateTrailer;
    }
    if (info && timed)
        make_npc_tribes(r.cur() + n * kEntry + kTrailerSize, extra, htribes, locals);
    g_mirror_timed += timed;
    for (int i = 0; i < n && r.ok(); i++) {
        uint16_t id = r.u16();
        GMember m;
        m.tribe = r.u8(); m.owner = r.u8(); m.age = r.u8();
        m.body = (m.age & kBodyBit) != 0; // BUG-0620
        m.age &= (uint8_t)~kBodyBit;
        m.pos.x = r.f32(); m.pos.y = r.f32(); m.pos.z = r.f32();
        m.vel.x = r.f32(); m.vel.y = r.f32(); m.vel.z = r.f32();
        m.rot.x = r.f32(); m.rot.y = r.f32(); m.rot.z = r.f32(); m.rot.w = r.f32();
        m.hp = r.f32();
        if (!r.ok())
            return;
        g_cycle_ids.insert(id);
        auto& g = g_gm[id];
        g.id = id;
        void* c = g.c;
        g.tribe = m.tribe; g.owner = m.owner; g.age = m.age; g.body = m.body;
        g.pos = m.pos; g.vel = m.vel; g.rot = m.rot; g.hp = m.hp; g.seen = now;
        if (g_test_lift != 0.f) { // tests (BUG-0329): the host's member as if its ground were higher
            float r = std::sqrt(g.pos.x * g.pos.x + g.pos.y * g.pos.y + g.pos.z * g.pos.z);
            if (r > 1.f) {
                float k = (r + g_test_lift) / r;
                g.pos = {g.pos.x * k, g.pos.y * k, g.pos.z * k};
            }
        }
        if (timed) {
            net::PlayerState ps{};
            ps.time_ms = host_ms;
            ps.flags = net::PF_IN_WORLD;
            ps.pos = g.pos;
            ps.rot = g.rot;
            ps.vel = g.vel;
            g.buf.delay = g_test_delay;
            g.buf.lead = true; // BUG-0425 (drive.lead)
            g.buf.smooth_max = kMirrorSmoothMax; // BUG-0619 (drive.lead_smooth)
            g.buf.push(ps, now);
        }
        if (c && !game::object_alive(c)) {
            g_gid_of.erase(c);
            g.c = c = nullptr;
        }
        if (c || m.body) // (no new mirror for a body)
            continue;
        void* t = m.tribe < g_local_tribe_of.size() ? g_local_tribe_of[m.tribe] : nullptr;
        if (!t)
            continue;
        // bind to the nearest unbound local member of that tribe, else spawn one
        float best = 6.f;
        for (void* lc : members(t)) {
            if (g_gid_of.count(lc))
                continue;
            float d = net::dist(game::position(lc), m.pos);
            if (d < best) {
                best = d;
                c = lc;
            }
        }
        if (!c) {
            c = spawn_member(t, m.age);
            if (!c)
                continue;
            note_ours(g_ours_c, c); // BUG-0495
            // BUG-0444: the host's position (g.pos: with the tests' lift), onto this game's ground where that lies
            // higher (BUG-0329)
            game::teleport(c, g_mirror_ground_fix ? drive::out_of_ground(g.pos, true) : g.pos, m.rot);
            g_spawned++;
        }
        g.c = c;
        g_gid_of[c] = id;
    }
    if (!r.ok()) // a cut chunk does not count: the cycle stays incomplete, nothing is removed
        return;
    g_cyc.add(chunk);
    if (chunk + 1 < chunks)
        return;
    if (!g_cyc.complete(chunks)) { // a chunk of this cycle was lost or comes late: keep everything
        g_incomplete++;
        return;
    }
    // end of a full snapshot: members the host no longer has go away, and so do local members
    // the host does not know (the guest's tribes are only mirrors)
    for (auto it = g_gm.begin(); it != g_gm.end();) {
        if (!g_cycle_ids.count(it->first)) {
            if (it->second.c && game::object_alive(it->second.c)) {
                remove_member(it->second.c);
                g_removed++;
            }
            g_gid_of.erase(it->second.c);
            it = g_gm.erase(it);
        } else
            ++it;
    }
    for (void* t : g_local_tribe_of)
        if (t)
            for (void* lc : members(t))
                if (!g_gid_of.count(lc)) {
                    remove_member(lc);
                    g_removed++;
                }
    unmapped_tick(locals, pt, now); // BUG-0483
}

// BUG-0173: a member knocked out on the host (health 0, the game's dead flag +0xB5E, behaviour
// node 0x05CA7FA7, it stays in the tribe until it respawns: the chieftain, NPC tribes) stood on the
// guest with its last health. The mirror's behaviour tree does not run on a guest, so the mod plays
// the death itself (Die + the death animation, game::play_death). When the host's member has health
// again, the dead mirror is replaced by a fresh one (the next snapshot binds or spawns it).
// `coop.tribe fix ko 0`: the code before (tests).
static bool g_fix_ko = true;
COOP_FIX("tribe.ko", "BUG-0173", g_fix_ko, "tribe: a guest's mirror of a member without health plays the death");
static uint32_t g_ko_shown, g_ko_replaced;

// BUG-0365: a mirror of a walking member was driven by drive::step towards the latest snapshot moved on
// by its arrival age: a MoveTo 0.25 s ahead every 0.1 s and SetPosition pulls. It walked at 9 m/s (~ the
// MoveTo given after SetDesiredSpeed resets the speed): while the host's member speeds up or slows down
// (1-5 m/s, every start and end of a walk) it reached the point ahead within a frame or two, stopped and
// walked again (the drawn step off the target's in 14-29 % of those frames), set off 0.5-1.6 m late and
// caught up at 15-24 m/s. The snapshots carried no sender time, so network jitter went straight into the
// target (the BUG-0122 class; over a mobile network 0.1 s windows matched the path in 71-80 %).
// * tribe.mirror_time: the host sends its clock with every chunk (an 8-byte trailer older guests ignore);
//   each mirror keeps a net::SnapshotBuffer (as the Creature puppets) and is driven towards the state
//   kMirrorDelay s back, walking at the interpolated path's velocity. Off (or from an older host): the
//   latest state + vel * age.
// * tribe.mirror_walk: while the member moves and the mirror is within kMirrorWalkMax of its path, the
//   shared drive::walk_place (driver.h, BUG-0360) walks it and places it on the path every frame (at its
//   own ground height, BUG-0329; the speed set after the MoveTo). Off: drive::step as in 0.7.3.3.
static void mirror_target(GMember& g, double now, net::Vec3& pos, net::Quat& rot, net::Vec3& vel, double& age)
{
    net::PlayerState s, back;
    if (g_mirror_time_fix && !g.buf.empty() && g.buf.sample(now, s)) {
        pos = s.pos;
        rot = s.rot;
        // the velocity of the interpolated path over the last kPathDt, not the newer state's: from a
        // standing state to a walking one the path moves slower than the walking state says, and the
        // game walks the mirror at this speed between two placings (it showed as stop-and-go)
        vel = s.vel;
        // BUG-0619 (drive.lead_smooth): the smoothed target's own velocity (its offset closing a late state's step
        // included): walked at the raw path's speed the mirror fell 0.5-1.4 m behind a closing target and surged
        // after it at 7-8 m/s when the closing ended (tribe_walk_mobile 20261106-140306 #3: uneven 16 %)
        g.lead_sp = -1;
        if (g.buf.jittery() != g.jittery) { // (diagnostics: the lead switched off / on by the link's jitter)
            g.jittery = g.buf.jittery();
            static unsigned said = 0; // (BUG-0650: 400 lines; 12 did not cover one test run)
            if (said++ < 400)
                H->log(M, SK_INFO, "tribe: mirror #%u lead %s (one-way latency %.0f ms, BUG-0619)", g.id,
                       g.jittery ? "off" : "on", g.buf.jitter() * 1000.0);
        }
        if (g.buf.smooth_velocity(vel)) {
            // the walk's speed rises at most kMirrorWalkAcc to the target's (it never goes under the lead's own):
            // set at once, the game's walk of the next frame grew by up to 0.4 m on top of the placing (1-1.3 m frames)
            float want = drive::speed(vel), raw = drive::speed(s.vel);
            double dt = now - g.walk_t;
            float w = (g.walk_t <= 0 || dt > 0.5) ? raw : g.walk_sp + kMirrorWalkAcc * (float)std::min(std::max(dt, 0.0), 0.1);
            w = want < raw ? want : std::min({std::max(w, raw), want, raw + kMirrorWalkOver});
            g.walk_sp = w;
            g.lead_sp = raw;
            g.walk_t = now;
            if (want > 0.01f) {
                float k = w / want;
                vel = {vel.x * k, vel.y * k, vel.z * k};
            }
            age = 0;
            return;
        }
        if (g.buf.sample(now - kPathDt, back)) {
            float k = (float)(1.0 / kPathDt);
            vel = {(s.pos.x - back.pos.x) * k, (s.pos.y - back.pos.y) * k, (s.pos.z - back.pos.z) * k};
            // never faster than the member walks: a late state over a bad network corrects the path by
            // 0.3-0.9 m at once, and walked at that path speed (13 m/s seen) the mirror surged ~1 m in a
            // frame on top of the placing's catch-up
            float ps = drive::speed(vel), ss = drive::speed(s.vel);
            if (ss > 0.3f && ps > ss) {
                float c = ss / ps;
                vel = {vel.x * c, vel.y * c, vel.z * c};
            }
        }
        age = 0;
        return;
    }
    pos = g.pos;
    rot = g.rot;
    vel = g.vel;
    age = now - g.seen;
    g.lead_sp = -1;
}

// BUG-0444: this game's time is paused (cGameTimeManager +0x48 bit 0: after a cold join for 10-40 s, the pause
// menu): no citizen update runs, so nothing but the mod moves or grounds the mirrors
static bool game_time_paused()
{
    static uintptr_t get = H->sym("Simulator::cGameTimeManager::Get");
    uint8_t* tm = get ? skg::cdecl_call<uint8_t*>(get) : nullptr;
    return tm && !IsBadReadPtr(tm + 0x48, 1) && (tm[0x48] & 1);
}

// drive::step at the mirror's own height (BUG-0329) and, with tribe.mirror_ground, out of this game's ground
// (onto it from above too while the game is paused: nothing else brings it down)
static void mirror_step(GMember& g, const net::Vec3& pos, const net::Quat& rot, const net::Vec3& vel, double now, double age)
{
    const uint32_t gr = g.ds.grounded;
    if (g_mirror_ground_fix && game_time_paused()) {
        float a = (float)std::min(std::max(age, 0.0), 0.5);
        net::Vec3 t = drive::onto_ground(
            drive::at_own_height(net::Vec3{pos.x + vel.x * a, pos.y + vel.y * a, pos.z + vel.z * a}, game::position(g.c)));
        drive::step(g.c, t, rot, vel, g.ds, now, 0, true, 8.f, false, false, false, true);
    } else
        drive::step(g.c, pos, rot, vel, g.ds, now, age, true, 8.f, false, false, true, g_mirror_ground_fix);
    if (g.ds.grounded == gr)
        return;
    g_mirror_grounded++;
    if (g.ds.grounded <= 3) // (diagnostics: the fix at work, the first times per mirror)
        H->log(M, SK_INFO, "tribe: mirror %u (owner %u) was %.2f m under this game's ground: put onto it (tribe.mirror_ground)",
               (unsigned)g.id, (unsigned)g.owner, -g.ds.grounded_from);
}

static void mirror_drive(GMember& g, double now)
{
    net::Vec3 pos, vel;
    net::Quat rot;
    double age;
    mirror_target(g, now, pos, rot, vel, age);
    float a = (float)std::min(std::max(age, 0.0), 0.5);
    g.aim = drive::at_own_height(net::Vec3{pos.x + vel.x * a, pos.y + vel.y * a, pos.z + vel.z * a}, game::position(g.c));
    float sp = drive::speed(vel);
    g.aim_speed = sp;
    // BUG-0444: never aimed under this game's ground (walk_place's ground takes 3 m only), nor over it while the
    // game is paused (walked down a slope it stayed at its height, 3 m over the ground)
    if (g_mirror_ground_fix)
        g.aim = game_time_paused() ? drive::onto_ground(g.aim) : drive::out_of_ground(g.aim, true);
    if (g_mirror_walk_fix && sp > 0.3f && net::dist(game::position(g.c), g.aim) < kMirrorWalkMax) {
        drive::WalkPlace o;
        o.own_height = !g_mirror_ground_fix; // (the aim is at its own height already)
        o.speed_after_goal = true;
        o.max_dt = 0.4; // a slow guest keeps up with its members (driver.h)
        o.ground = g_mirror_ground_fix; // BUG-0444
        // BUG-0425; BUG-0619: not while the smoothed target closes the error itself (catch_prop on top measured the
        // catch-up from the game's walk and drew walk + catch-up, 0.9-1.25 m frames), nor without the lead (0.7.8)
        o.catch_prop = g.lead_sp < 0 && !(net::g_lead_smooth_fix && g.buf.jittery());
        if (g.lead_sp >= 0)
            o.lead_speed = std::max(g.lead_sp, 0.5f); // BUG-0619
        drive::walk_place(g.c, g.aim, rot, vel, g.ds, now, o);
        g_mirror_walks++;
        // BUG-0386 (diagnostics): a walked mirror held over 2 m off its aim for 1 s: what is around it, once per 10 s
        if (g.ds.last_err > 2.f) {
            if (g.held_since <= 0)
                g.held_since = now;
            if (now - g.held_since > 1.0 && now - g.held_said > 10.0) {
                g.held_said = now;
                H->log(M, SK_INFO, "tribe: mirror #%u held %.1f m off its aim for %.1f s (place lost %u, held %d, %.1f m/s); "
                       "objects within 3 m: %s (BUG-0386)", g.id, g.ds.last_err, now - g.held_since, (unsigned)g.ds.place_lost,
                       (int)g.ds.held, sp, game::objects_near(game::position(g.c), 3.f, g.c).c_str());
            }
        } else
            g.held_since = 0;
        return;
    }
    drive::walk_end(g.c, g.aim, g.ds, now);
    mirror_step(g, pos, rot, vel, now, age);
}

static void guest_tick(double now)
{
    if (local_play())
        return; // the last states would hold the members for 3 s
    for (auto& [id, g] : g_gm) {
        if (!g.c || !game::object_alive(g.c) || now - g.seen > 3.0)
            continue;
        g.ds.drawn = game::position(g.c); // coop.track (BUG-0365): where the game drew it this frame
        if (g_fix_ko && g.hp <= 0) {
            if (!game::is_dead(g.c)) {
                game::set_health(g.c, 0);
                game::play_death(g.c);
                g_ko_shown++;
            }
            if (g.body && g_member_body_fix)
                detach_body(g.c);
            mirror_step(g, g.pos, g.rot, g.vel, now, now - g.seen);
            continue;
        }
        if (g_fix_ko && game::is_dead(g.c)) { // back on its feet on the host
            g_gid_of.erase(g.c);
            remove_member(g.c);
            g.c = nullptr;
            g_ko_replaced++;
            continue;
        }
        mirror_drive(g, now);
        float hp = game::health(g.c);
        if (std::fabs(hp - g.hp) > 0.01f && g.hp > 0)
            game::set_health(g.c, g.hp);
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
static std::unordered_map<uint8_t, void*> g_chief;                              // owner -> chief citizen
static std::unordered_map<void*, float> g_chief_scale;                          // chief -> its own scale
static std::unordered_map<void*, double> g_chief_saved;                         // chief -> last rescue
static uint32_t g_pincer_hits, g_aura_hits, g_chief_rescues, g_duet_ticks, g_heals, g_epics, g_epic_kills;
static void* g_epic;
static void* g_epic_herd; // the herd g_epic lives in (ours: quieted when the epic is gone)
static std::unordered_set<uint8_t> g_epic_hitters;
// BUG-0146: only the players' members count as hitters (an NPC tribe's member is owner 0xFE).
// BUG-0121: the epic's herd is reused and quieted, a live epic never goes into a save.
// `coop.tribe fix epic_players|epic_herd 0`: the old behaviour (tests).
static bool g_fix_epic_players = true, g_fix_epic_herd = true;
COOP_FIX("tribe.epic_players", "BUG-0146", g_fix_epic_players, "tribe: only players' members count as epic hitters");
COOP_FIX("tribe.epic_herd", "BUG-0121", g_fix_epic_herd, "tribe: the epic's herd is reused and quieted, a live epic is not saved");
static uint32_t g_epic_herd_reused, g_epic_saved_out;
constexpr uint32_t kEpicArchetype = 0xFC811218u;
static double g_epic_next, g_last_duet, g_duet_told;

static uint16_t member_id(void* c)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        auto it = g_hm.find(c);
        return it == g_hm.end() ? 0 : it->second.id;
    }
    auto it = g_gid_of.find(c);
    return it == g_gid_of.end() ? 0 : it->second;
}

uint16_t id_of(void* c) { return member_id(c); }
void* mirror_of(uint16_t id)
{
    auto it = g_gm.find(id);
    return it == g_gm.end() ? nullptr : it->second.c;
}

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
    auto it = g_gid_of.find(c);
    if (it == g_gid_of.end())
        return game::health(c);
    auto g = g_gm.find(it->second);
    return g == g_gm.end() ? game::health(c) : g->second.hp;
}

// A former chief gets its own size back (spatial vt 0x40 SetScale). BUG-0081: it is called only
// for a citizen that is still a member of the player tribe in the game's own list; before, the only
// check was object_alive, which reads the citizen's own memory and cannot tell a freed one (a guest
// mirror removed by guest_state, a load): the call went through a vtable read from freed memory.
// keep: the chiefs that stay (nullptr: none, the session ends). `coop.tribe fix chiefs 0`: the old check.
static bool g_fix_chiefs = true;
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
    return it != g_chief.end() && it->second != c && net::dist(game::position(it->second), game::position(c)) < 15.f;
}

// cCreatureCitizen TakeDamage 0xC26920 (this = citizen+0x5A8): every hit on a citizen
using CTD_t = int(SK_THISCALL*)(void*, float, uint32_t, int, const void*, void*);
static CTD_t o_CitizenTD;
struct EpicBlows {
    uint32_t n = 0;
    float last = 0, max = 0;
};
static std::unordered_map<void*, EpicBlows> g_epic_blows; // BUG-0515 (diagnostics): an epic -> its blows on members
static bool is_epic_animal(void* a)
{
    static uintptr_t is_epic = H->sym("SporeCoop::cCreatureAnimal_IsEpic");
    return a && is_epic && !IsBadReadPtr(a, 8) && game::noun_id(a) == 0x18EB45E && thiscall<bool>(is_epic, a);
}
static int SK_THISCALL h_CitizenTD(void* comb, float dmg, uint32_t pol, int type, const void* pos, void* att)
{
    if (civ::hit_is_cosmetic(att)) // BUG-0477: a Civ guest's copy of a host's shot hurts nobody
        return 0;
    if (is_host() && active()) {
        void* victim = (uint8_t*)comb - 0x5A8;
        void* pt = player_tribe();
        void* vt = F<void*>(victim, kOwnerTribeOff);
        void* ac = att && !IsBadReadPtr(att, 4) ? vcall<void*>(att, 0xC / 4) : nullptr;
        if (ac && !g_hm.count(ac) && is_epic_animal(ac)) {
            auto& b = g_epic_blows[ac];
            b.n++;
            b.last = dmg;
            b.max = std::max(b.max, dmg);
        }
        uint8_t ao = ac && g_hm.count(ac) ? g_hm[ac].owner : 0xFF;
        double now = H->time_seconds();
        if (ao < 0xFE && vt && vt != pt) { // our member hits another tribe
            auto& hits = g_pincer[vt];
            hits[ao] = now;
            int players = 0;
            for (auto& [o, t] : hits)
                if (now - t < kPincerWindow)
                    players++;
            if (players >= 2) {
                dmg *= 1.5f;
                g_pincer_hits++;
                if (now - g_pincer_told[vt] > 60 && C.notice) {
                    g_pincer_told[vt] = now;
                    C.notice("pincer attack: +50% damage");
                }
            }
            if (near_own_chief(ac, ao)) {
                dmg *= 1.2f;
                g_aura_hits++;
            }
        }
        // a chief about to fall next to a partner's member: the tribe protects it
        if (vt == pt && is_chief(victim)) {
            float hp = F<float>(victim, 0x5E0), mx = F<float>(victim, 0x5D4);
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
            mx = F<float>(pat, 0x5D4);
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
static void duet_tick(double now)
{
    if (now - g_last_duet < 0.5 || !a_SessionGet)
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
static bool is_epic_herd_of_ours(void* o)
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
// no epic of its own). `coop.fix tribe.epic_tuning off`: x1 as before.
// BUG-0563: the co-op epic took the species of the animal nearest the village: after the live Creature -> Tribe
// change that was the guest's own creature (its puppet of the Creature stage, species of the guest's creation, near
// the host's village where the guest stood): the host fought the guest's creature as an epic monster. The epic's
// species is never a puppet's, a proxy's or a player's creature. `coop.fix tribe.epic_species off`: the nearest
// animal (0.7.6).
static bool g_fix_epic_species = true;
COOP_FIX("tribe.epic_species", "BUG-0563", g_fix_epic_species, "tribe: the co-op epic is never a player's creature (nor a puppet of one)");
static uint32_t g_epic_skipped;
static bool g_fix_epic_tuning = true;
COOP_FIX("tribe.epic_tuning", "BUG-0515", g_fix_epic_tuning, "tribe: the co-op epic hits as hard as the game's own epics (its herd's damage multiplier)");
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

static bool spawn_epic()
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
    if (g_fix_epic_species)
        for (auto& pl : C.sess->players())
            if (void* sp = creations::player_species(pl.id))
                pick.players.push_back(sp);
    game::for_each_object(
        [](void* a, uint32_t noun, void* u) {
            auto* pk = (Pick*)u;
            if (noun != 0x18EB45E || !game::object_alive(a))
                return true;
            if (g_fix_epic_species && (game::is_puppet(a) || npc::is_proxy(a) ||
                                       std::find(pk->players.begin(), pk->players.end(), game::species_profile(a)) != pk->players.end())) {
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
    if (g_fix_epic_tuning)
        F<float>(herd, 0x158) = game_epic_damage_mult(); // mDamageMultiplier, BUG-0515
    else if (F<float>(herd, 0x158) > 1.f && is_epic_herd_of_ours(herd))
        F<float>(herd, 0x158) = 1.f; // a reused herd of ours: the old x1 (tests)
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
    g_epic_herd = herd;
    g_epic_hitters.clear();
    g_epics++;
    if (C.notice)
        C.notice("an epic creature comes! together: 80 food");
    return true;
}

// the epic is still an animal of the game's object list (BUG-0171: object_alive reads the
// creature's own memory and cannot tell a freed one; g_epic is a raw pointer kept for minutes)
static bool epic_listed()
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
    if (g_epic && (!epic_listed() || !game::object_alive(g_epic) || game::is_dead(g_epic))) {
        int food = g_epic_hitters.size() >= 2 ? 80 : 30;
        bonus_food(food);
        g_epic_kills++;
        if (C.notice)
            C.notice(std::string("epic creature down: +") + std::to_string(food) + " food");
        g_epic = nullptr;
        epic_herd_release();
        g_epic_next = now + 600;
    }
    if (!g_epic_next)
        g_epic_next = now + 480; // the first one after 8 minutes together
    if (!g_epic && now > g_epic_next && C.sess->players().size() >= 2) {
        g_epic_next = now + 600;
        spawn_epic();
    }
}

// the epic's hitters (animals go through the game's creature damage filter)
void note_animal_hit(void* animal, void* attacker_creature)
{
    if (animal && animal == g_epic && attacker_creature && g_hm.count(attacker_creature)) {
        uint8_t o = g_hm[attacker_creature].owner;
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
        if (g_place_still_fix)
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
        if (g_place_still_fix) {
            // a chief that walks, or walked since it was first looked at in this window, moves by its player's
            // orders: not a chief stacked at the arrival (seen: walked next to the host's chief and put 13.8 m away)
            if (drive::speed(game::velocity(b)) >= 0.3f || net::dist(game::position(b), g_place_first[b]) > 1.5f) {
                if (g_placed_now.insert(b).second)
                    g_place_walking++;
                continue;
            }
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
static uint32_t bt_node(void* c);
static bool g_grab_spot_fix = true;
COOP_FIX("tribe.grab_spot", "BUG-0505", g_grab_spot_fix,
         "tribe: a member that cannot reach its spot at a tool rack is set onto it and takes the tool");
constexpr uint32_t kGrabNode = 0x0462854D;
static uint32_t g_grab_spot_moves;
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
        game::teleport(c, goal, facing(goal, tp));
        g_grab_spot_moves++;
        g_grab_watch.erase(c);
        H->log(M, SK_INFO, "tribe: member #%u stood %.1f m short of its spot at a tool for 4 s: set onto it", member_id(c), d);
    }
    for (auto it = g_grab_watch.begin(); it != g_grab_watch.end();)
        it = seen.count(it->first) ? std::next(it) : g_grab_watch.erase(it);
}

static void coop_tick(double now)
{
    chiefs_tick();
    if (is_host()) {
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

// tests (BUG-0672): `coop.tribe frametool <tribe index> <type> <metres>`: the host's tool made in this game's tribe of
// that index the way the wobj tool kind makes it (make_tool), from the next frame's tick (inside SporeKit's frame, as the
// wobj snapshot runs), with a task for outside the frame queued just before (as one often is)
struct FrameTool {
    bool armed = false;
    int ix = 0, type = 0;
    float metres = 10.f;
};
static FrameTool g_frame_tool;
static void frame_tool_run()
{
    FrameTool f = g_frame_tool;
    g_frame_tool.armed = false;
    void* t = f.ix >= 0 && f.ix < (int)g_local_tribe_of.size() ? g_local_tribe_of[f.ix] : nullptr;
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

void tick(double now)
{
    if (g_frame_tool.armed && is_guest())
        frame_tool_run();
    if (!active())
        return;
    drop_foreign_selection();
    if (is_host())
        host_tick(now);
    else if (is_guest())
        guest_tick(now);
    coop_tick(now);
}

static uint32_t g_rx, g_rx_skipped, g_resets;

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
    for (auto& [c, t] : g_detached) // BUG-0667: the bodies stay in this world; the game tears them down later
        if (game::object_alive(c))
            restore_body_tribe(c, t);
    g_detached.clear();
    g_bt_local.clear(); // (g_left_tribe stays until the world goes: its citizens are torn down later)
    theme_forget(); // BUG-0568
    party_forget(); // BUG-0646
    baby_forget(); // BUG-0671
    clear_coop_tables(true);
    g_split_for.clear();
    g_hm.clear();
    g_hm_by_id.clear();
    g_gm.clear();
    g_gid_of.clear();
    g_npc_tribe_made.clear(); // BUG-0381
    g_npc_tribe_tried.clear();
    g_player_members_seen = 0;
    if (g_world_map_fix)
        g_local_tribe_of.clear(); // BUG-0672: the old world's tribes are freed; the next tribe state maps the new ones
}

// a world of the Tribe stage is gone (a load, a received world, the stage left, the game-over
// bounce back): every pointer of it is forgotten, with no game call (BUG-0217: no longer through
// on_session_end, whose restoring clear would touch the old world if the order ever changed)
void on_world_change()
{
    theme_forget(); // BUG-0568
    party_forget(); // BUG-0646
    baby_forget(); // BUG-0671
    g_resets++;
    forget_world_tables(); // every citizen of the old world is freed: nothing to restore
    g_detached.clear(); // (BUG-0667: freed with the world)
    g_left_tribe.clear();
    g_bt_local.clear();
    g_split_for.clear();
    g_hm.clear();
    g_hm_by_id.clear();
    g_gm.clear();
    g_gid_of.clear();
    g_npc_tribe_made.clear(); // BUG-0381
    g_npc_tribe_tried.clear();
    g_player_members_seen = 0;
}

// BUG-0217: which Tribe world is open. Before, any frame without the stage's local entity (every
// editor: no adapter) counted as a world change, so an editor visit dropped the live co-op epic
// untracked into the share save that follows. Now: the stage counter (game::stage_epoch: a load,
// a received world, another stage), leaving Tribe for a mode that is not an editor, and the
// in-stage signals (another player tribe object, another save name). Limit: a reload of the same
// save that reuses the player tribe's address and never shows a non-stage frame is not seen.
// `coop.fix tribe.editor_world off`: the old trigger (coop.cpp avatar_change_check).
static bool g_fix_editor_world = true;
COOP_FIX("tribe.editor_world", "BUG-0217", g_fix_editor_world, "tribe: an editor visit keeps the world (the co-op epic stays tracked)");
static stage_epoch::Watch g_world_watch;
static bool g_world_left = true; // the Tribe world is not open (forgotten when it was left)
static uint32_t g_world_checks_fired;

bool editor_world_fix() { return g_fix_editor_world; }

void world_check()
{
    if (!g_fix_editor_world) {
        g_world_watch = {};
        g_world_left = true;
        return;
    }
    uint32_t m = game::mode_id();
    bool in = m == modes::kTribe;
    bool editor = m == modes::kEdit || m == modes::kEditor;
    void* pt = in && g_ok ? player_tribe() : nullptr;
    bool ch = stage_epoch::changed(g_world_watch, game::stage_epoch(modes::kTribe), in, pt,
                                   in ? modes::current_world() : 0);
    bool fire = false;
    if (!in && !editor) { // the Tribe world is gone (menu, load, another stage)
        fire = !g_world_left;
        g_world_left = true;
    } else if (in) {
        fire = ch && !g_world_left; // a new world while one was open (in-stage signals)
        g_world_left = false;
    }
    if (fire) {
        g_world_checks_fired++;
        if (g_ok)
            H->log(M, SK_INFO, "tribe: the world changed (mode %08X, epoch %u)", m, game::stage_epoch(modes::kTribe));
        on_world_change();
    }
}

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
        for (auto& [c, hm] : g_hm)
            if (hm.owner < 0xFE && game::object_alive(c))
                fn({hm.id, hm.owner, c, nullptr, drive::speed(game::velocity(c)), 0, nullptr}, user);
        return;
    }
    double now = H->time_seconds();
    for (auto& [id, g] : g_gm) {
        if (g.owner >= 0xFE || !g.c || !game::object_alive(g.c) || now - g.seen > 3.0)
            continue;
        fn({id, g.owner, g.c, &g.aim, g.aim_speed, (uint16_t)g.ds.teleports, &g.ds.drawn}, user);
    }
}

std::string status()
{
    char b[1024];
    if (C.sess && C.sess->role() == net::Role::Host)
        snprintf(b, sizeof b, "tribe(host): members=%u cmds_done=%u rejected=%u gifts=%u unselected=%u bodies_sent=%u\n",
                 (unsigned)g_hm.size(), g_cmds_done, g_cmds_rejected, g_gifts, g_unselected, g_bodies_sent);
    else
        snprintf(b, sizeof b,
                 "tribe(guest): mirrors=%u spawned=%u removed=%u cmds_sent=%u rx=%u skipped=%u resets=%u "
                 "local_tribes=%u player_tribe=%p unselected=%u incomplete=%u older_chunks=%u ai_skipped=%u/%u away_orders=%u local=%d mirror_walks=%u timed=%u "
                 "npc_tribes_made=%u failed=%u mirror_grounded=%u ours=%u/%u save_stripped=%u/%u forms_blocked=%u forms_run=%u unmapped_removed=%u bodies_detached=%u teardown_restored=%u slot_null_skipped=%u teardown_tribe_back=%u teardown_bt_emptied=%u map_stale_skipped=%u\n",
                 (unsigned)g_gm.size(), g_spawned, g_removed, g_cmds_sent, g_rx, g_rx_skipped, g_resets,
                 (unsigned)all_tribes().size(), player_tribe(), g_unselected, g_incomplete, g_cyc.older, g_bt_skipped, g_bt_calls, g_away_orders, (int)local_play(),
                 g_mirror_walks, g_mirror_timed, g_npc_tribes_made, g_npc_tribes_failed, g_mirror_grounded, (unsigned)g_ours_c.size(),
                 (unsigned)g_ours_t.size(), g_save_stripped, g_save_saves, g_forms_blocked, g_forms_run, g_unmapped_removed, g_bodies_detached, g_teardown_restored, g_slot_null_skipped, g_teardown_tribe_back,
                 g_teardown_bt_emptied, g_map_stale_skipped);
    std::string out = b;
    if (!g_last_cmd.empty())
        out += "tribe last order: " + g_last_cmd + "\n";
    out += "tribe ui mode=" + std::to_string(ui_mode()) + " (0 social, 1 attack)\n";
    if (!g_last_local.empty())
        out += "tribe last local order: #" + std::to_string(g_local_orders) + " " + g_last_local + "\n";
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
    for (auto& [o, c] : g_chief) {
        auto it = g_chief_scale.find(c);
        char sb[32];
        snprintf(sb, sizeof sb, " %u:%.2f", o, it != g_chief_scale.end() && it->second > 0 ? game::scale(c) / it->second : 0.f);
        scales += sb;
    }
    snprintf(b, sizeof b,
             "tribe lifetime: chief_restores=%u chief_gone=%u chief_bad_calls=%u spawns_blocked=%u mirror_out_passed=%u fix_chiefs=%d "
             "fix_gameover=%d knocked_out=%u ko_replaced=%u epic_herd_reused=%u epic_saved_out=%u chief_scale=%s\n",
             g_chief_restores, g_chief_gone, g_chief_bad_calls, g_spawns_blocked, g_mirror_out_passed, (int)g_fix_chiefs, (int)g_fix_gameover,
             g_ko_shown, g_ko_replaced, g_epic_herd_reused, g_epic_saved_out, scales.c_str());
    return out + b;
}

// ---- debug (tests): orders through the same hooked functions the UI uses ----
static bool g_fix_act_check = true;
COOP_FIX("tribe.act_check", "BUG-0260", g_fix_act_check, "tribe: coop.tribe act refuses a tool / hut action on a member (tests)");
static bool act_needs_object(int action) { return action == 3 || action == 12 || action == 15 || action == 24; }

static void* local_of(uint16_t id)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        auto it = g_hm_by_id.find(id);
        return it == g_hm_by_id.end() ? nullptr : it->second;
    }
    auto it = g_gm.find(id);
    return it == g_gm.end() ? nullptr : it->second.c;
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

std::string debug(const std::vector<std::string>& a)
{
    // tests of the co-op mechanics through the game's damage path (host): ids as in coop.tribe
    auto hit = [](void* victim, void* attacker, float dmg) {
        float before = F<float>(victim, 0x5E0);
        // a dead victim takes no more blows: the test loop went on hitting the dying epic (BUG-0171)
        if (before <= 0 || game::is_dead(victim))
            return std::string("hp 0.00 -> 0.00 (dead)");
        // the combatant's TakeDamage (vt 0x18): the same path as a real blow, with our hooks
        // (the push vector: (0,0,0) as the game's own bites and strikes, docs/re/creature.md 3)
        static const float zero[3] = {0, 0, 0}; // the hit direction must not be null
        vcall<int>((uint8_t*)victim + 0x5A8, 0x18 / 4, dmg, 0xFFFFFFFFu, 0, zero,
                   attacker ? (uint8_t*)attacker + 0x5A8 : nullptr);
        char b[64];
        snprintf(b, sizeof b, "hp %.2f -> %.2f", before, F<float>(victim, 0x5E0));
        return std::string(b);
    };
    if (a.size() >= 3 && a[0] == "fix") { // fix chiefs|gameover 0|1: the code before BUG-0081 / BUG-0172 (tests)
        bool on = atoi(a[2].c_str()) != 0;
        if (a[1] == "chiefs")
            g_fix_chiefs = on;
        else if (a[1] == "gameover")
            g_fix_gameover = on;
        else if (a[1] == "ko")
            g_fix_ko = on;
        else if (a[1] == "epic_players")
            g_fix_epic_players = on;
        else if (a[1] == "epic_herd")
            g_fix_epic_herd = on;
        else if (a[1] == "away")
            g_fix_away = on;
        else
            return "usage: coop.tribe fix chiefs|gameover|ko|epic_players|epic_herd|away 0|1";
        return "fix " + a[1] + " " + (on ? "on" : "off");
    }
    if (a.size() >= 2 && a[0] == "ring") { // ring <id>: the HUD shows the target ring under member / NPC creature <id>
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        char b[96];
        snprintf(b, sizeof b, "ring=%d drawn=%u order=%p", (int)ring_shown(c), g_rings_drawn, g_ring.target);
        return c ? std::string(b) : std::string("no such member");
    }
    if (!a.empty() && a[0] == "advance") {
        // tests (BUG-0576): the goal bar full (cPlayer +0x10F0 = +0x10F4), then the message 0x5CC2589 the stage's
        // listener 0xCD81F0 takes after a goal: with the bar full it shows the HUD's advance button (window 0x6454B47);
        // the test clicks it as a player (TRG2CVG_PreEditor, then the City Hall editor, docs/re/tribe.md#tribe-civ-transition)
        uintptr_t nmg = H->sym("Simulator::cGameNounManager::Get"), get = H->sym("App::IMessageManager::Get");
        void* nm = nmg ? skg::cdecl_call<void*>(nmg) : nullptr;
        void* p = nm && skg::readable((uint8_t*)nm + 0x74, 4) ? F<void*>(nm, 0x74) : nullptr;
        void* mm = get ? skg::cdecl_call<void*>(get) : nullptr;
        if (game::mode_id() != modes::kTribe || !p || !skg::readable((uint8_t*)p + 0x10F0, 8) || !mm)
            return "not possible here";
        float& done = *(float*)((uint8_t*)p + 0x10F0);
        float need = F<float>(p, 0x10F4);
        if (need < 1)
            need = 1, *(float*)((uint8_t*)p + 0x10F4) = need;
        done = need;
        skg::vcall<void>(mm, 0x18 / 4, 0x5CC2589u, (void*)nullptr, (void*)nullptr, 0); // MessagePost
        char b[96];
        snprintf(b, sizeof b, "goal %.0f / %.0f, goal message posted", done, need);
        return b;
    }
    if (a.size() >= 2 && a[0] == "drop_chief") { // drop_chief <owner>: the chief leaves the tribe at once
        // (RemoveMember(c, destroy) as guest_state does for a mirror the host no longer sends):
        // BUG-0081, the next chiefs_tick must not call into it
        auto it = g_chief.find((uint8_t)atoi(a[1].c_str()));
        if (it == g_chief.end() || !it->second)
            return "no chief";
        void* c = it->second;
        uint16_t id = member_id(c);
        remove_member(c);
        return "dropped chief #" + std::to_string(id);
    }
    if (a.size() >= 2 && a[0] == "stale_chief") { // stale_chief <id>: as if a former chief's freed block
        // now held this member (another tribe's citizen): the next chiefs_tick must leave it alone
        // (BUG-0081; before the fix it got the old chief's size, 2.0 here)
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such member";
        g_chief_scale[c] = 2.f;
        return "stale chief entry for #" + a[1];
    }
    if (a.size() >= 2 && a[0] == "scale") { // scale <id>: the member's size in the game (spatial GetScale)
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        char b[64];
        snprintf(b, sizeof b, "scale %.3f", c ? game::scale(c) : 0.f);
        return c ? std::string(b) : std::string("no such member");
    }
    if (a.size() >= 4 && a[0] == "pincer" && is_host()) { // pincer <victim id> <attacker id> <dmg>
        void* v = local_of((uint16_t)atoi(a[1].c_str())), *at = local_of((uint16_t)atoi(a[2].c_str()));
        if (!v || !at)
            return "no such member";
        return hit(v, at, (float)atof(a[3].c_str()));
    }
    if (!a.empty() && a[0] == "scales") { // the player tribe's members: size in the game / own size (BUG-0067)
        void* pt = game::mode_id() == modes::kTribe ? player_tribe() : nullptr;
        int n = 0, big = 0;
        float mx = 0;
        std::string list;
        for (void* c : pt ? members(pt) : std::vector<void*>{}) {
            float own = vcall<float>(c, 0x80 / 4, 0); // GetDesiredScale(int)
            if (own <= 0.01f || own > 10.f)
                continue;
            float r = game::scale(c) / own;
            n++;
            big += r > 1.15f;
            mx = std::max(mx, r);
            char e[48];
            snprintf(e, sizeof e, " %u:%.2f/%.2f", (unsigned)member_id(c), game::scale(c), own);
            list += e; // id:scale in the game/own size (tests, BUG-0152)
        }
        char b[96];
        snprintf(b, sizeof b, "scales members=%d big=%d max_ratio=%.2f", n, big, mx);
        return b + list;
    }
    if (a.size() >= 2 && a[0] == "epic" && a[1] == "herds") { // the game's epic herds and epic creatures (BUG-0121)
        struct Cnt {
            uintptr_t is_epic;
            int herds = 0, ours = 0, enabled = 0, epics = 0;
        } n{H->sym("SporeCoop::cCreatureAnimal_IsEpic")};
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                auto* n = (Cnt*)u;
                if (!game::object_alive(o))
                    return true;
                if (noun == game::kHerdNoun && skg::readable(o, 0x21C) && F<uint32_t>(o, 0x88) == kEpicArchetype) {
                    n->herds++;
                    n->ours += is_epic_herd_of_ours(o);
                    n->enabled += F<uint8_t>(o, 0x218) != 0;
                } else if (noun == 0x18EB45E && n->is_epic && !game::is_dead(o) && thiscall<bool>(n->is_epic, o))
                    n->epics++;
                return true;
            },
            &n);
        char b[192];
        snprintf(b, sizeof b, "epic herds=%d quiet_ours=%d enabled=%d live_epics=%d tracked=%p resets=%u world_changes=%u epoch=%u",
                 n.herds, n.ours, n.enabled, n.epics, g_epic, g_resets, g_world_checks_fired, game::stage_epoch(modes::kTribe));
        return b;
    }
    if (a.size() >= 2 && a[0] == "epic" && a[1] == "stats") { // BUG-0515: every live epic, its herd's tuning, its blows
        struct U {
            uintptr_t is_epic;
            std::string s;
        } u{H->sym("SporeCoop::cCreatureAnimal_IsEpic"), {}};
        game::for_each_object(
            [](void* o, uint32_t noun, void* up) {
                auto* u = (U*)up;
                if (noun != 0x18EB45E || !game::object_alive(o) || game::is_dead(o) || !u->is_epic || !thiscall<bool>(u->is_epic, o))
                    return true;
                void* h = game::herd_of(o);
                bool hr = h && skg::readable(h, 0x21C);
                auto& d = g_epic_blows[o];
                char b[360];
                snprintf(b, sizeof b,
                         "epic %p ours=%d hp=%.0f/%.0f scale=%.2f herd=%p arch=%08X gen=%d scale_mult=%.2f hp_override=%.0f dmg_mult=%.2f "
                         "blows=%u last=%.1f max=%.1f\n",
                         o, (int)(o == g_epic), game::health(o), game::max_health(o), game::scale(o), h, hr ? F<uint32_t>(h, 0x88) : 0,
                         hr ? F<int>(h, 0xF0) : -1, hr ? F<float>(h, 0x150) : 0.f, hr ? F<float>(h, 0x154) : 0.f, hr ? F<float>(h, 0x158) : 0.f,
                         d.n, d.last, d.max);
                u->s += b;
                return true;
            },
            &u);
        return u.s + "end";
    }
    if (a.size() >= 3 && a[0] == "epic" && a[1] == "near" && is_host()) { // epic near <member id>: the co-op epic next to it
        void* c = local_of((uint16_t)atoi(a[2].c_str()));
        if (!g_epic || !c || !epic_listed())
            return "no epic / member";
        net::Vec3 p = game::position(c);
        p.x += 6.f;
        game::teleport(g_epic, p, game::orientation(g_epic));
        return "epic moved next to #" + a[2];
    }
    if (a.size() >= 2 && a[0] == "epic" && a[1] == "quiet") { // RE: quiet every epic herd of ours now
        std::string out;
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                if (noun == game::kHerdNoun && is_epic_herd_of_ours(o)) {
                    char b[96];
                    uint8_t e0 = F<uint8_t>(o, 0x218);
                    game::herd_quiet(o);
                    snprintf(b, sizeof b, "herd %p enabled %d -> %d (ours %p)\n", o, e0, F<uint8_t>(o, 0x218), g_epic_herd);
                    *(std::string*)u += b;
                }
                return true;
            },
            &out);
        return out.empty() ? "no herd of ours" : out;
    }
    if (a.size() >= 2 && a[0] == "epic" && a[1] == "save") { // as if the game saved now (BUG-0121)
        before_save();
        return "before_save done";
    }
    if (!a.empty() && a[0] == "epic" && is_host()) {
        if (a.size() >= 3 && a[1] == "hit") { // epic hit <attacker id> <dmg>
            void* at = local_of((uint16_t)atoi(a[2].c_str()));
            if (!g_epic || !at || !epic_listed())
                return "no epic / member";
            return hit(g_epic, at, a.size() > 3 ? (float)atof(a[3].c_str()) : 50.f);
        }
        bool ok = spawn_epic();
        uintptr_t is_epic = H->sym("SporeCoop::cCreatureAnimal_IsEpic");
        char b[200];
        // BUG-0563 (tests): the epic's species, and whether it is a player's creature (any player of the session)
        void* sp = g_epic ? game::species_profile(g_epic) : nullptr;
        int players = 0;
        for (auto& pl : C.sess->players())
            players += sp && creations::player_species(pl.id) == sp;
        snprintf(b, sizeof b, "epic %s %p is_epic=%d hp=%.0f species=%08X player_species=%d skipped=%u", ok ? "spawned" : "not spawned", g_epic,
                 g_epic && is_epic ? (int)thiscall<bool>(is_epic, g_epic) : -1, g_epic ? F<float>(g_epic, 0x5E0) : 0.f,
                 g_epic ? game::species_key(g_epic).instance : 0u, players, g_epic_skipped);
        return b;
    }
    if (a.size() >= 3 && a[0] == "select") { // select <id> <0|1>
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such member";
        vcall<int>((uint8_t*)c + 0xC0, 0x54 / 4, atoi(a[2].c_str()));
        return std::string("selected=") + std::to_string(F<uint8_t>(c, kSelectedOff));
    }
    if (a.size() >= 3 && a[0] == "select_own") { // select_own <n> <0|1>: the player tribe's n-th member (tests:
        // a guest in a world of its own has no shared ids, BUG-0231)
        void* pt = player_tribe();
        auto ms = pt ? members(pt) : std::vector<void*>{};
        size_t n = (size_t)atoi(a[1].c_str());
        if (n >= ms.size())
            return "no such member";
        vcall<int>((uint8_t*)ms[n] + 0xC0, 0x54 / 4, atoi(a[2].c_str()));
        return std::string("selected=") + std::to_string(F<uint8_t>(ms[n], kSelectedOff));
    }
    if (a.size() >= 4 && a[0] == "walk") { // walk <x> <y> <z> : the selected members walk there
        float p[3] = {(float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str())};
        thiscall<int>(a_WalkSelected, (void*)nullptr, (const float*)p);
        return "walk ordered";
    }
    if (!a.empty() && a[0] == "tools" && (a.size() < 2 || a[1] != "all")) { // BUG-0514: the player tribe's tools, and what the own members hold
        std::string s;
        struct U {
            std::string* s;
            int i;
            net::Vec3 c;
        } u{&s, 0, {}};
        center(u.c);
        for_each_player_tool(
            [](void* t, int type, const net::Vec3& p, void* up) {
                auto* u = (U*)up;
                char b[160];
                snprintf(b, sizeof b, "tool %d %p serial=%u type=%d pos=(%.2f %.2f %.2f) dist=%.1f\n", u->i++, t, game::object_serial(t), type,
                         p.x, p.y, p.z, net::dist(p, u->c));
                *u->s += b;
            },
            &u);
        void* pt = player_tribe();
        for (void* c : pt ? members(pt) : std::vector<void*>{}) {
            uint16_t id = member_id(c);
            char b[96];
            snprintf(b, sizeof b, "held %u owner=%u tool=%d leader=%d\n", id, owner_of(c), F<int>(c, kHeldToolOff),
                     (int)is_leader(c));
            s += b;
        }
        char b[192];
        snprintf(b, sizeof b, "tools n=%d tool_sets=%u hold_fix=%d grab_spot=%d moves=%u leader_kept=%u\n", u.i, g_tool_sets,
                 (int)g_tool_hold_fix, (int)g_grab_spot_fix, g_grab_spot_moves, g_leader_tools_kept);
        return s + b;
    }
    if (a.size() >= 2 && a[0] == "build" && is_host()) { // build <type> [metres]: test setup, a tool as if bought
        void* pt = player_tribe();
        net::Vec3 c;
        if (!pt || !center(c))
            return "no player tribe";
        float d = a.size() >= 3 ? (float)atof(a[2].c_str()) : 10.f;
        float l = std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
        if (l < 1.f)
            return "no player tribe";
        net::Vec3 n{c.x / l, c.y / l, c.z / l};
        // a tangent: up x (an axis away from up)
        net::Vec3 ax = std::fabs(n.z) < 0.9f ? net::Vec3{0, 0, 1} : net::Vec3{1, 0, 0};
        net::Vec3 t{n.y * ax.z - n.z * ax.y, n.z * ax.x - n.x * ax.z, n.x * ax.y - n.y * ax.x};
        float tl = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
        net::Vec3 p{c.x + t.x / tl * d, c.y + t.y / tl * d, c.z + t.z / tl * d};
        float g = game::ground_radius(p), pl = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (g > 1.f && pl > 1.f)
            p = {p.x / pl * g, p.y / pl * g, p.z / pl * g};
        void* tool = make_player_tool(atoi(a[1].c_str()), p);
        char b[128];
        snprintf(b, sizeof b, "built %p type=%s at (%.2f %.2f %.2f)", tool, a[1].c_str(), p.x, p.y, p.z);
        return tool ? std::string(b) : std::string("build failed");
    }
    // BUG-0523 / BUG-0524 (tests): every tribe's tools with the host's index of their tribe
    if (a.size() >= 2 && a[0] == "tools" && a[1] == "all") {
        std::string s;
        for_each_tool(
            true,
            [](void* t, int type, int ix, bool player, const net::Vec3& p, void* up) {
                char b[160];
                snprintf(b, sizeof b, "atool %p serial=%u tribe=%d player=%d type=%d pos=(%.2f %.2f %.2f)\n", t, game::object_serial(t), ix,
                         (int)player, type, p.x, p.y, p.z);
                *(std::string*)up += b;
            },
            &s);
        char b[96];
        snprintf(b, sizeof b, "tribes=%u mapped=%u bought=%u\n", (unsigned)all_tribes().size(), (unsigned)g_local_tribe_of.size(), g_tools_bought);
        return s + b;
    }
    // place <tribe index | p> <type> <metres> [degrees]: test setup, a tool for a tribe of this game (the host's index;
    // p the player tribe) that far from the tribe's centre, as a tribe's purchase makes it (no drag)
    // buy <type> <metres> [degrees]: the palette's placement in this game's player tribe (0xD13C05): the old tool of
    // the type goes, the new one is made and dragged (spatial +0x72) until `buy_end`, which sets the purchased bit
    if (a.size() >= 4 && (a[0] == "place" || a[0] == "buy")) {
        bool buy = a[0] == "buy";
        size_t o = buy ? 0 : 1;
        void* t = buy || a[1] == "p" ? player_tribe() : tribe_of_index(atoi(a[1].c_str()));
        if (!t)
            return "no such tribe";
        int type = atoi(a[1 + o].c_str());
        float d = (float)atof(a[2 + o].c_str()), deg = a.size() > 3 + o ? (float)atof(a[3 + o].c_str()) : 0.f;
        net::Vec3 c = tribe_pos(t);
        float l = std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
        if (l < 1.f)
            return "no tribe position";
        net::Vec3 n{c.x / l, c.y / l, c.z / l};
        net::Vec3 ax = std::fabs(n.z) < 0.9f ? net::Vec3{0, 0, 1} : net::Vec3{1, 0, 0};
        net::Vec3 u{n.y * ax.z - n.z * ax.y, n.z * ax.x - n.x * ax.z, n.x * ax.y - n.y * ax.x};
        float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
        u = {u.x / ul, u.y / ul, u.z / ul};
        net::Vec3 w{n.y * u.z - n.z * u.y, n.z * u.x - n.x * u.z, n.x * u.y - n.y * u.x};
        float ca = std::cos(deg * 3.14159265f / 180.f), sa = std::sin(deg * 3.14159265f / 180.f);
        net::Vec3 p{c.x + (u.x * ca + w.x * sa) * d, c.y + (u.y * ca + w.y * sa) * d, c.z + (u.z * ca + w.z * sa) * d};
        float g = game::ground_radius(p), pl = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (g > 1.f && pl > 1.f)
            p = {p.x / pl * g, p.y / pl * g, p.z / pl * g};
        void* tool = nullptr;
        if (buy) {
            std::vector<void*> old;
            for (void* x : tribe_tools(t))
                if (tool_type(x) == type)
                    old.push_back(x);
            tool = vcall<void*>(t, 0xB4 / 4, type);
            if (tool) {
                F<uint32_t>(tool, 0x84) |= 8;
                vcall<void>(tool, 0x64 / 4, t);
                thiscall<void>(a_ToolModelReset, tool);
                F<uint8_t>(tool, kToolDragOff) = 1;
                void* sp = game::cast(tool, 0x1186577);
                if (sp) {
                    float pp[3] = {p.x, p.y, p.z};
                    net::Quat r = upright(p);
                    float q[4] = {r.x, r.y, r.z, r.w};
                    vcall<void>(sp, 0x38 / 4, (const float*)pp);
                    vcall<void>(sp, 0x3C / 4, (const float*)q);
                }
                for (void* x : old)
                    vcall<void>(t, 0xB8 / 4, x);
                g_test_dragged = tool;
            }
        } else
            tool = make_tool_in(t, type, p);
        char b[160];
        snprintf(b, sizeof b, "%s %p type=%d at (%.2f %.2f %.2f)", buy ? "dragging" : "placed", tool, type, p.x, p.y, p.z);
        return tool ? std::string(b) : std::string("failed");
    }
    if (!a.empty() && a[0] == "buy_end") { // the end of `buy`'s drag: placed, the purchased bit (0xD10F90)
        void* pt = player_tribe();
        void* tool = g_test_dragged;
        g_test_dragged = nullptr;
        auto ts = tribe_tools(pt);
        if (!tool || std::find(ts.begin(), ts.end(), tool) == ts.end())
            return "nothing dragged";
        F<uint8_t>(tool, kToolDragOff) = 0;
        thiscall<void>(a_SetToolPurchased, pt, tool_type(tool));
        net::Vec3 p;
        game::object_position(tool, p);
        char b[128];
        snprintf(b, sizeof b, "placed %p type=%d at (%.2f %.2f %.2f)", tool, tool_type(tool), p.x, p.y, p.z);
        return b;
    }
    if (a.size() >= 2 && a[0] == "layout" && a_ToolLayout) { // layout <tool type>: the tribe's grab spots of a type
        void* pt = player_tribe();
        if (!pt)
            return "no player tribe";
        int type = atoi(a[1].c_str());
        uint8_t* l = thiscall<uint8_t*>(a_ToolLayout, (uint8_t*)pt + 0x394, &type);
        if (!l || !skg::readable(l, 0x58))
            return "no layout";
        uint8_t *sb = *(uint8_t**)(l + 0x50), *se = *(uint8_t**)(l + 0x54);
        char b[160];
        snprintf(b, sizeof b, "layout type=%d at=(%.1f %.1f %.1f) slots=%d\n", type, *(float*)(l + 0x14), *(float*)(l + 0x18),
                 *(float*)(l + 0x1C), sb && se > sb ? (int)((se - sb) / 0x18) : 0);
        std::string s = b;
        for (uint8_t* q = sb; q && q < se && skg::readable(q, 0x18); q += 0x18) {
            void* o = *(void**)(q + 4);
            snprintf(b, sizeof b, "  slot used=%u obj=%p member=%d pos=(%.1f %.1f %.1f)\n", q[0], o,
                     is_citizen(o) ? (int)member_id(o) : -1, *(float*)(q + 0xC), *(float*)(q + 0x10), *(float*)(q + 0x14));
            s += b;
        }
        return s;
    }
    if (a.size() >= 5 && a[0] == "put" && is_host()) { // put <member id> <x> <y> <z>: test setup (BUG-0505), on the ground
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such member";
        net::Vec3 p{(float)atof(a[2].c_str()), (float)atof(a[3].c_str()), (float)atof(a[4].c_str())};
        float g = game::ground_radius(p), pl = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (g > 1.f && pl > 1.f)
            p = {p.x / pl * g, p.y / pl * g, p.z / pl * g};
        game::teleport(c, p, game::orientation(c));
        char b[96];
        snprintf(b, sizeof b, "put #%s at (%.1f %.1f %.1f)", a[1].c_str(), p.x, p.y, p.z);
        return b;
    }
    // order <member id> <action> <x> <y> <z>: test setup (BUG-0531, host), the member's DoAction as an order gives it:
    // 4 walks there, 2 gathers the tree there, others take the object of this game nearest there (8 m)
    if (a.size() >= 6 && a[0] == "order" && is_host()) {
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        int action = atoi(a[2].c_str());
        net::Vec3 p{(float)atof(a[3].c_str()), (float)atof(a[4].c_str()), (float)atof(a[5].c_str())};
        if (!c)
            return "no such member";
        if (action == 4) {
            void* rec = thiscall<void*>(a_DoAction, c, 4, (void*)nullptr, (void*)nullptr);
            if (rec)
                thiscall<void>(a_SetValueVector3, (uint8_t*)rec + 0x39, (const float*)&p);
            return "walk ordered";
        }
        void* tgt = action == 2 ? find_tree(p, 3.f) : nullptr;
        if (!tgt)
            return "no target there";
        thiscall<void*>(a_DoAction, c, action, tgt, (void*)nullptr);
        return "ordered " + std::to_string(action);
    }
    if (a.size() >= 2 && a[0] == "who") { // who <citizen pointer>: BUG-0620, what a citizen of the object list is
        void* c = (void*)(uintptr_t)strtoul(a[1].c_str(), nullptr, 16);
        bool listed = false;
        game::for_each_object(
            [](void* o, uint32_t, void* u) {
                if (o == *(void**)((void**)u)[0])
                    *(bool*)((void**)u)[1] = true;
                return true;
            },
            (void*)(void* []){&c, &listed});
        if (!listed || !is_citizen(c))
            return "not a citizen of the object list";
        void* t = F<void*>(c, 0x1014);
        char b[200];
        int in_members = -1, n_tribes = 0, in_any = 0;
        for (void* tt : all_tribes()) {
            n_tribes++;
            auto m = members(tt);
            bool in = std::find(m.begin(), m.end(), c) != m.end();
            in_any += in;
            if (tt == t)
                in_members = in;
        }
        uint8_t ow = owner_of(c);
        snprintf(b, sizeof b, "tribe=%p (a tribe of the list: %s, in its members: %d) in any tribe's members: %d of %d tribes; "
                 "player tribe=%p; mod owner=%d id=%u; age=%u dead=%u species=%08X city=%p; ",
                 t, in_members >= 0 ? "yes" : "no", in_members, in_any, n_tribes, player_tribe(), ow == 0xFF ? -1 : ow, id_of(c),
                 F<uint8_t>(c, 0xB34), F<uint8_t>(c, 0xB5E), F<uint32_t>(c, 0xB28), F<void*>(c, 0x1010));
        return std::string(b) + ai_line(c);
    }
    if (a.size() >= 3 && a[0] == "frametool" && is_guest()) {
        g_frame_tool.ix = atoi(a[1].c_str());
        g_frame_tool.type = atoi(a[2].c_str());
        g_frame_tool.metres = a.size() >= 4 ? (float)atof(a[3].c_str()) : 10.f;
        g_frame_tool.armed = true;
        char b[96];
        snprintf(b, sizeof b, "frametool armed: tribe %d type %d (mapped=%u)", g_frame_tool.ix, g_frame_tool.type,
                 (unsigned)g_local_tribe_of.size());
        return b;
    }
    if (a.size() >= 2 && a[0] == "statehold" && is_guest()) { // statehold 1|0: tests (BUG-0672), the host's tribe
        // states are ignored (the mapping of host tribe index -> this game's tribe stays as it is, as before the first
        // state of a new world)
        g_state_hold = atoi(a[1].c_str()) != 0;
        char b[96];
        snprintf(b, sizeof b, "tribe states %s; mapped=%u stale_skipped=%u map=", g_state_hold ? "held" : "taken",
                 (unsigned)g_local_tribe_of.size(), g_map_stale_skipped);
        std::string out = b;
        void* pt = player_tribe();
        for (size_t i = 0; i < g_local_tribe_of.size() && i < 32; i++) {
            snprintf(b, sizeof b, "%u:%p%s ", (unsigned)i, g_local_tribe_of[i], g_local_tribe_of[i] && g_local_tribe_of[i] == pt ? "(p)" : "");
            out += b;
        }
        return out;
    }
    if (a.size() >= 3 && a[0] == "localbt" && is_guest()) { // localbt <member id> 1|0: tests (BUG-0667), that mirror's
        // behaviour tree runs on this guest as while the host is away (local_play), or is skipped again
        void* c = member_mirror((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such mirror";
        if (atoi(a[2].c_str()))
            g_bt_local.insert(c);
        else
            g_bt_local.erase(c);
        return std::string("mirror's tree ") + (g_bt_local.count(c) ? "runs" : "skipped");
    }
    if (a.size() >= 2 && a[0] == "eat" && is_guest()) { // eat <member id>: tests (BUG-0667), the mirror's own Eat
        // (CitizenAction 0) at its tribe's food mat (+0x260) in this game, as a click on the mat gives it
        void* c = member_mirror((uint16_t)atoi(a[1].c_str()));
        void* t = c ? F<void*>(c, kOwnerTribeOff) : nullptr;
        void* mat = t ? F<void*>(t, 0x260) : nullptr;
        if (!mat)
            return "no such mirror or no food mat";
        void* rec = thiscall<void*>(a_DoAction, c, 0, mat, (void*)nullptr);
        return std::string("eat ordered (rec ") + (rec ? "made" : "none") + ")";
    }
    if (a.size() >= 2 && a[0] == "ai") { // ai <member id>: BUG-0505, the member's order record, node and walk
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        return c ? ai_line(c) : "no such member";
    }
    if (a.size() >= 3 && a[0] == "applytool" && is_guest()) { // applytool <member id> <type>: tests (BUG-0583), the
        // mirror's tool field applied as if the host's member held that tool (tribe.tool_hold's own apply)
        void* c = member_mirror((uint16_t)atoi(a[1].c_str()));
        if (!c)
            return "no such mirror";
        apply_held_tool(c, (float)atoi(a[2].c_str()));
        return "tool " + std::to_string(F<int>(c, kHeldToolOff)) + " leader=" + std::to_string((int)is_leader(c));
    }
    if (a.size() >= 2 && a[0] == "chief" && is_guest()) { // chief <member id>: tests (BUG-0652), this game's
        // cTribe::SetChieftain on the mirror (the state a load or a respawned mirror left: another leader here)
        void* c = member_mirror((uint16_t)atoi(a[1].c_str()));
        void* tribe = c && is_citizen(c) ? F<void*>(c, 0x1014) : nullptr;
        if (!tribe || !a_SetChieftain)
            return "no such mirror";
        thiscall<void>(a_SetChieftain, tribe, c);
        return "leader=" + std::to_string((int)is_leader(c)) + " tool " + std::to_string(F<int>(c, kHeldToolOff));
    }
    if (a.size() >= 3 && a[0] == "settool" && is_host()) { // settool <member id> <type>: tests (BUG-0583), the game's
        // SetSpecializedTool on the host's member (any tribe's), as when it takes a rack's weapon
        void* c = nullptr;
        for (auto& kv : g_hm)
            if (kv.second.id == (uint16_t)atoi(a[1].c_str()) && game::object_alive(kv.first))
                c = kv.first;
        if (!c || !is_citizen(c) || !a_SetSpecializedTool)
            return "no such member";
        thiscall<void>(a_SetSpecializedTool, c, atoi(a[2].c_str()));
        return "tool " + std::to_string(F<int>(c, kHeldToolOff)) + " leader=" + std::to_string((int)is_leader(c));
    }
    if (a.size() >= 3 && a[0] == "grab") { // grab <member id> <tool index>: the member's GrabTool (3) order as a
        // click on the tool gives it (DoToolActionForAllSelected -> DoActionForObjects): a guest sends it to the host
        void* c = local_of((uint16_t)atoi(a[1].c_str()));
        int want = atoi(a[2].c_str());
        struct U {
            int i, want;
            void* tool;
        } u{0, want, nullptr};
        for_each_player_tool(
            [](void* t, int, const net::Vec3&, void* up) {
                auto* u = (U*)up;
                if (u->i++ == u->want)
                    u->tool = t;
            },
            &u);
        if (!c || !u.tool)
            return "no such member or tool";
        void* sel[1] = {(uint8_t*)c + 0xC0};
        void* vec[3] = {sel, sel + 1, sel + 1};
        int r = thiscall<int>(a_DoActionForObjects, (void*)nullptr, (void*)vec, 3, u.tool, (void*)nullptr);
        return "grab ordered (ret " + std::to_string(r) + ") " + g_last_cmd;
    }
    if (!a.empty() && a[0] == "party") { // party: tests (BUG-0646), the selected members party at the own fire pit
        // (a click on the fire pit: CitizenAction 12 Party with the tool of type 10, docs/re/tribe.md 2)
        void* pt = player_tribe();
        void* pit = nullptr;
        if (pt)
            for (void** it = F<void**>(pt, kToolsOff); it && it < F<void**>(pt, kToolsOff + 4); ++it)
                if (*it && tool_type(*it) == 10)
                    pit = *it;
        if (!pit)
            return "no fire pit";
        std::vector<void*> sel;
        for (auto& [c, hm] : g_hm)
            if (F<uint8_t>(c, kSelectedOff))
                sel.push_back((uint8_t*)c + 0xC0);
        void* vec[3] = {sel.data(), sel.data() + sel.size(), sel.data() + sel.size()};
        int r = thiscall<int>(a_DoActionForObjects, (void*)nullptr, (void*)vec, 12, pit, (void*)nullptr);
        return "party ordered (" + std::to_string(sel.size()) + " selected, ret " + std::to_string(r) + ")";
    }
    if (a.size() >= 3 && (a[0] == "act" || a[0] == "act_npc")) { // act <action> <member id | npc id>
        void* t = a[0] == "act" ? local_of((uint16_t)atoi(a[2].c_str())) : npc::proxy((uint16_t)atoi(a[2].c_str()));
        int action = atoi(a[1].c_str());
        // BUG-0260: the object actions (3 GrabTool, 12 Party, 15 RaidHuts, 24 Repair) take a tool or a
        // hut; with a citizen as the target DoAction read a field of the citizen as a tool's and
        // faulted (DoAction+0x78C, data 0x39C). The game's UI never pairs them (docs/re/tribe.md 2).
        if (g_fix_act_check && a[0] == "act" && t && act_needs_object(action))
            return "action " + std::to_string(action) + " needs a tool or a hut, not a member (attack a member: 7)";
        std::vector<void*> sel;
        for (auto& [id, g] : g_gm)
            if (g.c && F<uint8_t>(g.c, kSelectedOff))
                sel.push_back((uint8_t*)g.c + 0xC0);
        for (auto& [c, hm] : g_hm)
            if (F<uint8_t>(c, kSelectedOff))
                sel.push_back((uint8_t*)c + 0xC0);
        void* vec[3] = {sel.data(), sel.data() + sel.size(), sel.data() + sel.size()};
        int r = thiscall<int>(a_DoActionForObjects, (void*)nullptr, (void*)vec, action, t, (void*)nullptr);
        return "action ordered (" + std::to_string(sel.size()) + " selected, ret " + std::to_string(r) + ")";
    }
    if (a.size() >= 2 && a[0] == "near" && is_host()) { // near <ratio>: tests (BUG-0340), the later chief put that
        // part of the sum of the two chiefs' bounding radii from the first one, standing (no order)
        if (g_chief.size() < 2)
            return "no two chiefs";
        std::vector<std::pair<uint8_t, void*>> ch(g_chief.begin(), g_chief.end());
        std::sort(ch.begin(), ch.end());
        void *c0 = ch[0].second, *c1 = ch[1].second;
        float d = (float)atof(a[1].c_str()) * (game::bounding_radius(c0) + game::bounding_radius(c1));
        net::Vec3 p0 = game::position(c0);
        float l = std::sqrt(p0.x * p0.x + p0.y * p0.y + p0.z * p0.z);
        if (l < 1.f)
            return "no position";
        net::Vec3 n{p0.x / l, p0.y / l, p0.z / l};
        net::Vec3 ax = std::fabs(n.z) < 0.9f ? net::Vec3{0, 0, 1} : net::Vec3{1, 0, 0};
        net::Vec3 u{n.y * ax.z - n.z * ax.y, n.z * ax.x - n.x * ax.z, n.x * ax.y - n.y * ax.x};
        float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
        net::Vec3 p{p0.x + u.x / ul * d, p0.y + u.y / ul * d, p0.z + u.z / ul * d};
        float g = game::ground_radius(p), pl = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (g > 1.f && pl > 1.f)
            p = {p.x / pl * g, p.y / pl * g, p.z / pl * g};
        game::teleport(c1, p, game::orientation(c1));
        char b[128];
        snprintf(b, sizeof b, "put player %u's chief %.1f m from player %u's (radii %.1f + %.1f)", ch[1].first, d, ch[0].first,
                 game::bounding_radius(c0), game::bounding_radius(c1));
        return b;
    }
    if (!a.empty() && a[0] == "stack" && is_host()) { // tests (BUG-0340): every chief onto the first one
        if (g_chief.empty())
            return "no chiefs";
        auto first = *std::min_element(g_chief.begin(), g_chief.end());
        net::Vec3 at = game::position(first.second);
        int n = 0;
        for (auto& [o, c] : g_chief)
            if (c != first.second) {
                game::teleport(c, at, game::orientation(c));
                n++;
            }
        return "stacked " + std::to_string(n) + " chiefs on the chief of player " + std::to_string(first.first);
    }
    if (a.size() >= 2 && a[0] == "delay" && is_guest()) { // delay <s>: tests (BUG-0481), the mirrors' interpolation delay
        g_test_delay = std::max(0.0, std::min(3.0, atof(a[1].c_str())));
        return "mirrors follow the host's states " + std::to_string(g_test_delay) + " s back";
    }
    if (a.size() >= 2 && a[0] == "lift" && is_guest()) { // lift <m>: tests (BUG-0329), the host's members m higher
        g_test_lift = (float)atof(a[1].c_str());
        return "members of the host taken " + a[1] + " m further from the planet's centre";
    }
    if (a.size() >= 3 && a[0] == "color") { // color <tribe index> <hex colour id>: tests (BUG-0644), this game's village colour
        auto tribes = all_tribes();
        size_t i = (size_t)atoi(a[1].c_str());
        if (i >= tribes.size())
            return "no such tribe";
        F<uint32_t>(tribes[i], 0x234) = (uint32_t)strtoul(a[2].c_str(), nullptr, 16); // mIDColorID (+0x230 +4)
        memset((uint8_t*)tribes[i] + 0x254, 0, 12);                                 // mCachedColor
        return "color set";
    }
    if (a.size() >= 2 && a[0] == "setspecies") { // setspecies <hex crt inst>: tests (BUG-0643), this game's player tribe
        // members get that species (cCreatureAnimal::SetSpecies, as the game does after an editor: 0xD5BFA0)
        uintptr_t set = H->sym("Simulator::cCreatureAnimal::SetSpecies");
        void* prof = game::species_by_key({(uint32_t)strtoul(a[1].c_str(), nullptr, 16), 0x2B978C46, 0x40626200});
        void* pt = player_tribe();
        if (!set || !prof || !pt)
            return "no species / tribe";
        int n = 0;
        for (void* c : members(pt)) {
            thiscall<bool>(set, c, prof, F<int>(c, kAgeOff), true);
            n++;
        }
        // the tribe's own species key (mSpeciesKeys +0x1904: a vector whose first key sits at +0x1920 here, runtime
        // 2026-11-07): the roster's portraits are of this species
        if (F<uint32_t>(pt, 0x1924) == 0x2B978C46)
            F<uint32_t>(pt, 0x1920) = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        return "species set on " + std::to_string(n);
    }
    if (a.size() >= 2 && a[0] == "level" && is_host()) { // level <0..7>: tests (BUG-0645), the stores of the hut upgrade
        void* pt = player_tribe();
        int lv = atoi(a[1].c_str());
        if (!pt || lv < 0 || lv > 7)
            return "no player tribe / level 0..7";
        F<int>(pt, 0x38) = lv; // 0xFF0260, as the upgrade 0xCDA4F0 sets it
        uintptr_t hudp = H->sym("SporeCoop::TribeHud_ptr");
        void* hud = hudp && skg::readable((void*)hudp, 4) ? *(void**)hudp : nullptr;
        if (hud && skg::readable(hud, 0x14) && a_MaxPop)
            F<int>(hud, 0x10) = thiscall<int>(a_MaxPop, pt);
        return "level " + std::to_string(lv);
    }
    if (a.size() >= 2 && a[0] == "spawn" && is_host()) { // spawn <n>: test setup, n more adults in the player tribe
        void* pt = player_tribe();
        int n = 0;
        for (int i = 0; pt && i < atoi(a[1].c_str()) && i < 40; i++) {
            void* c = spawn_member(pt, 1);
            if (!c)
                break;
            n++;
        }
        return "spawned " + std::to_string(n);
    }
    // npc_tribe <members> <metres>: tests (BUG-0381), the host's game forms an NPC tribe that far from the
    // village (as the game's tribe controller does during play: the guests' games do not)
    if (a.size() >= 3 && a[0] == "npc_tribe" && is_host() && a_SpawnNpcTribe) {
        void* pt = player_tribe();
        if (!pt)
            return "no player tribe";
        net::Vec3 c = tribe_pos(pt);
        float r = drive::speed(c), d = (float)atof(a[2].c_str());
        if (r < 1)
            return "no village position";
        // a direction across the surface: c x (0,1,0) (or x (1,0,0) at a pole), then back onto the sphere
        net::Vec3 u{c.z, 0, -c.x};
        if (drive::speed(u) < 1e-3f)
            u = {0, -c.z, c.y};
        float ul = drive::speed(u);
        net::Vec3 q{c.x + u.x / ul * d, c.y + u.y / ul * d, c.z + u.z / ul * d};
        float ql = drive::speed(q);
        skg::Vector3 pos{q.x / ql * r, q.y / ql * r, q.z / ql * r};
        void* t = skg::cdecl_call<void*>(a_SpawnNpcTribe, &pos, 0, atoi(a[1].c_str()), 0, 0, (void*)nullptr);
        if (!t)
            return "SpawnNpcTribe returned null";
        net::Vec3 tp = tribe_pos(t);
        char b[160];
        snprintf(b, sizeof b, "npc tribe %p at (%.1f %.1f %.1f): %u members, %.0f m from the village", t, tp.x, tp.y, tp.z,
                 (unsigned)members(t).size(), net::dist(tp, c));
        return b;
    }
    // npc_tribe_at <members> <x> <y> <z>: tests (BUG-0332), SpawnNpcTribe at exactly that position (as a guest makes its
    // copy of a host's new village, tribe.npc_tribes), with the game's ground there before and after
    if (a.size() >= 5 && a[0] == "npc_tribe_at" && a_SpawnNpcTribe) {
        net::Vec3 q{(float)atof(a[2].c_str()), (float)atof(a[3].c_str()), (float)atof(a[4].c_str())};
        float g0 = game::ground_radius(q);
        skg::Vector3 pos{q.x, q.y, q.z};
        void* t = skg::cdecl_call<void*>(a_SpawnNpcTribe, &pos, 0, atoi(a[1].c_str()), 0, 0, (void*)nullptr);
        if (!t)
            return "SpawnNpcTribe returned null";
        net::Vec3 tp = tribe_pos(t);
        char b[200];
        snprintf(b, sizeof b, "npc tribe %p at (%.2f %.2f %.2f) r %.2f: %u members; ground before %.2f after %.2f, %.2f m from the given point",
                 t, tp.x, tp.y, tp.z, drive::speed(tp), (unsigned)members(t).size(), g0, game::ground_radius(q), net::dist(tp, q));
        return b;
    }
    // anims: tests (BUG-0569), the animation each member plays now as the game holds it (Anim::AnimatedCreature
    // vt 0x58 GetCurrentAnimation): host, its members by id (with the tribe index), guest, its mirrors by the host's id
    if (!a.empty() && a[0] == "anims") {
        std::string o;
        char b[96];
        auto line = [&](uint16_t id, void* c, uint32_t bhv) {
            uint32_t an = 0;
            if (c && game::object_alive(c) && game::drawn_state(c, nullptr, &an)) {
                net::Vec3 p = game::position(c);
                snprintf(b, sizeof b, "cur %u anim=%08X bhv=%08X pos=(%.1f %.1f %.1f)\n", id, an, bhv, p.x, p.y, p.z);
                o += b;
            }
        };
        if (is_host()) // bhv: the last animation its behaviour tree started (the game's call, game.cpp's listener)
            for (auto& [c, m] : g_hm)
                line(m.id, c, m.bhv_anim);
        else
            for (auto& [id, g] : g_gm)
                line(id, g.c, 0);
        return o + "anims fix=" + std::to_string((int)g_mirror_anims_fix) + " sent=" + std::to_string(g_anim_relay_sent) +
               " rx=" + std::to_string(g_anim_relay_rx) + " played=" + std::to_string(g_anim_relay_played) + " failed=" +
               std::to_string(g_anim_relay_failed) + " snap_played=" + std::to_string(g_anim_snap_played) + "\ntheme fix=" + std::to_string((int)g_theme_fix) +
               " sent=" + std::to_string(g_theme_sent) + " rx=" + std::to_string(g_theme_rx) + " played=" + std::to_string(g_theme_played) +
               "\nparty fix=" + std::to_string((int)g_party_fix) + " sent=" + std::to_string(g_party_sent) + " rx=" +
               std::to_string(g_party_rx) + " played=" + std::to_string(g_party_played) + " stopped=" + std::to_string(g_party_stopped) +
               "\nbaby fix=" + std::to_string((int)g_guest_baby_fix) + " sent=" + std::to_string(g_babies_sent) + " made=" + std::to_string(g_babies_made) + " paired=" + std::to_string(g_babies_paired) +
               " local=" + std::to_string(g_babies_local) + " from=" + [] { char b[16]; snprintf(b, sizeof b, "%X", (unsigned)g_baby_from); return std::string(b); }();
    }
    if (!a.empty() && a[0] == "form_pace") // tests (BUG-0571): the last decision of tribe.form_pace
        return "form_pace fix=" + std::to_string((int)g_form_pace_fix) + " held=" + std::to_string(g_forms_paced) + " last: " +
               (g_pace_last.empty() ? "-" : g_pace_last);
    // form: tests (BUG-0483), this game's NPC tribe controller forms its next tribe now (what it does during play)
    if (!a.empty() && a[0] == "form" && o_Form && a_NpcCtrl) {
        void* ctrl = skg::cdecl_call<void*>(a_NpcCtrl);
        if (!ctrl)
            return "no NPC tribe controller";
        size_t before = all_tribes().size();
        // BUG-0587: the controller forms nothing while the next tribe's species model is not baked yet: it asks
        // IBakeManager to bake it and returns (0xCC6440: species = list +0xD0 [min(tribes - 1, size - 1)],
        // IBakeManager vt 0x1C(species + 0x504, 0) false). Said here, so a test sees why nothing was formed.
        std::string why;
        static uintptr_t a_Bake = H->sym("Editors::IBakeManager::Get");
        auto* lb = F<void**>(ctrl, 0xD0);
        auto* le = F<void**>(ctrl, 0xD4);
        if (a_Bake && lb && le > lb && before > 0) {
            size_t i = std::min(before - 1, (size_t)(le - lb) - 1);
            void* species = lb[i];
            void* bm = species ? skg::cdecl_call<void*>(a_Bake) : nullptr;
            if (bm) {
                bool baked = vcall<char>(bm, 0x1C / 4, (uint8_t*)species + 0x504, 0) != 0;
                char b[96];
                snprintf(b, sizeof b, " (next species %p %s)", species, baked ? "baked" : "not baked yet");
                why = b;
            }
        }
        // and what else it needs: fewer tribe nouns (alive or not) than its property list allows, a spawn marker
        // (noun 0x36BE27E with +0x108 == 0x57F09E5E) left
        struct Cnt {
            int nouns = 0, markers = 0;
        } cnt;
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                auto* c = (Cnt*)u;
                if (noun == kTribeNoun)
                    c->nouns++;
                else if (noun == 0x36BE27E && F<uint32_t>(o, 0x108) == 0x57F09E5E)
                    c->markers++;
                return true;
            },
            &cnt);
        why += " (tribe nouns " + std::to_string(cnt.nouns) + ", tribe spawn markers " + std::to_string(cnt.markers) + ")";
        uint32_t blocked0 = g_forms_blocked;
        h_Form(ctrl, 0);
        return "tribes " + std::to_string(before) + " -> " + std::to_string(all_tribes().size()) + why +
               (g_forms_blocked != blocked0 ? " (blocked: tribe.guest_tribes)" : "") +
               (g_pace_last.empty() ? "" : " (pace: " + g_pace_last + ")");
    }
    // markers: tests (BUG-0332), the NPC tribe spawn markers of this game (noun 0x36BE27E, +0x108 == 0x57F09E5E: where the
    // tribe controller forms the next tribes) with their positions
    if (!a.empty() && a[0] == "markers") {
        std::string out;
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                if (noun == 0x36BE27E && F<uint32_t>(o, 0x108) == 0x57F09E5E) {
                    // a placed planet object: its position at +0x1A8 (docs/re/host-centric.md, the activation pass)
                    const float* q = (const float*)((uint8_t*)o + 0x1A8);
                    char b[96];
                    snprintf(b, sizeof b, "marker %p pos=(%.2f %.2f %.2f)\n", o, q[0], q[1], q[2]);
                    *(std::string*)u += b;
                }
                return true;
            },
            &out);
        return out.empty() ? "no markers" : out;
    }
    if (!a.empty() && a[0] == "trees") { // trees near the village with their raw fields (RE)
        net::Vec3 at;
        center(at);
        std::vector<std::pair<float, std::string>> v;
        for_each_tree([&](void* t, const net::Vec3& p) {
            char b[256];
            float d = net::dist(p, at);
            snprintf(b, sizeof b, "%p d=%.0f pos=(%.1f %.1f %.1f) c=%04X e=%04X 1c=%.2f 48=%08X 4c=%08X 50=%08X 54=%08X 58=%08X 6c=%.2f 78=%08X 7c=%02X\n",
                     t, d, p.x, p.y, p.z, F<uint16_t>(t, 0xC), F<uint16_t>(t, 0xE), F<float>(t, 0x1C), F<uint32_t>(t, 0x48),
                     F<uint32_t>(t, 0x4C), F<uint32_t>(t, 0x50), F<uint32_t>(t, 0x54), F<uint32_t>(t, 0x58), F<float>(t, 0x6C),
                     F<uint32_t>(t, 0x78), F<uint8_t>(t, 0x7C));
            v.push_back({d, b});
        });
        std::sort(v.begin(), v.end());
        std::string out;
        for (size_t i = 0; i < v.size() && i < (a.size() > 1 ? (size_t)atoi(a[1].c_str()) : 25); i++)
            out += v[i].second;
        return out;
    }
    if (a.size() >= 2 && a[0] == "objs") { // objs <noun hex>: objects of a kind near the village, world positions
        struct Q {
            uint32_t noun;
            net::Vec3 at;
            std::vector<std::pair<float, std::string>> v;
        } q{(uint32_t)strtoul(a[1].c_str(), nullptr, 16), {}, {}};
        center(q.at);
        game::for_each_object(
            [](void* o, uint32_t n, void* u) {
                auto* q = (Q*)u;
                net::Vec3 p;
                if (n == q->noun && game::object_position(o, p)) {
                    char b[128];
                    float d = net::dist(p, q->at);
                    snprintf(b, sizeof b, "%p alive=%d pos=(%.1f %.1f %.1f) dist=%.1f\n", o, (int)game::object_alive(o), p.x,
                             p.y, p.z, d);
                    q->v.push_back({d, b});
                }
                return true;
            },
            &q);
        std::sort(q.v.begin(), q.v.end());
        std::string out;
        for (size_t i = 0; i < q.v.size() && i < 40; i++)
            out += q.v[i].second;
        return out + std::to_string(q.v.size()) + " objects";
    }
    return "usage: coop.tribe select <id> <0|1> | select_own <n> <0|1> | walk <x> <y> <z> | act <action> <target id> | objs <noun hex>";
}

// the citizen's current behaviour-tree node (ai = c+0xB4C, node id at ai+0x1D8; docs/re/tribe.md B)
static uint32_t bt_node(void* c)
{
    uint8_t* ai = F<uint8_t*>(c, 0xB4C);
    return ai && skg::readable(ai + 0x1D8, 4) ? *(uint32_t*)(ai + 0x1D8) : 0;
}

std::string list()
{
    std::string out;
    char b[320];
    void* pt = player_tribe();
    auto tribes = all_tribes();
    for (size_t ti = 0; ti < tribes.size(); ti++) {
        net::Vec3 p = tribe_pos(tribes[ti]);
        auto ms = members(tribes[ti]);
        TribeInfo ti_info = tribe_info(tribes[ti]); // the members' species (BUG-0381 tests)
        uintptr_t hudp = tribes[ti] == pt ? H->sym("SporeCoop::TribeHud_ptr") : 0; // BUG-0645: the HUD's cap
        void* hud = hudp && skg::readable((void*)hudp, 4) ? *(void**)hudp : nullptr;
        snprintf(b, sizeof b, "tribe %u%s at (%.0f %.0f %.0f): %u members species=%08X archetype=%d made=%d level=%d cap=%d "
                 "hud_cap=%d obj=%p color=%08X\n",
                 (unsigned)ti, tribes[ti] == pt ? " (player)" : "", p.x, p.y, p.z, (unsigned)ms.size(), ti_info.key.instance,
                 ti_info.arch == 0xFF ? -1 : (int)ti_info.arch, (int)ours_in(g_ours_t, tribes[ti]), F<int>(tribes[ti], 0x38),
                 a_MaxPop ? thiscall<int>(a_MaxPop, tribes[ti]) : -1,
                 hud && skg::readable(hud, 0x14) ? F<int>(hud, 0x10) : -1, tribes[ti], F<uint32_t>(tribes[ti], 0x234));
        out += b;
        for (void* c : ms) {
            net::Vec3 cp = game::position(c);
            uint8_t ow = owner_of(c);
            uint16_t id = 0;
            if (C.sess && C.sess->role() == net::Role::Host) {
                auto it = g_hm.find(c);
                id = it == g_hm.end() ? 0 : it->second.id;
            } else {
                auto it = g_gid_of.find(c);
                id = it == g_gid_of.end() ? 0 : it->second;
            }
            net::Vec3 v = game::velocity(c), sv{};
            const drive::State* ds = nullptr;
            if (C.sess && C.sess->role() != net::Role::Host) { // the host's velocity in the snapshot
                auto it = g_gm.find(id);
                if (it != g_gm.end()) {
                    sv = it->second.vel;
                    ds = &it->second.ds;
                }
            }
            snprintf(b, sizeof b,
                     "  #%u %p owner=%d age=%u sel=%u hp=%.1f pos=(%.1f %.1f %.1f) speed=%.1f host_speed=%.1f dead=%d bt=%08X radius=%.2f ours=%d tool=%d leader=%d\n",
                     id, c, ow == 0xFF ? -1 : (int)ow, F<uint8_t>(c, kAgeOff), F<uint8_t>(c, kSelectedOff),
                     game::health(c), cp.x, cp.y, cp.z, drive::speed(v), drive::speed(sv), (int)game::is_dead(c),
                     bt_node(c), game::bounding_radius(c), (int)ours_in(g_ours_c, c), F<int>(c, kHeldToolOff), (int)is_leader(c));
            out += b;
            if (ds) { // guests: how the mirror is driven (drive::step)
                snprintf(b, sizeof b, "     drive err=%.2f nudges=%u kept=%u teleports=%u\n", ds->last_err, ds->nudges,
                         ds->nudges_kept, ds->teleports);
                out += b;
            }
        }
    }
    return out;
}

// BUG-0664 (docs/re/tribe.md#tribe-sync-bodies): the sync-check compares members, not bodies. tribe.member_body
// (BUG-0620) keeps a dead member's body in g_hm while the host's game keeps it, but a guest spawns no mirror for a body
// (it only lets a mirror it already has lie there): a guest that joined or loaded the world while a body lay on the
// host had none, and the check said "t: 1 missing on the guest" (long_tribe on rel/0.7.9, after its restart and its
// rejoin). With the fix both sides leave bodies out of the check; off: the host lists them (0.7.9 before the fix).
static bool g_sync_bodies_fix = true;
COOP_FIX("tribe.sync_bodies", "BUG-0664", g_sync_bodies_fix,
         "tribe: the sync-check leaves the bodies of dead members out (a guest that joined meanwhile has no copy)");

std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    if (C.sess && C.sess->role() == net::Role::Host) {
        for (auto& [c, h] : g_hm)
            if (game::object_alive(c) && !(g_sync_bodies_fix && h.body_since != 0))
                v.push_back({"t" + std::to_string(h.id), game::position(c), game::orientation(c)});
    } else {
        for (auto& [id, g] : g_gm)
            if (g.c && game::object_alive(g.c) && !(g_sync_bodies_fix && g.body))
                v.push_back({"t" + std::to_string(id), game::position(g.c), game::orientation(g.c)});
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
        for (auto& [c, hm] : g_hm)
            if (hm.owner == me && F<uint8_t>(c, kSelectedOff))
                n++;
    } else
        for (auto& [id, m] : g_gm)
            if (m.c && m.owner == me && F<uint8_t>(m.c, kSelectedOff))
                n++;
    return n;
}

int give_selected(uint8_t to)
{
    if (!C.sess || !C.sess->connected() || game::mode_id() != modes::kTribe || to == C.sess->my_id())
        return 0;
    uint8_t me = C.sess->my_id();
    std::vector<uint16_t> ids;
    if (C.sess->role() == net::Role::Host) {
        for (auto& [c, hm] : g_hm)
            if (hm.owner == me && F<uint8_t>(c, kSelectedOff))
                ids.push_back(hm.id);
        give_on_host(me, to, ids);
    } else {
        for (auto& [id, m] : g_gm)
            if (m.c && m.owner == me && F<uint8_t>(m.c, kSelectedOff))
                ids.push_back(id);
        send_cmd(CMD_GIVE, ids, to, {}, {});
    }
    return (int)ids.size();
}

} // namespace tribe
