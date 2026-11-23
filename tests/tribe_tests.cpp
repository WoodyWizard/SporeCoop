// Native tests for tribe_logic.h (make test, D3 of 0.7.15): the Tribe stage's rules of owner dealing, member hold and
// snapshot apply, and the per-peer message type of the member animations (wire/tribe_anim.h, C7). Where a rule has a
// switch, the case runs with it off too and must show the old behaviour, so the test is known to catch it.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "tribe_logic.h"
#include "drive_plan.h"
#include "bone_rules.h"
#include "tribe_editors.h"
#include "wire/tribe_anim.h"

using namespace tribe_logic;

static int g_fail = 0;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                             \
        }                                                         \
    } while (0)

// a member met first: its claim, else the players in turns (the player tribe only), a purchase over both
static void owners()
{
    printf("owner dealing: claims, turns, purchases, the split (E1 BUG-1154, BUG-0671)\n");
    std::vector<uint8_t> pl = {0, 1, 2};
    uint32_t turn = 0;
    CHECK(new_owner(true, -1, pl, turn, -1, false) == 0);
    CHECK(new_owner(true, -1, pl, turn, -1, false) == 1);
    CHECK(new_owner(true, 2, pl, turn, -1, false) == 2 && turn == 2); // a claim takes no turn
    CHECK(new_owner(true, -1, pl, turn, -1, false) == 2);
    CHECK(new_owner(true, -1, pl, turn, -1, false) == 0 && turn == 4); // the turns wrap
    // another tribe's member is nobody's, and takes no turn (a claim is only asked for the player tribe)
    CHECK(new_owner(false, -1, pl, turn, -1, false) == kNobody && turn == 4);
    // BUG-0671: bought for guest 2, still a player: its own, over a claim and over the turn (the turn still counts)
    CHECK(new_owner(true, -1, pl, turn, 2, true) == 2 && turn == 5);
    CHECK(new_owner(true, 1, pl, turn, 2, true) == 2 && turn == 5);
    // ... gone since: the turn's, and in another tribe never
    CHECK(new_owner(true, -1, pl, turn, 2, false) == pl[5 % 3]);
    CHECK(new_owner(false, -1, pl, turn, 2, true) == kNobody);
    // no players (a host alone before its session): nobody, no division by zero
    std::vector<uint8_t> none;
    uint32_t t0 = 0;
    CHECK(new_owner(true, -1, none, t0, -1, false) == kNobody && t0 == 0);
    // the split when the players change: claims first, the rest in turns from 0
    uint32_t t = 0;
    CHECK(resplit_owner(-1, pl, t) == 0 && resplit_owner(1, pl, t) == 1 && resplit_owner(-1, pl, t) == 1 && t == 2);
    CHECK(resplit_owner(-1, none, t) == kNobody);
}

// E1: a newcomer gets an even share, plain members first, nobody below the share
static void balance_share()
{
    printf("owner dealing: a newcomer's even share (E1 BUG-1154)\n");
    // 7 members, host 0 has 6 (two of them born for it: 10, 11), guest 1 has 1, guest 2 joins with none
    std::map<uint8_t, std::vector<int>> by = {{0, {10, 11, 12, 13, 14, 15}}, {1, {16}}};
    std::vector<uint8_t> pl = {0, 1, 2};
    auto plain = [](int m) { return m != 10 && m != 11; };
    auto mv = balance(by, pl, 7, plain);
    CHECK(mv.size() == 2); // share 7 / 3 = 2
    CHECK(mv.size() == 2 && mv[0].first == 12 && mv[0].second == 2 && mv[1].first == 13 && mv[1].second == 2);
    CHECK(by[0].size() == 4 && by[2].size() == 2 && by[1].size() == 1); // guest 1 had one: no newcomer, gets nothing
    // the richest gives, never below the share: 0 has 3, 1 has 3, 2 joins: share 2, one from each? the richest each time
    std::map<uint8_t, std::vector<int>> b2 = {{0, {1, 2, 3}}, {1, {4, 5, 6}}};
    auto m2 = balance(b2, pl, 6, [](int) { return true; });
    CHECK(m2.size() == 2 && b2[0].size() == 2 && b2[1].size() == 2 && b2[2].size() == 2);
    // only born members left: the first one is taken (plain first, then any)
    std::map<uint8_t, std::vector<int>> b3 = {{0, {10, 11, 12, 13}}};
    auto m3 = balance(b3, std::vector<uint8_t>{0, 1}, 4, [](int m) { return m == 13; });
    CHECK(m3.size() == 2 && m3[0].first == 13 && m3[1].first == 10);
    // a player who has members is no newcomer; no players: nothing
    std::map<uint8_t, std::vector<int>> b4 = {{0, {1, 2, 3, 4}}, {1, {5}}};
    CHECK(balance(b4, std::vector<uint8_t>{0, 1}, 5, [](int) { return true; }).empty());
    CHECK(balance(b4, std::vector<uint8_t>{}, 5, [](int) { return true; }).empty());
}

