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

// BUG-0569: a guest saw its village's members stand still while the host's fought, gathered, danced and built.
// The behaviour tree of a guest's mirror does not run (h_BTTick: the host's member decides), and the animations of
// the members' actions are started by those trees (SporeCoop::CreaturePlayAnim: fights, gathering, the grab, the
// social dances; game.cpp's behaviour animation listener). The mirrors played only what their locomotion picks
// (idle, walk). As for the animals (BUG-0464): the host reports every behaviour animation of a member
// it tracks and sends them once a cycle to the guests (MSG_NPC_ANIM kind 2: u8 n, n x {u16 member id, u32 anim, u8 how,
// u8 a, u32 b}); once a second the animation each member plays now when its tree started it (kind 3: u8 n, n x
// {u16 id, u32 anim}) for a mirror made later or a lost message. A guest plays the same call on the mirror.
// Older guests ignore both kinds (npc.cpp reads kinds 0 and 1 only).
struct MemberAnim {
    uint16_t id;
    uint32_t anim;
    uint8_t how, a;
    int32_t b;
};
static std::vector<MemberAnim> g_member_anim_out;
static double g_member_snap_at;

void on_behaviour_anim(void* c, uint32_t anim, int how, int a, int b)
{
    if (!g_ok || !is_host() || game::mode_id() != modes::kTribe)
        return;
    HEntry* it = hm_of(c);
    if (!it)
        return;
    it->data.bhv_anim = anim; // (`coop.tribe anims` shows it to the tests)
    if (g_member_anim_out.size() < 256)
        g_member_anim_out.push_back({it->id, anim, (uint8_t)how, (uint8_t)(a & 0xFF), (int32_t)b});
}

// BUG-0946 (docs/re/tribe.md#tribe-member-toss): a blow of 60 hp and more throws a member up (about 5 m, over 3 m for
// 1.3-1.5 s, on the host's game, the fight node's reaction); a guest's mirror keeps its own height (the two games'
// grounds differ: drive.own_height BUG-0329, the mirrors' ground BUG-0444) and stayed on its ground. As the animal
// proxies (BUG-0550): once a cycle the host sends every member it runs that is off its ground for a reason
// its game knows (drive::body_airborne: not grounded and more than 0.15 m off GetHeightAt; not dead) with its height
// over the host's ground (MSG_NPC_ANIM kind 9: u8 n, n x {u16 member id, f32 agl}); a guest places that mirror at that
// height over its own ground (drive::air_place: not walked, not grounded, held through animation starts) while the
// news is fresh (0.35 s), and lets its game land it after. Older guests ignore the kind (npc.cpp passes 2..8 on).
// `coop.fix tribe.mirror_air off`: nothing sent or placed (the mirror on its ground).
bool g_mirror_air_fix = true;
COOP_FIX("tribe.mirror_air", "BUG-0946", g_mirror_air_fix, "tribe: a member thrown up by a blow (or otherwise off its ground) on the host is in the air on the guests' screens too");
constexpr float kMirrorAirMin = 0.5f; // m over the host's ground: off it whatever the grounded byte says
// BUG-1082 (docs/re/tribe.md#tribe-member-hang): the campaign's raids failed the sync-check on members the host's game
// held up for 15-28 s (the airborne node 0x0485FD10, 5-15 m, speed 0; an NPC village member 114 m up) while the
// guests' mirrors stood on their ground, 11.9 and 113.9 m apart. Three limits of tribe.mirror_air let a mirror down:
// a height over drive::g_agl_max (30 m: the 114 m member was never placed), the 0.35 s freshness (a 4-player host at
// 18-20 fps with 200-360 ms hitches sends the list every 0.12 s of its own frames, i.e. 0.25-0.4 s apart; one late
// list and the mirror walked on its ground until the next), and 64 members per list (a cycle's others not sent).
// tribe.mirror_hang: any height (kMirrorHangMax), the list kept for kMirrorHangHold s, lists of 64 as often as
// needed, and a member that was in the last list and is on its ground now is sent once with height 0 (a landing:
// the guest walks it at once, not after the hold; guests before it read 0 as "on the ground" too). Off: 0.7.13.
bool g_mirror_hang_fix = true;
COOP_FIX("tribe.mirror_hang", "BUG-1082", g_mirror_hang_fix,
         "tribe: a member the host's game holds up in the air (any height, any frame rate) is up on the guests' screens too");
