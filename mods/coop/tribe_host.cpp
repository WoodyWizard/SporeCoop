#include "tribe.h"
#include "tribe_internal.h"
#include "tribe_logic.h"
#include "call_guard.h"
#include "lifecycle.h"
#include "mirror_registry.h"
#include "player_life.h"

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

// BUG-0841 (evidence): members the host's game moved from one tribe to another (none seen: the game adds only new
// citizens to a tribe, docs/re/tribe.md#tribe-member-moves); unowned: into the player tribe with owner 0xFE
uint32_t g_member_moves, g_member_moves_unowned;

// E1 (BUG-1154): the owners of the player tribe's members across joins, leaves, a game over and sessions. The owner
// table (g_hm) lived only in the host's memory, and every change of the players (a join, a leave, a new session) and
// every world change (a game over, a load) dealt every member anew in turns: a child a guest bought, members it was
// given and its chief went to other players. Now each member has a claim: its owner's stable key (store.h key_of; the
// host is "host"). A claimed member is its player's whenever that player is here; while it is away the host holds it
// (never another guest). A player who joins with no member gets an even share from the player with the most (members
// the game made for a player, babies and gifts, move last). The claims go into the per-player store on the host's disk
// with every save (owner_claims.h: by position, world record tribe.owners) and come back to the same members after a
// load (the game over's reload too). `coop.fix store.tribe_owners off`: dealt in turns as before (0.7.14).
bool g_owners_fix = true;
COOP_FIX("store.tribe_owners", "BUG-1154", g_owners_fix,
         "tribe: tribe members keep their owners across joins, leaves, a game over and sessions (kept on the host's disk)");
std::unordered_map<void*, std::string> g_member_claim; // host: player tribe member -> its owner's stable key
std::unordered_map<void*, uint8_t> g_member_flags;      // host: 1 born for its player, 2 a gift
static std::vector<claims::Spot> g_member_spots;              // host: read from the store, not matched yet
uint32_t g_mclaims_serial, g_mclaims_read, g_mclaims_moved, g_mclaims_held;
bool g_members_matched;
static std::map<std::string, uint8_t> g_tpresent;
static double g_mclaims_at;
static int tpresent(const std::string& k)
{
    auto it = g_tpresent.find(k);
    return it == g_tpresent.end() ? -1 : it->second;
}
// a claimed member's holder (its player, or the host for an absent one), -1: no claim
static int claim_holder(void* c)
{
    if (!g_owners_fix)
        return -1;
    auto it = g_member_claim.find(c);
    return it == g_member_claim.end() ? -1 : claims::holder(it->second, tpresent, C.sess->my_id());
}
void member_claim(void* c, uint8_t owner, uint8_t flag)
{
    std::string k = store::key_of(owner);
    if (!g_owners_fix || k.empty())
        return;
    g_member_claim[c] = k;
    if (flag)
        g_member_flags[c] |= flag;
}
// TR-1 (BUG-1429): the 2 s follow below ran first in host_tick, before the adopt loop and the resplit: after a world
// change (a load, the game over's reload) with a player here the members had no entries yet (hm null) and every
// present player's claim was dropped (the host's too), then all were dealt in turns; at a join it saw the newcomer
// here while its members were still held by the host and rewrote their claims to "host" before the resplit; after
// `coop.store adopt` (claim keys renamed, roster unchanged) it rewrote them to the holder's key. On: the follow runs
// after the adopt loop and the resplit of the same tick, keeps a claim whose member has no entry yet, and an adopt
// redoes the split. Off: as 0.7.16.
static bool g_claims_order_fix = true;
COOP_FIX("tribe.claims_order", "BUG-1429", g_claims_order_fix,
         "tribe: who owns which tribe member is kept when the host loads its world again (a game over too), when a player joins and after coop.store adopt");
// TR-6 (BUG-1430): the first tick after a world change resplit (and balanced) with no member entries yet: a present
// player with no claim on a world whose members are all claimed got none, and split_for == players held that until the
// next join or leave. On: the balance of a resplit runs after the adopt loop. Off: before it (0.7.16).
static bool g_claims_balance_fix = true;
COOP_FIX("tribe.claims_balance", "BUG-1430", g_claims_balance_fix,
         "tribe: after the host loads its world again a player whose members were all claimed by others still gets its share");