// BUG-1450: the game's leader (the host's chief #1, first in member order and plain) never moves; off (the 4-argument
// form, tribe.balance_leader off) it is the first one given
static void balance_leader()
{
    printf("owner dealing: the balance keeps the leader (BUG-1450)\n");
    std::vector<uint8_t> pl = {0, 1};
    auto plain = [](int) { return true; };
    auto lead = [](int m) { return m == 1; };
    // the guest's rejoin with its claims lost: the host holds all 7 (#1 the leader first), the guest has none
    std::map<uint8_t, std::vector<int>> by = {{0, {1, 2, 3, 4, 5, 6, 7}}};
    auto mv = balance(by, pl, 7, plain, lead);
    CHECK(mv.size() == 3 && mv[0].first == 2 && mv[1].first == 3 && mv[2].first == 4);
    CHECK(std::find(by[0].begin(), by[0].end(), 1) != by[0].end() && by[1].size() == 3);
    std::map<uint8_t, std::vector<int>> old = {{0, {1, 2, 3, 4, 5, 6, 7}}};
    auto mo = balance(old, pl, 7, plain);
    CHECK(mo.size() == 3 && mo[0].first == 1 && mo[0].second == 1); // off: the leader goes to the guest (the bug)
    // the leader the only plain one (the rest born or given): the next member is given, not the leader
    std::map<uint8_t, std::vector<int>> b2 = {{0, {1, 2, 3, 4}}};
    auto m2 = balance(b2, pl, 4, [](int m) { return m == 1; }, lead);
    CHECK(m2.size() == 2 && m2[0].first == 2 && m2[1].first == 3);
    // only the leader left above the share: nothing (no endless loop, no leader)
    std::map<uint8_t, std::vector<int>> b3 = {{0, {1}}};
    CHECK(balance(b3, std::vector<uint8_t>{0, 1}, 2, plain, lead).empty() && b3[0].size() == 1);
    std::map<uint8_t, std::vector<int>> b4 = {{0, {1, 2, 3}}};
    auto m4 = balance(b4, std::vector<uint8_t>{0, 1}, 3, plain, [](int m) { return m == 1 || m == 2; });
    CHECK(m4.size() == 1 && m4[0].first == 3);
}

// BUG-0620: the host keeps a dead member's body while the game does, at most kBodyMax s
static void bodies()
{
    printf("member hold: the bodies of BUG-0620\n");
    BodyHold h = body_hold(true, true, 0, 0, 100.0);
    CHECK(h.keep && h.since == 100.0); // first seen now
    h = body_hold(true, true, 0, h.since, 150.0);
    CHECK(h.keep && h.since == 100.0);
    h = body_hold(true, true, 0, h.since, 160.0);
    CHECK(h.keep); // exactly kBodyMax: still sent
    h = body_hold(true, true, 0, 100.0, 160.5);
    CHECK(!h.keep && h.since == 100.0); // longer: the entry goes
    CHECK(!body_hold(false, true, 0, 0, 100).keep);      // the object gone (or another one at its address)
    CHECK(!body_hold(true, false, 0, 0, 100).keep);      // alive and out of every list: not a body (it goes)
    CHECK(!body_hold(true, true, -1, 0, 100).keep);      // its tribe is gone
    CHECK(!body_hold(true, true, kMaxTribes, 0, 100).keep); // beyond the snapshot's tribe table
    CHECK(body_hold(true, true, kMaxTribes - 1, 0, 100).keep);
    CHECK(body_hold(true, false, 0, 0, 100).since == 0); // not a body: its time is not started
}