static std::unordered_set<uint16_t> g_air_last; // the ids of the last list (their landing is sent)
void send_member_air()
{
    if (!g_mirror_air_fix)
        return;
    std::vector<std::pair<uint16_t, float>> v;
    std::unordered_set<uint16_t> up;
    g_host.members.for_each([&](HEntry& e_) {
        void* const c = e_.ref.p;
        if ((!g_mirror_hang_fix && v.size() >= 64) || !kept_alive(c, e_.ref.serial, 0x18EB4B7u) || game::is_dead(c))
            return;
        // (a thrown member can keep the grounded byte +0x137 for part of its flight: its height over the ground counts)
        float agl = drive::over_ground(game::position(c));
        if (agl > kMirrorAirMin || drive::body_airborne(c)) {
            v.push_back({e_.id, agl});
            up.insert(e_.id);
        }
    });
    if (g_mirror_hang_fix) {
        for (uint16_t id : g_air_last)
            if (!up.count(id)) { // landed (or gone: a guest ignores an unknown id)
                v.push_back({id, 0.f});
                g_anim.air_landed++;
            }
        g_air_last.swap(up);
    }
    for (size_t at = 0; at < v.size(); at += 64) {
        size_t n = std::min<size_t>(64, v.size() - at);
        net::Writer w;
        wire::tribe_anim::Air air; // (C7: kind 9)
        air.v.assign(v.begin() + (long)at, v.begin() + (long)(at + n));
        air.write(w);
        for (auto& p : C.sess->players())
            if (p.id != 0)
                anim_send(p.id, w, false);
    }
    g_anim.air_sent += (uint32_t)v.size();
}

// BUG-0936: same-view pairs differed in the pose of members at rest (one stands, the other sits or crouches at the
// fire), BUG-0914 in what a staff does. Probe (lane 4, tribe fixture, 5 min, both games' current animation of every
// player member once a second): 72 % of the pairs the same; the rest mostly the guest's mirror still in a relayed
// behaviour animation (the idle variants 0x02C39001..3, a gesture 0x04C6CB02) while the host's member, whose tree had
// cut it short, walked or stood in its idle (0x04866DAB / 0x04866D8E / 0x02481DE5): the relay starts animations and
// a mirror keeps one until it ends (no tree to cut it); the snapshot (kind 3) sends only behaviour animations. And
// the host's own idle choices (its idle refresh, Creature_IdleRefresh 0xC148B0) are never relayed. Now the host
// notes each member's idle refresh plays and once a second sends, for every member whose current animation is not
// its behaviour animation, that animation with whether its idle refresh chose it (MSG_NPC_ANIM kind 10: u8 n, n x
// {u16 id, u32 anim, u8 1 idle choice | 2 another start}); a guest whose mirror plays something else (not within 1.5 s
// of a relayed start, at most every 2 s) ends it: the idle refresh (forced) for an idle choice, so its own game picks
// idle or walk as its locomotion has it, or the host's animation played for another start. `coop.fix
// tribe.mirror_idle off`: nothing sent or done (a relayed animation plays to its end).
bool g_mirror_idle_fix = true;
COOP_FIX("tribe.mirror_idle", "BUG-0936", g_mirror_idle_fix, "tribe: a guest's mirror stops a member's animation when the host's member has gone back to its idle or walk (members at rest had other poses)");
// BUG-0972: the idle fix left a mirror alone whose animation was not the last relayed start (taken for its own idle or
// walk). A relayed restart of a gesture the mirror still played waited behind it as a pending track (BUG-0992); the
// next relayed start (the change 0x02C39200) played, and after it the waiting gesture came back: the guest's copy of
// the host's chief swung its staff for 37 s while the host's stood (lab probe: the chief's animation the same on both
// games in 59 % of the samples, 257 of the others host idle 0x02481DE5 / mirror gesture 0x04C6CB02; the same_view
// stills showed it standing on one game and in a gesture or a step on the other). Now a mirror in any animation that
// is not its locomotion's idle or walk, while the host's member is in its idle or walk, has its waiting starts ended
// (AnimatedCreature_ClearPending) and its idle refreshed. `coop.fix tribe.mirror_stale_anim off`: as before.
bool g_stale_anim_fix = true;
COOP_FIX("tribe.mirror_stale_anim", "BUG-0972", g_stale_anim_fix,
         "tribe: a guest's mirror stops a gesture that came back after another animation when the host's member stands or walks (the chief swung its staff on one game only)");
// BUG-1071: a member whose behaviour tree itself started its locomotion's idle or walk (bhv_anim one of those ids, as
// the host's chief after the save's load, before any guest joined: nothing relayed) went to the guests as "plays its
// behaviour animation" (kind 3), not as its idle (kind 10). Kind 3 starts that idle on the mirror like a relayed start:
// over a gesture it waits behind it (a pending track, #tribe-anim-queued-start) and the stale-gesture cut (BUG-0972)
// never ran: the mirror stayed in a gesture for 9 s while the host's chief stood. Now a member in its locomotion's idle
// or walk is always reported as its idle (kind 10, how 1). `coop.fix tribe.mirror_idle_loco off`: as before.
static bool g_idle_loco_fix = true;
COOP_FIX("tribe.mirror_idle_loco", "BUG-1071", g_idle_loco_fix,
         "tribe: a guest's mirror leaves a gesture when the host's member stands in an idle its behaviour started (right after the session start)");