// BUG-1450: the balance gave a player with no member the first plain member in member order: the game's leader (the
// host's chief #1 with the staff) when the host was the giver (lab 20261122-194210 tribe_held_item: after the guest's
// rejoin #1 was "Bob's chief"; the guest's claims had been lost by TR-1's join window). On: the leader never moves in a
// balance (tribe_logic::balance's `fixed`). Off: as 0.7.16.
static bool g_balance_leader_fix = true;
COOP_FIX("tribe.balance_leader", "BUG-1450", g_balance_leader_fix,
         "tribe: the host's chief (the tribe's leader with the staff) stays the host's when a player joins with no member of its own");
// tests (BUG-1450): `coop.tribe claims_at_join 1` makes the 2 s follow due in the tick of the next join (the window of
// TR-1's join case, about 1 tick in 17), so the lab hits it every time
bool g_claims_at_join;
uint32_t g_claims_join_forced;
static void member_claims_follow(void* pt, double now, const std::vector<void*>& ms);
static void member_claims_tick(void* pt, double now, bool follow = true)
{
    g_tpresent.clear();
    for (auto& p : C.sess->players()) {
        std::string k = store::key_of(p.id);
        if (!k.empty())
            g_tpresent[k] = p.id;
    }
    if (!g_owners_fix || !pt)
        return;
    pstore::Store* w = store::world();
    if (w && store::serial() != g_mclaims_serial) { // the world's store opened (or the world changed): its claims
        g_mclaims_serial = store::serial();
        g_member_spots.clear();
        g_member_claim.clear();
        g_member_flags.clear();
        g_members_matched = false;
        if (const pstore::Record* r = w->get_world("tribe.owners"); r && r->ver == 1) {
            pstore::In in(r->data);
            if (!claims::get_spots(in, g_member_spots))
                g_member_spots.clear();
        }
    }
    auto ms = members(pt);
    if (!g_members_matched && !ms.empty()) {
        std::vector<std::array<float, 3>> pos;
        for (void* c : ms) {
            net::Vec3 q = game::position(c);
            pos.push_back({q.x, q.y, q.z});
        }
        auto m = claims::match(pos, g_member_spots, 2.5f);
        for (size_t i = 0; i < ms.size(); i++)
            if (m[i] >= 0) {
                g_member_claim[ms[i]] = g_member_spots[m[i]].key;
                if (g_member_spots[m[i]].flags)
                    g_member_flags[ms[i]] = g_member_spots[m[i]].flags;
                g_mclaims_read++;
            }
        g_members_matched = true;
        g_member_spots.clear();
    }
    if (follow)
        member_claims_follow(pt, now, ms);
}
static void member_claims_follow(void* pt, double now, const std::vector<void*>& ms)
{
    if (!g_owners_fix || !pt || !g_members_matched || now - g_mclaims_at < 2.0)
        return;
    g_mclaims_at = now; // every 2 s: the claims follow the owners of members whose claimed player is here
    std::unordered_map<void*, std::string> mc;
    for (void* c : ms) {
        auto old = g_member_claim.find(c);
        HEntry* hm = hm_of(c);
        if (old != g_member_claim.end() && tpresent(old->second) < 0)
            mc[c] = old->second;
        else if (hm && hm->data.owner < 0xFE && !store::key_of(hm->data.owner).empty())
            mc[c] = store::key_of(hm->data.owner);
        else if (g_claims_order_fix && !hm && old != g_member_claim.end())
            mc[c] = old->second; // TR-1: no entry yet (adopted later in this tick): the claim stays
    }
    g_member_claim = std::move(mc);
    for (auto it = g_member_flags.begin(); it != g_member_flags.end();)
        it = g_member_claim.count(it->first) ? std::next(it) : g_member_flags.erase(it);
}
// a player here without a member gets an even share: from the player with the most, its plain members first
static void member_balance(void* pt, const std::vector<uint8_t>& players)
{
    if (!g_owners_fix || players.empty())
        return;
    auto ms = members(pt);
    std::map<uint8_t, std::vector<void*>> by;
    for (void* c : ms)
        if (HEntry* hm = hm_of(c))
            by[hm->data.owner].push_back(c);
    // (D3: tribe_logic::balance, the rule; here its moves)
    auto plain = [](void* m) { return !g_member_flags.count(m); };
    auto moves = g_balance_leader_fix ? tribe_logic::balance(by, players, ms.size(), plain, [](void* m) { return is_leader(m); })
                                      : tribe_logic::balance(by, players, ms.size(), plain);
    for (auto& [c, p] : moves) {
        if (HEntry* hm = hm_of(c))
            hm->data.owner = p;
        member_claim(c, p, 0);
        g_mclaims_moved++;
    }
}
// the game saves: the claims by position (world record tribe.owners v1)
static void member_claims_save(pstore::Store& s, int64_t now)
{
    if (!g_owners_fix || !g_mclaims_serial || g_mclaims_serial != store::serial() || &s != store::world() ||
        game::mode_id() != modes::kTribe)
        return;
    std::vector<claims::Spot> v = g_member_spots; // (not matched yet: as read)
    if (void* pt = player_tribe())
        for (void* c : members(pt))
            if (auto it = g_member_claim.find(c); it != g_member_claim.end()) {
                net::Vec3 q = game::position(c);
                auto f = g_member_flags.find(c);
                v.push_back({q.x, q.y, q.z, it->second, f == g_member_flags.end() ? (uint8_t)0 : f->second});
            }
    if (v.empty() && !s.get_world("tribe.owners"))
        return;
    pstore::Out o;
    claims::put_spots(o, v);
    s.put_world("tribe.owners", 1, o.d);
    std::vector<std::string> keys; // BUG-1500: the owners keep their names in the store
    for (auto& sp : v)
        keys.push_back(sp.key);
    store::mark_owners(s, "tribe.owner", keys, now);
}
// NET-3 (BUG-1499): the save opened the world's store itself: its claims are read and matched now and follow the
// owners of this moment (as the next ticks would), so this save writes them
static void member_claims_opened()
{
    if (!active() || !is_host() || game::mode_id() != modes::kTribe)
        return;
    if (void* pt = player_tribe()) {
        g_mclaims_at = 0; // the follow runs now
        member_claims_tick(pt, H->time_seconds());
    }
}
static bool g_mclaims_reg = store::add({"tribe.owners", member_claims_save, [](const std::string& from, const std::string& to) {
                                            for (auto& [c, k] : g_member_claim)
                                                if (k == from)
                                                    k = to;
                                            for (auto& sp : g_member_spots)
                                                if (sp.key == from)
                                                    sp.key = to;
                                            if (g_claims_order_fix)
                                                g_host.split_for.clear(); // TR-1: the renamed claims go to their player
                                        }, member_claims_opened});