// the guest's per-entry decisions of a snapshot
static void apply()
{
    printf("snapshot apply: grow up, make a mirror, the bind candidate, the knock-out (BUG-0857, BUG-0665, BUG-0173)\n");
    CHECK(grow_up(true, 1, false, 0, false));
    CHECK(!grow_up(false, 1, false, 0, false)); // tribe.mirror_grow off: by its own timer
    CHECK(!grow_up(true, 0, false, 0, false) && !grow_up(true, 1, true, 0, false) && !grow_up(true, 1, false, 1, false) &&
          !grow_up(true, 1, false, 0, true));
    CHECK(make_mirror(false, false, false) && make_mirror(false, true, true));
    CHECK(!make_mirror(false, true, false)); // tribe.join_body off: no mirror for a body (0.7.10)
    CHECK(!make_mirror(true, false, true));
    std::vector<Candidate> v = {{0, 0, 0, false}, {3, 0, 0, true}, {4, 0, 0, false}, {10, 0, 0, false}};
    CHECK(pick_bind(v, 3.2f, 0, 0) == 2); // the nearest unbound (index 1 is bound to another mirror)
    CHECK(pick_bind(v, 0.5f, 0, 0) == 0);
    CHECK(pick_bind(v, 20, 0, 0) == -1);  // none within 6 m: spawn
    CHECK(pick_bind(v, 16, 0, 0) == -1);  // exactly 6 m: not nearer than 6 m
    CHECK(pick_bind(v, 15.9f, 0, 0) == 3);
    CHECK(pick_bind({}, 0, 0, 0) == -1);
    CHECK(ko(true, 0, false) == Ko::Death && ko(true, -1, true) == Ko::Death);
    CHECK(ko(true, 50, true) == Ko::Replace && ko(true, 50, false) == Ko::None);
    CHECK(ko(false, 0, true) == Ko::None); // tribe.ko off: the mirror stands with its last health
}

// C7 (BUG-1160): the member animations' own message type for a peer that knows it, MSG_NPC_ANIM for an older one
static void anim_type()
{
    printf("member animations: MSG_TRIBE_ANIM for a peer with the tribe family's minor, MSG_NPC_ANIM for 0.7.14 (C7)\n");
    using namespace wire::tribe_anim;
    std::vector<net::FamilyRev> none, old15 = {{6, 1, 0}}, new15 = {{6, 1, kMinor}}, later = {{6, 1, kMinor + 3}},
                                       rev2 = {{6, 2, 0}}, other = {{7, 1, 5}};
    CHECK(type_for(false, none, true) == net::MSG_NPC_ANIM);   // 0.7.14 and older: no families sent
    CHECK(type_for(true, old15, true) == net::MSG_NPC_ANIM);   // a 0.7.15 build before C7 (tribe 1.0)
    CHECK(type_for(true, new15, true) == net::MSG_TRIBE_ANIM);
    CHECK(type_for(true, later, true) == net::MSG_TRIBE_ANIM); // a later minor reads it too
    CHECK(type_for(true, rev2, true) == net::MSG_NPC_ANIM);    // another rev: refused anyway; never the new type
    CHECK(type_for(true, other, true) == net::MSG_NPC_ANIM);   // no tribe family in the list
    CHECK(type_for(true, new15, false) == net::MSG_NPC_ANIM);  // tribe.anim_msg off: 0.7.14's type for everyone
    // the kinds: exactly npc.cpp's whitelist of 0.7.14 (2..12, 14, 15, 16, 20, 21); the NPC's own kinds 1 and 13 are not
    int n = 0;
    for (int k = 0; k < 256; k++) {
        bool want = (k >= 2 && k <= 12) || k == 14 || k == 15 || k == 16 || k == 20 || k == 21;
        CHECK(is_tribe_kind((uint8_t)k) == want);
        n += want;
    }
    CHECK(n == 16);
    // the kinds a guest takes outside Tribe: the party music's stop (BUG-0890), the game-over and pause waits
    for (int k = 0; k < 256; k++)
        CHECK(any_mode((uint8_t)k) == (k == K_PARTY_STOP || k == K_GAMEOVER || k == K_HOST_PAUSE));
}