// BUG-1069: the moment (kind 14) went only for a member whose current animation its behaviour tree started while the
// host tracked it (bhv_anim). The host's chief of the save stood in its long staff gesture 0x04C6CB02 started at the
// load, before the session (bhv_anim 0, t 16-22 s): the guest's mirror played it from its own start (t 0.6-6 s) and
// never got the host's moment: the staff forward in the hand on the host, another pose (or still the resting grip) on
// the guest in same_view_tribe's stills. The host's own track of that gesture can also loop (+0xA0 -1.0) for 30 s+
// while the mirror played single passes that ended and came back. Now the moment of any current animation but the
// locomotion's idle / walk goes too, loops included, with the track's loop setting and passes (kind 15); the guest
// sets them only when its mirror plays the same id. `coop.fix tribe.mirror_phase_any off`: as before.
bool g_phase_any_fix = true;
COOP_FIX("tribe.mirror_phase_any", "BUG-1069", g_phase_any_fix,
         "tribe: a guest's copy of a member is at the host's moment also of a gesture that began before the session (the chief's staff held another way)");
// BUG-1069: a relayed restart of the animation a host's hold was bound to under 0.5 s before is not played (the mirror
// keeps the running instance and the hold). `coop.fix tribe.hold_restart off`: played, the hold lost until the resend.
bool g_hold_restart_fix = true;
COOP_FIX("tribe.hold_restart", "BUG-1069", g_hold_restart_fix,
         "tribe: a guest's copy of a member keeps the item in its hand when the host's member restarts its gesture with it");
static bool loco_anim(uint32_t a)
{
    return a == 0x02481DE5u || a == 0x04866D8Eu || a == 0x04866DABu || a == 0x04866DD4u;
}
// BUG-0988: the guest's copy of a member plays the host's behaviour animation from when the relay's start reached it
// (one network delay and up to a cycle later): the same gesture at another moment of it, the chief's staff at another
// angle in the same_view stills (0.1-0.3 s in the hold lab). Once a second the host sends where in its behaviour
// animation each member is (MSG_NPC_ANIM kind 14: u8 n, n x {u16 id, u32 anim, f32 time in it, s}); a guest whose mirror
// plays the same animation (not a loop) more than 0.1 s from that time plus half the round trip sets its track's time
// there (the track's +0xA8). `coop.fix tribe.mirror_phase off`: nothing sent or set.
bool g_mirror_phase_fix = true;
COOP_FIX("tribe.mirror_phase", "BUG-0988", g_mirror_phase_fix,
         "tribe: a guest's copy of a member is at the same moment of a gesture as the host's member (the chief's staff pointed another way)");
// BUG-1072: the host's chief in the same main animation at the same moment on both games still stood another way (bent /
// upright, the staff at another angle). Probes of every playing track (lab lane 7, docs/re/tribe.md#tribe-chief-tracks):
// besides the main track the chief plays a second looping track (0x05EF4EE1, a ~2 s loop, also 0x0354559B) whose time
// was 1-31 s apart on the two games (each game started it itself), and its locomotion idle 0x02481DE5 (left out of
// kind 15) 0.4-6.8 s apart. Now the host also sends, once a second, the time / loop setting / passes of every member's
// other playing tracks and of its idle (MSG_NPC_ANIM kind 16, the format of kind 15), and a guest whose mirror plays a
// track of that id gives it the host's. `coop.fix tribe.mirror_phase_tracks off`: only the main track (kind 15).
bool g_phase_tracks_fix = true;
COOP_FIX("tribe.mirror_phase_tracks", "BUG-1072", g_phase_tracks_fix,
         "tribe: a guest's copy of a member plays its other animation layers (the chief's posture loop, its idle) at the host's moment");