void host_tick(double now)
{
    test_vel_tick(now);
    if (now - g_last_send < 0.12)
        return;
    g_last_send = now;
    gameover_host_tick(now); // BUG-0960
    baby_tick(now); // BUG-0671
    ff_host_tick(now); // BUG-0899
    void* pt = player_tribe();
    auto tribes = all_tribes();
    std::vector<uint8_t> players;
    for (auto& p : C.sess->players())
        players.push_back(p.id);
    std::sort(players.begin(), players.end());
    if (g_claims_at_join && pt && players.size() > g_host.split_for.size()) { // tests (BUG-1450): a join in this tick
        g_claims_at_join = false;
        g_claims_join_forced++;
        g_mclaims_at = -1e9; // the follow is due now
    }
    member_claims_tick(pt, now, !g_claims_order_fix); // E1 (BUG-1154); TR-1: the follow after the adopt loop
    bool resplit = false;
    if (players != g_host.split_for && pt) {
        resplit = true;
        arrival_place_due(now); // BUG-0340: a player arrived or rejoined
        // the players changed: split the player tribe again, in member order (E1: a claimed member to its player)
        g_host.split_for = players;
        g_host.dealt = 0;
        for (void* c : members(pt)) {
            HEntry* it = hm_of(c);
            if (!it)
                continue;
            int cl = claim_holder(c);
            if (cl >= 0)
                g_mclaims_held += cl == C.sess->my_id() && g_member_claim[c] != "host";
            it->data.owner = tribe_logic::resplit_owner(cl, players, g_host.dealt); // (D3)
        }
        if (!g_claims_balance_fix)
            member_balance(pt, players);
    }
    struct Entry {
        uint16_t id;
        uint8_t tribe, owner, age;
        void* c;
        bool body = false; // BUG-0620
    };
    std::vector<Entry> entries;
    std::unordered_set<uint16_t> seen; // the ids of this pass (C1: the sweep's "seen")
    for (size_t ti = 0; ti < tribes.size() && ti < 32; ti++) {
        bool player = tribes[ti] == pt;
        for (void* c : members(tribes[ti])) {
            const mirror::Ref ref{c, game::object_serial(c), kMemberNoun};
            uint16_t id = 0;
            bool fresh = false;
            if (!g_kept_refs_fix && g_host.members.id_of(c, id)) {
                // (tribe.kept_refs off: the entry at the address stays that member's whatever was built there)
            } else if (!g_host.members.adopt(ref, [] { return net::alloc_id(g_host.next_id, HIds{}); }, // BUG-0154, BUG-0951
                                   [](uint16_t) { g_kept_stale++; }, id, &fresh))
                continue; // (no id free)
            HEntry* it = g_host.members.find(id);
            if (!it)
                continue;
            seen.insert(id);
            if (fresh) { // the owner (D3: tribe_logic::new_owner): E1's claim, else the players in turns; BUG-0671's purchase
                auto bf = g_host.born_for.find(c);
                bool bought = bf != g_host.born_for.end();
                it->data.owner = tribe_logic::new_owner(player, player ? claim_holder(c) : -1, players, g_host.dealt,
                                                        bought ? (int)bf->second : -1,
                                                        bought && net::is_player(*C.sess, bf->second));
                if (bought)
                    g_host.born_for.erase(bf);
            }
            if (it->data.tribe && it->data.tribe != tribes[ti]) { // BUG-0841: a known member in another tribe now
                g_member_moves++;
                if (player && it->data.owner == 0xFE)
                    g_member_moves_unowned++;
            }
            it->data.tribe = tribes[ti];
            it->data.body_since = 0;
            entries.push_back({id, (uint8_t)ti, it->data.owner, F<uint8_t>(c, kAgeOff), c});
        }
    }
    // BUG-0620: the bodies of members that left their list dead are kept and sent (D3: tribe_logic::body_hold); the
    // other entries the pass did not see go
    auto body = [&](HEntry& e, bool alive_obj) {
        void* c = e.ref.p;
        auto ti = std::find(tribes.begin(), tribes.end(), e.data.tribe);
        auto h = tribe_logic::body_hold(alive_obj, alive_obj && game::is_dead(c), ti == tribes.end() ? -1 : (int)(ti - tribes.begin()),
                                        e.data.body_since, now, kBodyMax);
        e.data.body_since = h.since;
        if (!h.keep)
            return false;
        g_host.bodies_sent++;
        entries.push_back({e.id, (uint8_t)(ti - tribes.begin()), e.data.owner, F<uint8_t>(c, kAgeOff), c, true});
        return true;
    };
    g_host.members.sweep([&](uint16_t id) { return seen.count(id) != 0; },
               [&](HEntry& e, bool alive_obj) { return body(e, alive_obj) ? mirror::Unseen::Keep : mirror::Unseen::Remove; },
               [](HEntry&, bool) {}); // (no game call: the game removed the member)
    if (resplit && g_claims_balance_fix) { // TR-6 (BUG-1430): with this tick's new entries (a world change has only them)
        member_balance(pt, players);
        for (Entry& e : entries) // (this tick's snapshot with the owners the balance gave)
            if (HEntry* it = g_host.members.find(e.id))
                e.owner = it->data.owner;
    }
    if (g_claims_order_fix && pt) // TR-1 (BUG-1429): the claims follow the owners this tick's split and adoption gave
        member_claims_follow(pt, now, members(pt));
    // chunks that fit one packet; every chunk carries the tribe table. A chunk larger than a packet
    // is dropped by the connection: with 24 members per chunk (fixed until 2026-10-23) the first
    // chunk (the player tribe first) never arrived once the planet had 24 citizens, and the guest
    // removed those mirrors at the end of every cycle (play_tribe.py: the guest's tribe emptied)
    g_cycle++;
    size_t nt = std::min<size_t>(tribes.size(), 32);
    std::vector<TribeInfo> infos; // BUG-0381
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
    send_member_air();      // BUG-0946
}