// BUG-1271: a member's species against its owner's own line
static void own_species()
{
    const uint32_t slot = 0x067B3E4A, line = 0x7C123456, other = 0x290229B9;
    CHECK(species_act(line, slot, slot, 0, true) == SpeciesAct::Apply);   // a guest's member of the slot's species
    CHECK(species_act(line, slot, slot, 0, false) == SpeciesAct::Wait);   // its creation not buildable here yet
    CHECK(species_act(line, line, slot, line, true) == SpeciesAct::Keep); // already
    CHECK(species_act(line, other, slot, 0, true) == SpeciesAct::Apply);  // any other species: the owner's
    CHECK(species_act(slot, slot, slot, 0, true) == SpeciesAct::Keep);    // a line equal to the slot is none
    CHECK(species_act(0, line, slot, line, true) == SpeciesAct::Revert);  // given to the host / its owner left / off
    CHECK(species_act(0, other, slot, 0, true) == SpeciesAct::Keep);      // never touched by the mod: as it is
    CHECK(species_act(0, other, slot, line, true) == SpeciesAct::Keep);   // changed since by someone else
    CHECK(species_act(0, slot, slot, line, true) == SpeciesAct::Keep);    // back already
}


// BUG-1335: a hand bone against the body's bone array (CreateEffect's read). The lab's case (20261119-214823
// late_species): the hand bone 64 from the species' limb table, the body's array not built that far yet. The old rule
// (`--nofix`: the game's only check, bone != -1) lets it through: FAILS
static bool g_nofix = false;
static void hand_bone()
{
    using bone_rules::Bone;
    auto rule = [](bool have, uint32_t b, uint32_t e, int bone) {
        return g_nofix ? (bone == -1 || have ? Bone::Ok : Bone::Unknown) : bone_rules::check(have, b, e, bone);
    };
    const uint32_t b = 0x1218E000;
    CHECK(rule(true, b, b + 24 * 700, 20) == Bone::Ok);   // the fixture creature: 24 bones, a hand at 20
    CHECK(rule(true, b, b + 24 * 700, 23) == Bone::Ok);   // the last bone
    CHECK(rule(true, b, b + 24 * 700, 24) == Bone::Out);  // one past the end
    CHECK(rule(true, b, b + 40 * 700, 64) == Bone::Out);  // the lab's: hand 64, the body not built that far
    CHECK(rule(true, b, b + 80 * 700, 64) == Bone::Ok);   // the same body once built
    CHECK(rule(true, 0, 0, 64) == Bone::Out);             // no bones at all yet
    CHECK(rule(true, b, b, 0) == Bone::Out);              // an empty array
    CHECK(rule(true, b, b + 24 * 700, -1) == Bone::Ok);   // -1: the game's own "no bone" (CreateEffect returns null)
    CHECK(rule(false, 0, 0, 5) == Bone::Unknown);         // no model to judge by
    CHECK(rule(true, b, b + 24 * 700 + 3, 5) == Bone::Unknown); // not an array of bones: not judged
    CHECK(rule(true, b, b + 24 * 700, -7) == Bone::Out);  // a negative index other than -1
}