double g_phase_last_off;
void on_idle_anim(void* c, uint32_t anim)
{
    if (!g_ok || !is_host())
        return;
    if (HEntry* it = hm_of(c))
        it->data.idle_anim = anim;
}
uint32_t hold_modifier(void* c, bool end);
static void send_member_holds()
{
    std::vector<std::pair<uint16_t, uint32_t>> v, ends;
    std::vector<uint8_t> items; // BUG-0971: v's items
    static double again_at; // once a second every hold again (a mirror that played the animation later, a lost start)
    double now = H->time_seconds();
    bool again = now - again_at >= 1.0 || now < again_at;
    if (again)
        again_at = now;
    g_host.members.for_each([&](HEntry& e_) {
        void* const c = e_.ref.p;
        HMember& m = e_.data;
        uint32_t h = kept_alive(c, e_.ref.serial, 0x18EB4B7u) && F<int>(c, kHeldItemOff) ? hold_modifier(c, false) : 0;
        uint32_t cur = 0;
        if (h && (h != m.hold || again) && v.size() < 64 && game::drawn_state(c, nullptr, &cur) && cur) {
            v.push_back({e_.id, cur});
            int it = F<int>(c, kHeldItemOff);
            items.push_back((uint8_t)(it > 0 && it <= 0x17 ? it : 0));
        }
        else if (!h && m.hold && ends.size() < 64)
            ends.push_back({e_.id, 0}); // the hold ended (the item back in the resting grip)
        m.hold = h;
    });
    if (!g_mirror_hold_fix)
        return;
    for (int reliable = 0; reliable < 2; reliable++) { // (an end must not be lost: the mirror would hold on for 8 s)
        auto& list = reliable ? ends : v;
        if (list.empty())
            continue;
        net::Writer w;
        wire::tribe_anim::Hold hold; // (C7: kinds 11 / 12)
        hold.with_item = !reliable && g_hold_item_fix; // (BUG-0971: starts carry the item)
        for (size_t k = 0; k < list.size(); k++)
            hold.v.push_back({list[k].first, list[k].second, hold.with_item ? items[k] : (uint8_t)0});
        hold.write(w);
        for (auto& p : C.sess->players())
            if (p.id != 0) {
                if (reliable)
                    anim_send(p.id, w, true);
                else
                    anim_send(p.id, w, false);
            }
        g_anim.hold_sent += (uint32_t)list.size();
    }
}
// BUG-0992: the attach (0xC240E0) binds the hold modifier to the track of the current animation's id, looking in the
// animation state's pending tracks (+0xF4C, 16 indices) before the playing ones (+0xF0C): a start of the same id waiting
// behind the running one (a relayed restart of a gesture) got the hold, whose tick (0xC11240) ends it while that track
// does not play. When a pending track has the current id, the pending tracks are cleared with the game's own call
// (0xA02B00, as CreaturePlayAnim does for its `a`): the mirror goes on in the running instance, the hold binds to it.
bool clear_pending_same(void* c, uint32_t cur)
{
    uint8_t* ac = has_body(c) ? F<uint8_t*>(c, 0xB54) : nullptr;
    uint8_t* st = ac && skg::readable(ac + 0x184, 4) ? *(uint8_t**)(ac + 0x184) : nullptr;
    if (!st || !cur || !a_ClearPending || !skg::readable(st, 0xFAC))
        return false;
    bool same = false;
    for (int i = 0; i < 16 && !same; i++) {
        uint32_t k = *(uint32_t*)(st + 0xF4C + i * 4);
        if (k > 0xF || (!g_hold_track_fix && k == *(uint32_t*)(st + 0xFA8)))
            continue;
        uint8_t* tr = st + 8 + k * 0xF0;
        same = *(uint32_t*)tr && *(uint32_t*)(tr + 0xD8) == cur;
    }
    if (same) {
        thiscall<void>(a_ClearPending, ac);
        g_anim.hold_queued++;
    }
    return same;
}
// BUG-0988: the main animation track of a creature, as GetCurrentAnimation (0xA05090) finds it (0x9FF690): the
// animation state (AnimatedCreature +0x184), the main index +0xFA4 into the playing list +0xF0C, tracks of 0xF0 bytes
// from +8; a track's id +0xD8, its time in the animation +0xA8 (double, s), +0xA0 -1.0 when it loops
uint8_t* main_track(void* c)
{
    uint8_t* ac = has_body(c) ? F<uint8_t*>(c, 0xB54) : nullptr;
    uint8_t* st = ac && skg::readable(ac + 0x184, 4) ? *(uint8_t**)(ac + 0x184) : nullptr;
    if (!st || !skg::readable(st, 0xFAC))
        return nullptr;
    uint32_t i = *(uint32_t*)(st + 0xFA4);
    if (i > 0xF)
        return nullptr;
    uint32_t k = *(uint32_t*)(st + 0xF0C + i * 4);
    if (k > 0xF)
        return nullptr;
    uint8_t* tr = st + 8 + k * 0xF0;
    return *(uint32_t*)tr ? tr : nullptr;
}
// BUG-1347: handle of the creature's live (state 1 fading-in or 2 playing) main animation track, matching 0x9FF630
uint32_t main_track_handle(void* c)
{
    uint8_t* ac = has_body(c) ? F<uint8_t*>(c, 0xB54) : nullptr;
    uint8_t* st = ac && skg::readable(ac + 0x184, 4) ? *(uint8_t**)(ac + 0x184) : nullptr;
    if (!st || !skg::readable(st, 0xFAC))
        return 0;
    uint32_t i = *(uint32_t*)(st + 0xFA4);
    if (i > 0xF)
        return 0;
    uint32_t k = *(uint32_t*)(st + 0xF0C + i * 4);
    if (k > 0xF)
        return 0;
    uint8_t* tr = st + 8 + k * 0xF0;
    if (!*(uint32_t*)tr || !tribe_logic::track_live_state(*(int*)(tr + 0xC0)))
        return 0;
    return tribe_logic::track_handle(k, *(uint32_t*)(tr + 0xB4));
}
// BUG-1347: after Citizen_SetHandheldItem attaches an item (0xC240E0), 0x9FF700 may have bound the kind-5 hold
// modifier to an earlier pending (state 0) or fading-out (state 3) track of the same animation id, causing the next
// tick (0xC11240) to delete the hold immediately. Rebind any kind-5 hold modifier onto the live main track handle.
bool rebind_hold_to_main(void* c)
{
    uint32_t want = main_track_handle(c);
    uint8_t* tab = want && has_body(c) ? F<uint8_t*>(c, kModifiersOff) : nullptr;
    if (!tab || !skg::readable(tab, 8 * 0x44))
        return false;
    bool rebound = false;
    for (uint32_t i = 0; i < 8; i++) {
        uint8_t* e = tab + i * 0x44;
        if (*(int*)(e + 0x24) != 5 || *(int*)(e + 0x28) != 0 || *(void**)(e + 0x3C) != c)
            continue;
        if (*(uint32_t*)(e + 0x04) != want) {
            *(uint32_t*)(e + 0x04) = want;
            g_anim.hold_rebound++;
            rebound = true;
        }
    }
    return rebound;
}
bool track_loops(uint8_t* tr) { return *(float*)(tr + 0xA0) == -1.0f; }
// guest: the mirror takes its item in the host's animation when it plays it (kind 11, or right after the relay played it)
void hold_try(GEntry& e, double now)
{
    GMember& g = e.data;
    void* const c = e.ref.p;
    if (!g.hold_anim || now - g.hold_at > 3.0 || !mirror_ok(e) || !has_body(c) || !a_SetHandheldItem)
        return;
    int item = F<int>(c, kHeldItemOff);
    bool other_item = g_hold_item_fix && g.hold_item > 0 && item != g.hold_item; // BUG-0971
    if (other_item)
        item = g.hold_item;
    uint32_t cur = 0;
    if (g_hold_anim_fix && item > 0 && game::drawn_state(c, nullptr, &cur) && cur != g.hold_anim && now - g.hold_at < 0.5 &&
        now - g.hold_play_at >= 1.0 && !game::is_dead(c)) { // BUG-0968: the host's member plays it now
        g.hold_play_at = now;
        if (game::play_behaviour_anim(c, g.hold_anim, 0, 1, -1)) {
            g.relayed_anim = g.hold_anim;
            g.anim_rx_at = now; // (as a relayed start: the idle fix leaves it alone for 1.5 s)
            g_anim.hold_anim_plays++;
            hold_ev(member_id(c), 'P', g.hold_anim, cur);
        }
    }
    bool queued = false;
    if (item > 0 && game::drawn_state(c, nullptr, &cur) && cur && cur != g.hold_anim && g_hold_anim_fix &&
        g_hold_queued_fix && now - g.hold_at < 1.0 && now - g.hold_play_at >= 0.2 && now - g.hold_play_at < 1.0 &&
        !game::is_dead(c)) { // BUG-0992: the started animation waits behind the mirror's current one
        queued = true;
        g_anim.hold_queued++;
        hold_ev(member_id(c), 'Q', g.hold_anim, cur);
    }
    if (item <= 0 || !game::drawn_state(c, nullptr, &cur) || (cur != g.hold_anim && !queued)) {
        hold_ev(member_id(c), 't', g.hold_anim, item <= 0 ? 0xFFFFFFFFu : cur);
        return;
    }
    hold_ev(member_id(c), 'T', cur, (hold_modifier(c, false) ? 1 : 0) | (other_item ? 2 : 0) | (uint32_t)item << 8);
    if (other_item || !hold_modifier(c, false)) {
        if (other_item)
            g_anim.hold_item_sets++;
        // (the attach adds the hold modifier with the animation playing now; it also sizes the model again by the
        // mirror's own size: the size it had, the host's (tribe.item_scale, BUG-0912), is put back at once)
        float sc = read_item_scale(c);
        if (!hand_ready(c)) { // BUG-1335: not before its body has the hand's bone (the host's next hold tries again)
            g_hand_waits++;
            return;
        }
        if (g_hold_queued_fix && clear_pending_same(c, cur))
            hold_ev(member_id(c), 'W', cur, 0); // BUG-0992
        set_hand(c, item);
        if (g_hold_track_fix && rebind_hold_to_main(c))
            hold_ev(member_id(c), 'F', cur, main_track_handle(c)); // BUG-1347
        if (g_item_scale_fix && sc != 1.f)
            apply_item_scale(c, sc);
        g.hold_set_at = now; // BUG-1347: held_items_tick checks that this attach's hold started
        g.hold_retries = 0;
    } else
        g.hold_set_at = 0;
    g.hold_bound = true;
    g.hold_at = now; // (bound: held_items_tick lets it end with the animation)
    g.hold_anim_bound = queued ? cur : g.hold_anim;
    g.hold_anim = 0;
    g_anim.hold_bound++;
}