// BUG-0744: a guest's gather order on a loose fruit (cFruit, noun 0x2C9CC91) its game has and the host's has not within
// 8 m (each game's fruit roll and are recycled their own way, BUG-0472 / BUG-0497) was rejected: the guest's members
// stood. With the fix the host takes the nearest tree with fruit left (cTreeData +0x48 != 0) within 12 m (the
// usual gather target, CitizenAction 2), else its own nearest fruit within 15 m, so the order is a gather on both games.
// `coop.fix tribe.fruit_order off`: rejected (0.7.11).
static bool g_fruit_order_fix = true;
COOP_FIX("tribe.fruit_order", "BUG-0744", g_fruit_order_fix,
         "tribe: a guest's gather order on a fruit the host's game does not have there gathers the host's nearest fruit or fruit tree");
uint32_t g_fruit_orders_moved;
// BUG-1494 (audit TR-8): fruit_instead read cTreeData +0x48 and +0x4C as one u64; +0x4C is every fruit slot of the tree
// (0x07 / 0x1F, never 0: docs/re/tribe.md CORRECTION 2026-11-13, two u32 masks), so a picked-empty tree passed and the
// guest's members walked to it and gathered nothing. Now only +0x48 (the fruit left, as for_each_tree_pos reads it).
// `coop.fix tribe.tree_mask off`: the u64 read (0.7.16.1).
static bool g_tree_mask_fix = true;
COOP_FIX("tribe.tree_mask", "BUG-1494", g_tree_mask_fix,
         "tribe: a guest's gather order on a fruit the host lacks never goes to a tree with no fruit left");