// BUG-1280 (found by the Lineages fork, its BUG-9060): every vehicle editor the game opens at the end of Tribe (the
// archetype rule of 0xCD2308) is an end-of-Tribe editor for the BUG-0849 guard. FAILS with `--nofix` (the old list)
static void end_editors()
{
    using namespace tribe_editors;
    CHECK(vehicle_editor(kArchetypeA) == 0xF670AA43u);
    CHECK(vehicle_editor(kArchetypeB) == 0x9AD7D4AAu);
    CHECK(vehicle_editor(kArchetypeAggressive) == 0x7D433FADu && vehicle_editor(0) == 0x7D433FADu);
    for (uint32_t a : {kArchetypeA, kArchetypeB, kArchetypeAggressive, 0u, 0xFFFFFFFFu})
        CHECK(end_editor(vehicle_editor(a), !g_nofix));
    CHECK(end_editor(kCityHall, !g_nofix));
    CHECK(!end_editor(0x7CB21BADu) && !end_editor(0x465C50BAu)); // no such editor / another editor
}

// BUG-1235 (tribe.smooth_bound): the frame a walked member mirror draws = the placing's step (walk_place: at most
// (sp + catch_up) x dt, with lead_speed at most (2 x lead_speed + catch_up - sp) x dt) + the game's walk of the next
// frame at sp. The lab's case (tribe_walk_player 20261121-003202 member 3, 20261119-214823 member 7): a walk start in
// the smoothed path while the late lead catches up at 1.49-1.51x: sp = raw lead = 13.4-13.7 m/s, catch_up 3 m/s
// (catch_prop off there), 32 ms frames; the test's jump limit 3 x 9 m/s x dt. FAILS with `--nofix` (the 0.7.16 rc rule)
static float drawn_frame(float lead_speed, float sp, float catch_up, float dt)
{
    float max_step = (sp + catch_up) * dt;
    if (lead_speed > 0)
        max_step = std::min(max_step, std::max(0.f, 2.f * lead_speed + catch_up - sp) * dt);
    return max_step + sp * dt;
}

static void smooth_bound()
{
    printf("member mirror frame bound: smoothed walk start while the late lead catches up (BUG-1235)\n");
    const bool fix = !g_nofix;
    const float dt = 0.032f, limit = 3.f * 9.f * dt; // 0.864 m
    for (float raw : {13.4f, 13.7f}) {
        const float rate = raw / 9.f;
        float ls = mirror_lead_speed(true, fix, raw, raw, true, rate);
        CHECK(drawn_frame(ls, raw, 3.f, dt) < limit); // 0.67 m with the bound, 0.95 m without (the lab's frames)
        CHECK(ls > 8.9f && ls < 9.1f);                // the body's own speed
    }
    // without a catch-up the bound is BUG-0619's (the raw lead's speed); a standing body still gets the 0.5 m/s floor
    CHECK(!fix || mirror_lead_speed(true, true, 9.f, 9.f, false, 1.f) == 9.f);
    CHECK(!fix || mirror_lead_speed(true, true, 0.f, 0.f, false, 1.f) == 0.5f);
    // the raw lead (lead_sp < 0): drive.lead_keep's bound, only while it catches up, unchanged by the switch
    CHECK(mirror_lead_speed(true, false, -1.f, 13.5f, true, 1.5f) == 9.f);
    CHECK(mirror_lead_speed(true, true, -1.f, 9.f, false, 1.f) == 0.f);
    CHECK(mirror_lead_speed(false, true, -1.f, 13.5f, true, 1.5f) == 0.f);
    // the switch off: no bound in the smoothed path (the 0.7.16 rc rule; the lab's 0.95 m frame)
    CHECK(mirror_lead_speed(true, false, 13.5f, 13.5f, true, 1.5f) == 0.f);
    CHECK(drawn_frame(0.f, 13.6f, 3.f, dt) > limit);
    // BUG-1451 (tribe.smooth_catch): BUG-1235's case 1-2 m behind keeps the body's own speed; any lag stays under the
    // jump limit (at most kLagCatchMax: 0.67 m frames)
    for (float raw : {13.4f, 13.7f})
        for (float lag : {1.f, 2.f})
            CHECK(mirror_lead_speed(true, true, raw, raw, true, raw / 9.f, true, lag) > 8.9f &&
                  mirror_lead_speed(true, true, raw, raw, true, raw / 9.f, true, lag) < 9.1f);
    CHECK(drawn_frame(mirror_lead_speed(true, true, 13.6f, 13.6f, true, 13.6f / 9.f, true, 20.f), 13.6f, 3.f, dt) < limit);
    // body_trail_tribe_player 20261122-194210 t1: the host slowed to ~2 m/s while its late copy stood 3.3 m behind; the
    // aim (the smoothed target closing that offset) at 6 m/s, 33 ms frames: held to (2 x 2 + 3 - 6) x dt = 0.03 m a
    // frame by the body's speed alone; with the catch-up the placing closes (sp + catch_up) x dt again (as before 0.7.16)
    {
        const float sp = 6.f, ddt = 0.033f;
        float off = mirror_lead_speed(true, true, 2.f, sp, false, 1.f, false, 3.3f);
        float on = mirror_lead_speed(true, true, 2.f, sp, false, 1.f, !g_nofix, 3.3f);
        CHECK(off == 2.f);
        CHECK(drawn_frame(on, sp, 3.f, ddt) - sp * ddt >= (sp + 3.f) * ddt - 1e-4f); // the placing's full step
        CHECK(!g_nofix || drawn_frame(off, sp, 3.f, ddt) - sp * ddt < 0.05f);
    }
}