void send_member_anims(double now)
{
    send_member_holds(); // BUG-0914
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
                anim_send(p.id, w, false);
        g_anim.anim_relay_sent += (uint32_t)n;
    }
    g_member_anim_out.clear();
    if (now - g_member_snap_at < 1.0)
        return;
    g_member_snap_at = now;
    std::vector<std::pair<uint16_t, uint32_t>> v;
    struct IdleNow {
        uint16_t id;
        uint32_t anim;
        uint8_t how;
    };
    std::vector<IdleNow> idle; // BUG-0936
    g_host.members.for_each([&](HEntry& e_) {
        void* const c = e_.ref.p;
        HMember& m = e_.data;
        uint32_t cur = 0;
        // (BUG-1071: with the fix the animation is read for a member with no behaviour start yet too, bhv_anim 0: it
        // was never reported at all)
        bool drawn = (m.bhv_anim || g_idle_loco_fix) && hm_ok(e_) && game::drawn_state(c, nullptr, &cur);
        if (m.bhv_anim && drawn && cur && cur == m.bhv_anim && !(g_idle_loco_fix && g_mirror_idle_fix && loco_anim(cur)))
            v.push_back({e_.id, cur});
        else if (g_mirror_idle_fix && cur && idle.size() < 255 && !game::is_dead(c))
            idle.push_back({e_.id, cur, (uint8_t)(cur == m.idle_anim ? 1 : 2)});
    });
    if (!idle.empty()) {
        net::Writer w;
        w.u8(10);
        w.u8((uint8_t)idle.size());
        for (auto& e : idle) {
            w.u16(e.id);
            w.u32(e.anim);
            w.u8(e.how);
        }
        for (auto& p : C.sess->players())
            if (p.id != 0)
                anim_send(p.id, w, false);
        g_anim.idle_sent += (uint32_t)idle.size();
    }
    if (g_mirror_phase_fix) { // BUG-0988: where in its behaviour animation each member is now
        // ph: kind 14 (the behaviour's start, not a loop: as 0.7.13); ph2: kind 15, BUG-1069, every member's current
        // animation but the locomotion's idle / walk, loops too, with the track's +0xA0 / +0xB0 (a guest with
        // tribe.mirror_phase_any off ignores kind 15: the twin works from its join)
        std::vector<std::tuple<uint16_t, uint32_t, float>> ph;
        struct Ph2 {
            uint16_t id;
            uint32_t anim;
            float t, a0;
            uint16_t b0;
        };
        std::vector<Ph2> ph2;
        g_host.members.for_each([&](HEntry& e_) {
            void* const c = e_.ref.p;
            HMember& m = e_.data;
            uint8_t* tr;
            uint32_t cur = 0;
            if (!hm_ok(e_) || !game::drawn_state(c, nullptr, &cur) || !cur || !(tr = main_track(c)) ||
                *(uint32_t*)(tr + 0xD8) != cur)
                return;
            if (ph.size() < 120 && m.bhv_anim && cur == m.bhv_anim && !track_loops(tr))
                ph.emplace_back(e_.id, cur, (float)*(double*)(tr + 0xA8));
            if (g_phase_any_fix && ph2.size() < 64 && !loco_anim(cur)) // (64 x 16 bytes in one packet)
                ph2.push_back({e_.id, cur, (float)*(double*)(tr + 0xA8), *(float*)(tr + 0xA0),
                               (uint16_t)std::clamp(*(int*)(tr + 0xB0), 0, 0xFFFF)});
        });
        if (!ph.empty()) {
            net::Writer w;
            w.u8(14);
            w.u8((uint8_t)ph.size());
            for (auto& [id, an, t] : ph) {
                w.u16(id);
                w.u32(an);
                w.f32(t);
            }
            for (auto& p : C.sess->players())
                if (p.id != 0)
                    anim_send(p.id, w, false);
            g_anim.phase_sent += (uint32_t)ph.size();
        }
        if (!ph2.empty()) {
            net::Writer w;
            w.u8(15);
            w.u8((uint8_t)ph2.size());
            for (auto& e : ph2) {
                w.u16(e.id);
                w.u32(e.anim);
                w.f32(e.t);
                w.f32(e.a0);
                w.u16(e.b0);
            }
            for (auto& p : C.sess->players())
                if (p.id != 0)
                    anim_send(p.id, w, false);
            g_anim.phase_sent += (uint32_t)ph2.size();
        }
        if (g_phase_tracks_fix) { // BUG-1072: kind 16, the other playing tracks and the idle (the format of kind 15)
            std::vector<Ph2> ph3;
            g_host.members.for_each([&](HEntry& e_) {
                void* const c = e_.ref.p;
                if (!hm_ok(e_) || game::is_dead(c))
                    return;
                playing_tracks(c, [&](uint8_t* tr, bool main) {
                    uint32_t an = *(uint32_t*)(tr + 0xD8);
                    // (the main one goes with kind 15 / 14, but the idle; a walk's time follows its speed)
                    if (main ? an != 0x02481DE5u : *(int*)(tr + 0xC0) == 3) // (3: fading out)
                        return;
                    if (ph3.size() < 255)
                        ph3.push_back({e_.id, an, (float)*(double*)(tr + 0xA8), *(float*)(tr + 0xA0),
                                       (uint16_t)std::clamp(*(int*)(tr + 0xB0), 0, 0xFFFF)});
                });
            });
            const size_t per = 64; // (64 x 16 bytes in one packet)
            for (size_t i = 0; i < ph3.size(); i += per) {
                size_t n = std::min(per, ph3.size() - i);
                net::Writer w;
                w.u8(16);
                w.u8((uint8_t)n);
                for (size_t j = i; j < i + n; j++) {
                    w.u16(ph3[j].id);
                    w.u32(ph3[j].anim);
                    w.f32(ph3[j].t);
                    w.f32(ph3[j].a0);
                    w.u16(ph3[j].b0);
                }
                for (auto& p : C.sess->players())
                    if (p.id != 0)
                        anim_send(p.id, w, false);
            }
            g_anim.tracks_sent += (uint32_t)ph3.size();
        }
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
                anim_send(p.id, w, false);
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
// kinds.
using ThemePlay_t = int(SK_THISCALL*)(void*, uint32_t, float, float, float);
using ThemeMove_t = void(SK_THISCALL*)(void*, int, float, float, float);
using ThemeStop_t = void(SK_THISCALL*)(void*, int);
static ThemePlay_t o_ThemePlay;
static ThemeMove_t o_ThemeMove;
static ThemeStop_t o_ThemeStop;
static bool g_theme_relaying; // guest: our own call of the game's functions
CALL_GUARD("tribe.theme_relaying", g_theme_relaying); // BUG-1047
static double g_theme_moved_at[24];
static int g_theme_local[24]; // guest: host slot -> this game's slot (0 none)

static void theme_send(uint8_t kind, const net::Writer& w)
{
    for (auto& p : C.sess->players())
        if (p.id != 0) {
            if (kind == 5)
                anim_send(p.id, w, false);
            else
                anim_send(p.id, w, true);
        }
    g_anim.theme_sent++;
}
static bool theme_host() { return g_ok && C.sess && is_host() && game::mode_id() == modes::kTribe; }
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
void theme_hooks_install()
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
// BUG-1496 (audit TR-11): theme_forget at the session's end only forgot the slots the relay plays, the guest's game
// stays in Tribe and played them on; and a kind 6 stop that came while the guest was not in Tribe (an editor) was lost
// (on_member_anims returns outside Tribe), the slot played on until its next play or the stage's end. BUG-0890's shape
// (party music, fixed for kind 8). Now the session's end stops them in Tribe, and a stop outside Tribe is kept and runs
// when this game is back in Tribe in the same world (theme_tick; a world change forgets it: another theme manager).
// `coop.fix tribe.theme_end off`: as 0.7.16.1.
static bool g_theme_end_fix = true;
COOP_FIX("tribe.theme_end", "BUG-1496", g_theme_end_fix,
         "tribe: the host's fight and hunt music stops on the guest at the session's end and when its stop came during an editor");
static int g_theme_later[24]; // guest: this game's slot of a stop that came outside Tribe (0 none)
uint32_t g_theme_ended, g_theme_later_n, g_theme_lost;
static void theme_end_slot(uint8_t slot, bool session_end)
{
    int& local = g_theme_local[slot];
    if (!local)
        return;
    void* mgr = game::mode_id() == modes::kTribe ? theme_mgr() : nullptr;
    switch (tribe_logic::theme_end(g_theme_end_fix, mgr && o_ThemeStop, session_end)) {
    case tribe_logic::ThemeEnd::Stop: {
        callguard::Hold theme_relaying(g_theme_relaying);
        o_ThemeStop(mgr, local);
        g_theme_ended++;
        break;
    }
    case tribe_logic::ThemeEnd::Later:
        g_theme_later[slot] = local;
        g_theme_later_n++;
        break;
    case tribe_logic::ThemeEnd::Lost:
        g_theme_lost++;
        break;
    }
    local = 0;
}
void theme_forget() // a world change: the slots were the old theme manager's
{
    memset(g_theme_local, 0, sizeof g_theme_local);
    memset(g_theme_later, 0, sizeof g_theme_later);
}
void theme_session_end()
{
    for (uint8_t slot = 1; slot < 24; slot++)
        theme_end_slot(slot, true);
    memset(g_theme_local, 0, sizeof g_theme_local);
}
void theme_stop_outside(net::Reader& r) // guest, kind 6 while this game is not in Tribe
{
    uint8_t slot = r.u8();
    if (!r.ok() || slot == 0 || slot >= 24)
        return;
    g_anim.theme_rx++;
    if (g_theme_end_fix)
        theme_end_slot(slot, false);
    else if (g_theme_local[slot])
        g_theme_lost++;
}
void theme_tick() // every frame in Tribe (also after the session): the stops kept for the return
{
    void* mgr = nullptr;
    for (uint8_t slot = 1; slot < 24; slot++)
        if (g_theme_later[slot]) {
            if (!mgr && !(mgr = theme_mgr()))
                return;
            {
                callguard::Hold theme_relaying(g_theme_relaying);
                o_ThemeStop(mgr, g_theme_later[slot]);
            }
            g_theme_later[slot] = 0;
            g_theme_ended++;
            H->log(M, SK_INFO, "tribe: the host's theme slot %u stopped here on the return to Tribe (BUG-1496)", slot);
        }
}
void on_theme(uint8_t kind, net::Reader& r)
{
    uint8_t slot = r.u8();
    float x = 0, y = 0, z = 0;
    uint32_t sound = kind == 4 ? r.u32() : 0;
    if (kind != 6) {
        x = r.f32(); y = r.f32(); z = r.f32();
    }
    if (!r.ok() || slot == 0 || slot >= 24)
        return;
    g_anim.theme_rx++;
    void* mgr = theme_mgr();
    if (!mgr || !o_ThemePlay)
        return;
    callguard::Hold theme_relaying(g_theme_relaying);
    int& local = g_theme_local[slot];
    if (kind == 4) {
        if (local)
            o_ThemeStop(mgr, local);
        local = o_ThemePlay(mgr, sound, x, y, z);
        if (local)
            g_anim.theme_played++;
    } else if (kind == 5 && local) {
        o_ThemeMove(mgr, local, x, y, z);
    } else if (kind == 6 && local) {
        o_ThemeStop(mgr, local);
        local = 0;
    }
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
bool g_party_fix = true;
COOP_FIX("tribe.party_music", "BUG-0646", g_party_fix, "tribe: a guest hears the music of the dance at its tribe's fire pit as the host does");
static constexpr uint32_t kPartyMusic = 0x19DC06DF;
using PlayProc_t = void(SK_CDECL*)(uint32_t, uint32_t, float, float, float);
using StopAudio_t = void(SK_CDECL*)(uint32_t, int);
static PlayProc_t o_PlayProc;
static StopAudio_t o_StopAudio;
static uintptr_t a_AudioGet;
static bool g_party_relaying;            // guest: our own call of the game's functions
CALL_GUARD("tribe.party_relaying", g_party_relaying); // BUG-1047
static uint32_t g_party_host_track;      // host: the party music's track now (0 none)
static uint32_t g_party_rx_track, g_party_local; // guest: the host's track and this game's track for it
static uint32_t g_party_own;             // guest: a party music track its own game plays

static bool party_host() { return g_ok && g_party_fix && C.sess && is_host() && game::mode_id() == modes::kTribe; }
// BUG-0890: "the fire-dance music plays on on the guest". The host sent the stop only while its own game was in Tribe and
// a guest dropped every MSG_NPC_ANIM while its game was not in Tribe (an editor, a load), so a stop that came then was
// lost and the guest's track played on until its next world change. Now the host sends the stop of a track it relayed
// whatever its mode, and a guest takes a stop (kind 8) in any mode. `coop.fix tribe.party_stop off`: the old checks.
bool g_party_stop_fix = true;
COOP_FIX("tribe.party_stop", "BUG-0890", g_party_stop_fix, "tribe: the end of the dance music reaches the guest also when the host or the guest is not in Tribe at that moment");
uint32_t g_party_stops_outside; // stops sent or taken outside Tribe (tests)
static void party_send(uint8_t kind, uint32_t track, uint32_t sound, float x, float y, float z)
{
    net::Writer w;
    w.u8(kind); w.u32(track);
    if (kind == 7) {
        w.u32(sound); w.f32(x); w.f32(y); w.f32(z);
    }
    for (auto& p : C.sess->players())
        if (p.id != 0)
            anim_send(p.id, w, true);
    g_anim.party_sent++;
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
        else if (g_party_stop_fix && g_party_fix && C.sess && is_host()) { // BUG-0890: relayed before, stopped outside Tribe
            party_send(8, track, 0, 0, 0, 0);
            g_party_stops_outside++;
        }
    } else if (track == g_party_own) {
        g_party_own = 0;
    }
}
void party_hooks_install()
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
        {
            callguard::Hold party_relaying(g_party_relaying);
            o_StopAudio(g_party_local, 0);
        }
        g_anim.party_stopped++;
    }
    g_party_local = g_party_rx_track = 0;
}
void party_forget()
{
    party_stop_local(); // (the audio system outlives the world: a track left playing would play on)
    g_party_host_track = g_party_own = 0;
}
void on_party(uint8_t kind, net::Reader& r)
{
    uint32_t track = r.u32(), sound = 0;
    float x = 0, y = 0, z = 0;
    if (kind == 7) {
        sound = r.u32(); x = r.f32(); y = r.f32(); z = r.f32();
    }
    if (!r.ok() || !track)
        return;
    g_anim.party_rx++;
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
    {
        callguard::Hold party_relaying(g_party_relaying);
        o_PlayProc(sound, mine, x, y, z);
    }
    g_party_local = mine;
    g_party_rx_track = track;
    g_anim.party_played++;
}

} // namespace tribe