static void* fruit_instead(const net::Vec3& pos)
{
    void* best = nullptr;
    float bd = 12.f;
    for_each_tree([&](void* t, const net::Vec3& p) {
        float d = net::dist(p, pos);
        if (d < bd && tribe_logic::tree_has_fruit(g_tree_mask_fix, F<uint32_t>(t, 0x48), F<uint32_t>(t, 0x4C))) {
            bd = d;
            best = t;
        }
    });
    return best ? best : game::find_object(kFruitNoun, pos, 15.f);
}

// BUG-1053 (class BUG-0426, the live pointer package L2 of 0.7.14): the host ran a guest's command on the members and
// targets of its table g_hm, rebuilt from the game's lists only every kHostInterval: a member the host's game removed
// since (taken out of its tribe and destroyed, a raid's fall) was still there, and DoAction got the destroyed citizen
// as the order's target (or the member, checked by its own memory only, object_alive). Now a member or target of the
// table is used only while it is the same object in the game's list (game::alive with the serial the table kept,
// inside the frame's game::LiveScope: one walk); else the command is rejected as gone. The same check guards
// give_on_host and near_own_chief (a hook inside the game's update: the list is walked there).
// `coop.fix tribe.cmd_listed off`: the table's pointers as they were (0.7.13).
bool g_cmd_listed_fix = true;
COOP_FIX("tribe.cmd_listed", "BUG-1053", g_cmd_listed_fix,
         "tribe: the host never runs a guest's order on a member or target its game removed since the last table cycle");
uint32_t g_cmd_gone;
// host: c of g_hm is the member the table made it for and in the game's list now
static bool hm_live(void* c)
{
    HEntry* it = hm_of(c);
    if (!it)
        return false;
    if (!g_cmd_listed_fix || game::alive(game::LiveRef{c, it->ref.serial, kMemberNoun}))
        return true;
    g_cmd_gone++;
    return false;
}

// BUG-0899: Host-side friendly fire attack orders between members of different players inside the shared player tribe.
struct FfOrder {
    uint16_t victim_id = 0;
    double next_walk = 0, next_hit = 0;
};
static std::unordered_map<uint16_t, FfOrder> g_ff_orders;
static uint32_t g_ff_orders_total;

void ff_clear_orders()
{
    g_ff_orders.clear();
    ff_note_counts(g_ff_orders_total, 0);
}

void ff_cancel_member(uint16_t id)
{
    g_ff_orders.erase(id);
    ff_note_counts(g_ff_orders_total, (uint32_t)g_ff_orders.size());
}