// BUG-1460: the make-way hook leaves out only a citizen without a tribe; a dead mirror stands still only when it was a
// host body taken out of its tribe. With `--nofix` (the switches off) neither acts: the first checks fail
static void body_rules()
{
    printf("tribe-less walkers: make-way skip and still bodies (BUG-1460)\n");
    const bool fix = !g_nofix;
    int t = 0;
    CHECK(makeway_skip(fix, true, nullptr)); // the fault's case (FAILS with --nofix)
    CHECK(!makeway_skip(fix, true, &t));             // a citizen in its tribe: the game's own step
    CHECK(!makeway_skip(fix, false, nullptr));       // an animal (its herd path, no tribe read)
    CHECK(!makeway_skip(false, true, nullptr));      // the switch off: the game's function always
    CHECK(body_still(fix, true, true)); // (FAILS with --nofix)
    CHECK(!body_still(fix, false, true) && !body_still(fix, true, false));
}

// BUG-1235 / item 12 of docs/plan-0.7.17.md (drive.lead_cap): the frame a walked member mirror draws = walk_place's
// placing step (drive::walk_place_max_step) + the game's walk of the next frame at walk_sp. The lab's raw-path frames
// (tribe_walk_player 20261123-023643 guest's view of member 8: 9 m/s, 2.74 m behind, the game walking 9 m/s, 32 ms;
// 20261123-071312 member 1: a 12 m/s run, 1.40 m behind, the game at 7.74 m/s, 31 ms); the test's jump limit
// 3 x 9 m/s x dt (the members' p95 speed 9). FAILS with --nofix.
static float raw_frame(float sp, float err, float gv, float dt, bool fix)
{
    const float ls = mirror_lead_speed(true, true, -1.f, sp, false, 1.f, true, err, fix);
    return drive::walk_place_max_step(sp, sp, gv, err, dt, 3.f, true, true, ls, false, fix) + sp * dt;
}
static void lead_cap()
{
    printf("member mirror frame bound: the raw lead's catch-up (BUG-1235, drive.lead_cap)\n");
    const bool fix = !g_nofix;
    CHECK(raw_frame(9.f, 2.74f, 9.15f, 0.032f, fix) < 3.f * 9.f * 0.032f);   // 0.64 m (0.93 m before)
    CHECK(raw_frame(12.f, 1.40f, 7.74f, 0.031f, fix) < 3.f * 9.f * 0.031f);  // 0.68 m (0.92 m before)
    CHECK(raw_frame(9.f, 2.74f, 9.15f, 0.032f, false) > 3.f * 9.f * 0.032f); // the 0.7.16.1 rule: the lab's jump
    // the hard bound: never more than 2 x the body's speed + 3 m/s in a frame, whatever the error and the game's walk
    for (float err : {0.5f, 1.5f, 3.f, 9.f})
        for (float gv : {0.f, 4.f, 9.f})
            CHECK(!fix || raw_frame(9.f, err, gv, 0.032f, true) <= (2.f * 9.f + 3.f) * 0.032f + 1e-4f);
    // a big error (BUG-0674, 6-9 m after a switch) still closes fast: the frame at the body's speed + 12 m/s
    CHECK(!fix || std::fabs(raw_frame(9.f, 9.f, 9.f, 0.032f, true) - 21.f * 0.032f) < 1e-3f);
    // the smoothed path (catch_prop off) with BUG-1451's lag bound: unchanged over kSlowCapErr (body_trail_tribe_player
    // 20261122-194210 t1: 3.3 m behind, the host at 2 m/s, the aim at 6 m/s): the placing's full (sp + 3) step
    {
        const float sp = 6.f, dt = 0.033f;
        const float ls = mirror_lead_speed(true, true, 2.f, sp, false, 1.f, true, 3.3f, fix);
        CHECK(std::fabs(drive::walk_place_max_step(sp, sp, sp, 3.3f, dt, 3.f, false, true, ls, false, fix) -
                        (sp + 3.f) * dt) < 1e-4f);
    }
    // slow phases with a small error (the stop-and-go half: the guest's member 9 at 1.6 m/s, 0.46 m behind, drawn
    // 4.8-5.0 m/s in 20261123-023643): whether the game's next walk comes (walk_sp) or not (0), the frame stays within the
    // test's uneven line (the target's step +- half of it + 1 m/s)
    for (float sp : {1.6f, 3.2f, 4.5f})
        for (float next : {0.f, sp}) {
            const float dt = 0.033f, ts = sp * dt;
            const float ls = mirror_lead_speed(true, true, sp, sp, false, 1.f, true, 0.46f, fix);
            const float drawn = drive::walk_place_max_step(sp, sp, sp * 0.8f, 0.46f, dt, 3.f, false, true, ls, false, fix) +
                                next * dt;
            CHECK(!fix || std::fabs(drawn - ts) <= 0.5f * ts + 1.f * dt);
        }
    // the lab's frame itself, the game walking 1.6 m/s next frame: 1.5 x 1.6 + 0.9 = 3.3 m/s (4.6 m/s placing + walk before)
    CHECK(drive::walk_place_max_step(1.6f, 1.6f, 1.28f, 0.46f, 0.033f, 3.f, false, true, 1.6f, false, fix) + 1.6f * 0.033f <=
          3.3f * 0.033f + 1e-4f);
    // no bound asked (lead_speed 0: the NPC proxies, Creature and Civ): the old (sp + catch_up) step, switch on or off
    CHECK(std::fabs(drive::walk_place_max_step(4.f, 4.f, 4.f, 2.f, 0.1, 3.f, true, true, 0.f, false, true) -
                    (4.f + 7.f) * 0.1f) < 1e-4f); // catch_up min(2 / 0.25, 4 + 3)
    // the switch off: the raw lead without a late catch-up has no bound (0.7.16.1)
    CHECK(mirror_lead_speed(true, true, -1.f, 9.f, false, 1.f, true, 1.f, false) == 0.f);
    CHECK(!fix || mirror_lead_speed(true, true, -1.f, 9.f, false, 1.f, true, 1.f, true) == 9.f);
}