bool ff_try_order(uint16_t att_id, uint8_t att_owner, void* target, int action)
{
    void* pt = player_tribe();
    if (!pt || action != 7 || !target || !is_citizen(target) || F<void*>(target, kOwnerTribeOff) != pt) {
        ff_cancel_member(att_id);
        return false;
    }
    HEntry* vh = hm_of(target);
    if (!vh || vh->data.owner >= tribe_logic::kNobody || vh->data.owner == att_owner) {
        ff_cancel_member(att_id);
        return false;
    }
    if (!tribe_logic::can_ff_target(ff_on(), att_owner, vh->data.owner)) {
        ff_cancel_member(att_id);
        return true; // refuse same-tribe cross-player attack while FF is off
    }
    double now = H->time_seconds();
    g_ff_orders[att_id] = {vh->id, 0.0, now + 0.15};
    g_ff_orders_total++;
    ff_note_counts(g_ff_orders_total, (uint32_t)g_ff_orders.size());
    return true;
}

void ff_host_tick(double now)
{
    if (g_ff_orders.empty())
        return;
    if (!ff_on() || !is_host() || !active()) {
        ff_clear_orders();
        return;
    }
    void* pt = player_tribe();
    if (!pt) {
        ff_clear_orders();
        return;
    }
    for (auto it = g_ff_orders.begin(); it != g_ff_orders.end();) {
        HEntry* ah = g_host.members.find(it->first);
        HEntry* vh = g_host.members.find(it->second.victim_id);
        void* ac = ah ? ah->ref.p : nullptr;
        void* vc = vh ? vh->ref.p : nullptr;
        bool ok = ac && vc && hm_live(ac) && hm_live(vc) && F<void*>(ac, kOwnerTribeOff) == pt &&
                  F<void*>(vc, kOwnerTribeOff) == pt && !game::is_dead(ac) && !game::is_dead(vc) &&
                  game::health(ac) > 0 && game::health(vc) > 0 &&
                  tribe_logic::can_ff_target(true, ah->data.owner, vh->data.owner);
        if (!ok) {
            it = g_ff_orders.erase(it);
            continue;
        }
        net::Vec3 ap = game::position(ac), vp = game::position(vc);
        float d = net::dist(ap, vp);
        auto step = tribe_logic::ff_combat_step(d, now, it->second.next_walk, it->second.next_hit);
        if (step == tribe_logic::FfStep::Walk) {
            void* rec = thiscall<void*>(a_DoAction, ac, 4, (void*)nullptr, (void*)nullptr);
            float p[3] = {vp.x, vp.y, vp.z};
            if (rec)
                thiscall<void>(a_SetValueVector3, (uint8_t*)rec + 0x39, (const float*)p);
            it->second.next_walk = now + 0.5;
        } else if (step == tribe_logic::FfStep::Hit) {
            ff_deal_hit(vc, ac, 8.f);
            it->second.next_hit = now + 1.2;
        }
        ++it;
    }
    ff_note_counts(g_ff_orders_total, (uint32_t)g_ff_orders.size());
}

static void* resolve_target(const Target& t)
{
    if (t.kind == 1) {
        void* c = hm_obj(t.id);
        if (!c)
            return nullptr;
        return !g_cmd_listed_fix || hm_live(c) ? c : nullptr; // (BUG-1053)
    }
    if (t.kind == 2 && t.noun)
        return game::find_object(t.noun, t.pos, 8.f);
    if (t.kind == 3)
        return find_tree(t.pos, 1.f);
    return nullptr;
}

// host: members of `from` change hands (a gift); owners travel with the snapshots
void give_on_host(uint8_t from, uint8_t to, const std::vector<uint16_t>& ids)
{
    void* pt = player_tribe();
    int n = 0;
    for (uint16_t id : ids) {
        HEntry* hm = g_host.members.find(id);
        void* c = hm ? hm->ref.p : nullptr;
        if (!c || hm->data.owner != from || (g_cmd_listed_fix && !hm_live(c)) || // (BUG-1053)
            F<void*>(c, kOwnerTribeOff) != pt)
            continue;
        hm->data.owner = to;
        member_claim(c, to, 2); // E1 (BUG-1154): the receiver's from now on
        n++;
    }
    g_gifts += n;
    if (n && C.notice)
        C.notice(C.sess->name_of(from) + " gave " + std::to_string(n) + (n == 1 ? " tribe member to " : " tribe members to ") +
                 C.sess->name_of(to));
}
// BUG-1068: "a village only guests raided falls without counting for the stage's goal". The goal bar counts a village
// when its cTribeToTribeMission ends (TribeToTribeMission_Update 0xC2F750 sees the conquered flag +0x556 or the
// alliance, docs/re/tribe.md#tribe-conquest-sync); the mission is made by TribeMission_SetTargetTribe 0xCD3D70, which
// the host's game calls from its own input only (DoActionGeneric 0xCD0C20, input actions 0xC raid / 0xE, 0x10 gift).
// A guest's order runs here through DoAction without it: the village had no mission, its fall counted nothing.
// Now a guest's raid (CitizenAction 15) or gift (9) on another tribe's object makes that village's mission as the
// host's click does (wsync::goal_target: SetTargetTribe outside the frame; the goal card follows it, as for the host's
// own clicks). `coop.fix tribe.guest_mission off`: 0.7.13.
static bool g_guest_mission_fix = true;
COOP_FIX("tribe.guest_mission", "BUG-1068", g_guest_mission_fix,
         "tribe: a village the guests raid or give gifts to counts for the stage's goal, as one the host acts on");
uint32_t g_guest_missions;
// the village (cTribe) of an order's target: the tribe itself, a hut (+0x230), a tool (+0x1F8), a member (+0x1014)
static void* target_village(void* o)
{
    if (!o || !game::object_alive(o))
        return nullptr;
    uint32_t noun = game::noun_id(o);
    void* t = noun == kTribeNoun        ? o
              : noun == 0x1E4DAAEu      ? F<void*>(o, 0x230)       // cTribeHut (docs/re/tribe.md "Raids on buildings")
              : noun == 0x18C8F0Cu      ? F<void*>(o, 0x1F8)       // cTribeTool
              : noun == 0x18EB4B7u      ? F<void*>(o, kOwnerTribeOff) // cCreatureCitizen
                                        : nullptr;
    if (!t || t == player_tribe())
        return nullptr;
    auto ts = all_tribes();
    return std::find(ts.begin(), ts.end(), t) != ts.end() ? t : nullptr;
}

void host_command(uint8_t from, net::Reader& r)
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
    if (kind == CMD_CONQUER) { // BUG-0879
        if (g_away_conquest_fix && from != 0 && game::mode_id() == modes::kTribe)
            conquer_on_host(from, pos);
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
    if (kind == CMD_ACTION && !target && g_fruit_order_fix && tgt.kind == 2 && tgt.noun == kFruitNoun && action == 2 &&
        (target = fruit_instead(tgt.pos))) { // BUG-0744
        g_fruit_orders_moved++;
        char m[96];
        snprintf(m, sizeof m, " (no such fruit here: %s %p instead)", is_tree(target) ? "tree" : "fruit", target);
        g_last_cmd += m;
    }
    if (kind == CMD_ACTION && tgt.kind != 0 && !target) {
        g_cmds_rejected++;
        g_last_cmd += tgt.kind == 1 && g_host.members.has(tgt.id) ? " rejected: the target is gone (BUG-1053)" : " rejected: no such target here";
        return; // the object is gone on the host
    }
    int ran = 0;
    for (uint16_t id : ids) {
        HEntry* hm = g_host.members.find(id);
        void* c = hm ? hm->ref.p : nullptr;
        if (!c) {
            g_cmds_rejected++;
            continue;
        }
        if (hm->data.owner != from ||
            (g_cmd_listed_fix ? !hm_live(c) : !game::object_alive(c)) || F<void*>(c, kOwnerTribeOff) != pt) { // (BUG-1053)
            g_cmds_rejected++;
            continue;
        }
        if (kind == CMD_WALK) {
            ff_cancel_member(id);
            void* rec = thiscall<void*>(a_DoAction, c, 4, (void*)nullptr, (void*)nullptr);
            float p[3] = {pos.x, pos.y, pos.z};
            if (rec)
                thiscall<void>(a_SetValueVector3, (uint8_t*)rec + 0x39, (const float*)p);
        } else if (!ff_try_order(id, hm->data.owner, target, action))
            thiscall<void*>(a_DoAction, c, action, target, (void*)nullptr);
        g_cmds_done++;
        ran++;
    }
    // BUG-1068: the village's goal mission, as the host's own raid or gift click makes it
    if (ran && g_guest_mission_fix && kind == CMD_ACTION && (action == 15 || action == 9) && from != C.sess->my_id())
        if (void* v = target_village(target); v && wsync::goal_target(v)) {
            g_guest_missions++;
            g_last_cmd += " (the village's goal mission: BUG-1068)";
        }
}

} // namespace tribe