// 0.7.17 WP-B1, the audit's TR items and the save's taken members: each rule with its switch on (the lab's case) and off
// (0.7.16.1); with `--nofix` the first check of each FAILS
static void audit_tr()
{
    printf("audit TR-7, 8, 9, 11 and the save's taken members (BUG-1493..1496, BUG-1522)\n");
    const bool fix = !g_nofix;
    // TR-8: a picked-empty tree (+0x48 = 0) of a big tree (+0x4C = 0x1F) is no gather target
    CHECK(!tree_has_fruit(fix, 0, 0x1F));        // (FAILS with --nofix)
    CHECK(tree_has_fruit(fix, 0x04, 0x1F));      // fruit left
    CHECK(tree_has_fruit(false, 0, 0x07));       // the 0.7.16.1 read: every tree passes
    CHECK(tree_has_fruit(true, 0x01, 0x07) && !tree_has_fruit(true, 0, 0));
    // TR-7: the hold goes when the mirror is not placed from the host any more
    CHECK(air_release(fix, true, true, false));  // the host away (FAILS with --nofix)
    CHECK(air_release(fix, true, false, true) == fix); // no snapshot for 3 s
    CHECK(!air_release(fix, true, false, false)); // placed from the host: the hold is the air list's
    CHECK(!air_release(fix, false, true, true));  // no hold
    CHECK(!air_release(false, true, true, true));
    // TR-9: a guest in another world drops the host's music plays and its caption's going up, takes the stops and ends
    CHECK(elsewhere_drops(fix, true, 20, true)); // the game-over caption up (FAILS with --nofix)
    for (uint8_t k : {4, 5, 7})
        CHECK(elsewhere_drops(fix, true, k, true) == fix);
    CHECK(elsewhere_drops(fix, true, 21, true) == fix);
    for (uint8_t k : {6, 8})
        CHECK(!elsewhere_drops(fix, true, k, false)); // the stops pass
    CHECK(!elsewhere_drops(fix, true, 20, false) && !elsewhere_drops(fix, true, 21, false)); // a wait's end passes
    for (uint8_t k : {2, 3, 4, 11, 16, 20})
        CHECK(!elsewhere_drops(fix, false, k, true)); // in the host's world: every kind
    CHECK(!elsewhere_drops(false, true, 20, true));
    // TR-11: a theme the relay plays is stopped at the session's end in Tribe; a stop outside Tribe waits for the return
    CHECK(theme_end(fix, true, true) == ThemeEnd::Stop); // (FAILS with --nofix)
    CHECK(theme_end(fix, false, false) == (fix ? ThemeEnd::Later : ThemeEnd::Lost));
    CHECK(theme_end(fix, true, false) == ThemeEnd::Stop);
    CHECK(theme_end(false, true, true) == ThemeEnd::Lost && theme_end(false, false, false) == ThemeEnd::Lost);
    // BUG-1522: a taken member goes back only alive, the same object, its tribe listed, not in the vector yet
    CHECK(!took_goes_back(fix, true, false, false, false)); // removed during the save (FAILS with --nofix)
    CHECK(took_goes_back(fix, true, true, true, false));
    CHECK(took_goes_back(fix, true, true, false, false) == !fix); // another citizen at that address
    CHECK(took_goes_back(fix, false, true, true, false) == !fix); // its tribe gone
    CHECK(took_goes_back(fix, true, true, true, true) == !fix);   // already back (put back by the removal itself)
    CHECK(took_goes_back(false, false, false, false, true));
}

int main(int argc, char** argv)
{
    g_nofix = argc > 1 && !strcmp(argv[1], "--nofix");
    own_species();
    hand_bone();
    owners();
    balance_share();
    balance_leader();
    bodies();
    apply();
    anim_type();
    end_editors();
    smooth_bound();
    lead_cap();
    body_rules();
    audit_tr();
    if (g_fail) {
        printf("tribe_tests: %d FAILED\n", g_fail);
        return 1;
    }
    printf("tribe_tests: all passed\n");
    return 0;
}
