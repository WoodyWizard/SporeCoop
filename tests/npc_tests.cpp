// Native tests for the NPC id allocator (npc_ids.h, BUG-0023 / BUG-0044) and the joint-hunt hit
// table (hunt_hits.h, BUG-0099) (make test; built with AddressSanitizer).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>

#include "drive_plan.h"
#include "hunt_hits.h"
#include "npc_ids.h"
#include "savestrip_rule.h"
#include "stage_epoch.h"
#include "share_wait.h"
#include "busy.h"
#include "swap_ledger.h"
#include "world_name.h"
#include "fields_wire.h"
#include "realign_rule.h"
#include "lifecycle.h"
#include "call_guard.h"
#include "off_main.h"
#include "npc_proxy_rules.h"
#include "max_hp_rule.h"

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

// host_tick's pattern: NPCs come and go, a few live for good. Returns how many times an id was
// given while a living NPC had it.
static int churn(bool skip_used)
{
    std::unordered_map<uint16_t, int> live; // id -> npc
    uint16_t next = 1;
    int repeats = 0;
    for (int i = 0; i < 5; i++) // five NPCs that stay in the world (the nest next to the avatar)
        live[npc::alloc_npc_id(next, [&](uint16_t id) { return live.count(id) != 0; }, skip_used)] = i;
    for (int n = 0; n < 200000; n++) { // NPCs that load and unload at once while the players roam
        uint16_t id = npc::alloc_npc_id(next, [&](uint16_t id) { return live.count(id) != 0; }, skip_used);
        CHECK(id >= npc::kFirstNpcId && id <= npc::kLastNpcId);
        repeats += live.count(id) != 0;
    }
    return repeats;
}

static void test_ids()
{
    printf("npc ids\n");
    CHECK(churn(false) > 0); // the code before BUG-0023: an id in use is given again after the wrap
    CHECK(churn(true) == 0);
    { // never 0 or 0xFFFF, wraps to 1
        uint16_t next = 0xFFFE;
        CHECK(npc::alloc_npc_id(next, [](uint16_t) { return false; }) == 0xFFFE);
        CHECK(npc::alloc_npc_id(next, [](uint16_t) { return false; }) == 1);
        next = 0;
        CHECK(npc::alloc_npc_id(next, [](uint16_t) { return false; }) == 1);
    }
    { // every id in use: none
        uint16_t next = 7;
        CHECK(npc::alloc_npc_id(next, [](uint16_t) { return true; }) == 0);
    }
    { // skips a run of used ids
        std::set<uint16_t> used{5, 6, 7};
        uint16_t next = 5;
        CHECK(npc::alloc_npc_id(next, [&](uint16_t id) { return used.count(id) != 0; }) == 8);
        CHECK(next == 9);
    }
    // BUG-0044: an object that is another creature now
    CHECK(!npc::npc_object_reused(0x65CB8AB, false, 0x65CB8AB, false));
    CHECK(!npc::npc_object_reused(0x65CB8AB, false, 0x65CB8AB, true)); // it died
    CHECK(npc::npc_object_reused(0x65CB8AB, true, 0x65CB8AB, false));  // alive after it was dead
    CHECK(npc::npc_object_reused(0x65CB8AB, false, 0x65CBF12, false)); // another species
}

static void test_hits()
{
    printf("joint hunt hits\n");
    for (int fix = 0; fix < 2; fix++) {
        teamplay::HitTable t(6.0);
        t.fix = fix;
        // 1000 creatures hit once each, one per second: only those of the last 6 s matter
        static char targets[1000];
        for (int i = 0; i < 1000; i++)
            t.hit(&targets[i], 1, i);
        if (!fix)
            CHECK(t.size() == 1000); // the code before BUG-0099: one entry per creature ever hit
        else
            CHECK(t.size() <= 7);
    }
    teamplay::HitTable t(6.0);
    char a, b;
    CHECK(!t.hit(&a, 0, 0));
    CHECK(t.hit(&a, 1, 2));  // another player within the window: joint
    CHECK(!t.hit(&b, 1, 2));
    CHECK(!t.hit(&a, 0, 20)); // the window is over
    t.forget(&a);              // the object was freed: a new creature at its address starts clean
    CHECK(!t.hit(&a, 1, 21));
    t.clear();
    CHECK(t.size() == 0);
}

// BUG-0110: changes around a call into the game that a fault leaves open are undone once per frame
static void set_u32(void* p, uintptr_t v) { *(uint32_t*)p = (uint32_t)v; }
static void test_ledger()
{
    printf("swap ledger\n");
    for (bool guard : {false, true}) {
        swapledger::Ledger<4> l;
        l.guard = guard;
        uint32_t av = 7, flag = 1;
        // a call that returns: nothing left
        size_t m = l.open(set_u32, &av, av, "av");
        av = 9;
        av = 7;
        l.close(m);
        CHECK(l.n == 0 && l.tick() == 0);
        // a call that faults (the close never runs): the frame's tick puts the value back
        m = l.open(set_u32, &av, av, "av");
        av = 9;
        l.fault = true;
        l.close(m);
        l.tick();
        if (guard)
            CHECK(av == 7 && l.restored == 1);
        else
            CHECK(av == 9); // the code before the fix: the partner stays the avatar
        av = 7;
        l.n = 0;
        // an inner call faulted inside an outer one that returned: the outer close undoes it
        size_t outer = l.open(set_u32, &av, av, "av");
        av = 9;
        l.open(set_u32, &flag, flag, "flag"); // inner
        flag = 0;
        av = 7;
        l.close(outer);
        CHECK(flag == 1 && l.n == 0);
        // several own entries of one caller are its own (not leftovers)
        uint32_t x = 1, y = 2;
        m = l.n;
        l.open(set_u32, &x, x, "x");
        l.open(set_u32, &y, y, "y");
        x = y = 5;
        l.close(m, l.n - m);
        CHECK(x == 5 && y == 5 && l.n == 0);
        // full: no entry, nothing breaks
        for (int i = 0; i < 6; i++)
            l.open(set_u32, &x, 1, "x");
        CHECK(l.n == 4);
        l.close(0, 4);
        CHECK(l.n == 0);
    }
}

// BUG-0169: the host's NPC pushes against a rock: it reports 2 m/s and stands 4 m from where the
// guest's proxy can get (the proxy walks into the same rock). Returns the teleports in 40 s.
static int pushing(bool fix, float reachable_after_teleport)
{
    drive::Track t;
    float err = 4.f;
    int tps = 0;
    for (int f = 0; f < 40 * 30; f++) { // 30 frames a second
        double now = 1 + f / 30.0;
        drive::Plan p = drive::plan(t, err, 2.f, now, 8.f, fix);
        if (p.snap || p.stuck || p.stall) {
            tps++;
            err = reachable_after_teleport; // where the collision leaves it
        }
    }
    return tps;
}

static void test_drive()
{
    printf("proxy driving\n");
    CHECK(pushing(false, 0.f) == 0); // the code before BUG-0169: 4 m off for as long as it pushes
    CHECK(pushing(true, 0.f) == 1);  // put in place after ~3 s
    int n = pushing(true, 4.f);      // a spot it cannot stand on: pushed out again each time
    CHECK(n >= 2 && n <= 4);         // not every frame: once per 3 s + 10 s quiet
    // a walking puppet that catches up (the error shrinks) is not teleported
    drive::Track t;
    int tps = 0;
    for (int f = 0; f < 30 * 30; f++) {
        float err = 7.f - f * 0.02f;
        if (err < 0.5f)
            err = 0.5f;
        drive::Plan p = drive::plan(t, err, 3.f, 1 + f / 30.0, 8.f, true);
        tps += p.snap || p.stuck || p.stall;
    }
    CHECK(tps == 0);
    // a remote standing still: the stuck teleport after 1.5 s as before
    drive::Track s;
    bool stuck = false;
    for (int f = 0; f < 60 && !stuck; f++)
        stuck = drive::plan(s, 2.f, 0.f, 1 + f / 30.0, 8.f, true).stuck;
    CHECK(stuck);
    // BUG-0305: a proxy pushed 3 m off the path of a running animal by the proxy beside it (runs at
    // the animal's speed, never closes in): put in place after 0.5 s with the fix, never without
    auto trailing = [](bool fix) {
        drive::Track t;
        int tps = 0;
        float err = 3.f;
        for (int f = 0; f < 3 * 30; f++) {
            drive::Plan p = drive::plan(t, err, 6.5f, 1 + f / 30.0, 8.f, true, fix);
            if (p.snap || p.stuck || p.stall || p.trail) {
                tps++;
                err = 3.f; // pushed off again at once
            }
        }
        return tps;
    };
    CHECK(trailing(false) == 0);
    int tr = trailing(true);
    CHECK(tr >= 2 && tr <= 3); // 0.5 s, then 1 s quiet each time
    // a proxy that closes in is left walking
    drive::Track u;
    int closing = 0;
    for (int f = 0; f < 60; f++)
        closing += drive::plan(u, 3.f - f * 0.05f, 6.5f, 1 + f / 30.0, 8.f, true, true).trail;
    CHECK(closing == 0);
}

// BUG-0329: a mirror whose own ground is 5 m higher than the host's is driven at its own height
static void test_own_height()
{
    printf("mirror target at its own ground height (BUG-0329)\n");
    struct V { float x, y, z; };
    V host{0, -511.f, 0}, cur{3.f, -516.f, 0};
    V t = drive::at_own_height(host, cur, true);
    float rt = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z), rc = std::sqrt(cur.x * cur.x + cur.y * cur.y + cur.z * cur.z);
    CHECK(std::fabs(rt - rc) < 0.01f);           // the mirror's own distance from the centre
    CHECK(std::fabs(t.x) < 0.01f && t.y < 0);    // the host's direction
    V same = drive::at_own_height(host, cur, false);
    CHECK(same.y == host.y);                      // off: the host's absolute position
    V far{0, -540.f, 0};                          // more than 10 m apart (a flight, a jump): unchanged
    CHECK(drive::at_own_height(far, cur, true).y == far.y);
}

// BUG-0217 / BUG-0223: the stage counters and a module's watch (stage_epoch.h)
static void test_stage_epoch()
{
    using namespace stage_epoch;
    const uint32_t kLoad = 0x1654C08, kMenu = 0x2CCD1D2;
    State s;
    step(s, kMenu);
    CHECK(get(s, kTribe) == 0);
    step(s, kLoad);
    step(s, kTribe);
    CHECK(get(s, kTribe) == 1);
    step(s, kTribe);
    CHECK(get(s, kTribe) == 1);
    step(s, kEditor); // editor, then Done: the same world
    step(s, kEdit);
    step(s, kTribe);
    CHECK(get(s, kTribe) == 1);
    step(s, kEditor); // editor, then the menu, then a load: a new world
    step(s, kMenu);
    step(s, kLoad);
    step(s, kTribe);
    CHECK(get(s, kTribe) == 2);
    step(s, kCiv); // another stage
    CHECK(get(s, kCiv) == 1 && get(s, kTribe) == 2);
    step(s, kScenario); // kScenario keeps only Space
    step(s, kCiv);
    CHECK(get(s, kCiv) == 2);
    step(s, kSpace);
    step(s, kScenario);
    step(s, kSpace);
    CHECK(get(s, kSpace) == 1);
    step(s, kTribe);
    CHECK(get(s, kTribe) == 3);
    CHECK(get(s, kMenu) == 0);

    int a = 0, b = 0;
    Watch w;
    CHECK(!changed(w, 5, true, &a, 7)); // the first call records only (hot reload)
    CHECK(!changed(w, 5, true, &a, 7));
    CHECK(!changed(w, 5, false, nullptr, 0)); // editor: the in-stage signals wait
    CHECK(!changed(w, 5, true, &a, 7));
    CHECK(changed(w, 6, true, &a, 7)); // the stage counter moved
    CHECK(!changed(w, 6, true, &a, 7));
    CHECK(changed(w, 6, true, &b, 7)); // another player object in the stage
    CHECK(!changed(w, 6, true, &b, 7));
    CHECK(changed(w, 6, true, nullptr, 7)); // the object went away (null included): once
    CHECK(!changed(w, 6, true, nullptr, 7));
    CHECK(!changed(w, 6, true, &a, 7)); // back: no recorded object to compare with
    CHECK(changed(w, 6, true, &a, 9)); // another save name
    CHECK(!changed(w, 6, true, &a, 0)); // no save name: no signal
    CHECK(!changed(w, 6, true, &a, 9));
    Watch w2; // recorded outside the stage: no object yet
    CHECK(!changed(w2, 1, false, nullptr, 0));
    CHECK(!changed(w2, 1, true, &a, 3));
    CHECK(!changed(w2, 1, true, &a, 3));
}

// BUG-0255: one list of our objects per save; empty herds only when surely empty (savestrip_rule.h)
static void test_savestrip_rule()
{
    using namespace savestrip;
    int s1, s2;
    SaveState st;
    CHECK(new_save(st, &s1, 10));  // the first Write
    CHECK(!new_save(st, &s1, 10)); // same serializer, same frame: the same save
    CHECK(new_save(st, &s1, 11));  // the same address one frame later: a new save
    CHECK(new_save(st, &s2, 11));  // another serializer: a new save
    CHECK(!new_save(st, &s2, 11));
    CHECK(strip_empty_herd(true, true, false, false));
    CHECK(!strip_empty_herd(false, true, false, false)); // member list not readable
    CHECK(!strip_empty_herd(true, false, false, false)); // has members
    CHECK(!strip_empty_herd(true, true, true, false));   // a creature points at it
    CHECK(!strip_empty_herd(true, true, false, true));   // a creature of the save was unreadable
}

// BUG-0232: the host's wait for the evolution cinematic before a world share. `each` = the fix (one
// timer per guest, restarted when the guest is marked as needing the world); without it one
// global timer (npc_tests --nofix: the checks must fail).
static bool g_nofix = false;
struct Waiter {
    bool needs = false;
    double since = 0;
    double sent_at = -1;
};

// follow_phase's loop over the waiting guests at `now`
static void share_frame(std::vector<Waiter>& w, double& global, bool cine, double now, double due_at, bool each)
{
    for (auto& g : w) {
        if (!g.needs)
            continue;
        double& since = each ? g.since : global;
        if (share_wait::hold(since, cine, now, due_at))
            continue;
        g.since = 0;
        g.needs = false;
        g.sent_at = now;
    }
}

static void test_share_wait()
{
    printf("share wait for the evolution cinematic: per guest (BUG-0232)\n");
    const bool each = !g_nofix;
    // three guests wait while a cinematic plays for 400 s: all get the world after 120 s
    std::vector<Waiter> w(3);
    double global = 0;
    for (auto& g : w)
        g.needs = true;
    for (double t = 1000; t < 1400; t += 1)
        share_frame(w, global, true, t, 1003, each);
    for (auto& g : w)
        CHECK(g.sent_at >= 1120 && g.sent_at <= 1125); // one global timer: 1123 / 1243 / 1363
    // a guest waits and leaves; another one waits 200 s later during a cinematic: held 120 s
    std::vector<Waiter> a(1);
    global = 0;
    a[0].needs = true;
    for (double t = 2000; t < 2050; t += 1)
        share_frame(a, global, true, t, 0, each);
    a.clear(); // the guest left (its Remote is erased)
    std::vector<Waiter> b(1);
    b[0].needs = true; // a new guest, a new Remote
    for (double t = 2250; t < 2500; t += 1)
        share_frame(b, global, true, t, 0, each);
    CHECK(b[0].sent_at >= 2369); // one global timer: sent at once (2250), during the cinematic
    // marked again as needing the world (a new evolution): the timer starts from 0
    Waiter c;
    c.needs = true;
    c.since = 3000; // left over from an earlier wait
    if (each)
        c.since = 0; // on_mode_change / share_all / a join in a menu reset it with needs_world
    std::vector<Waiter> cv{c};
    for (double t = 3200; t < 3400; t += 1)
        share_frame(cv, global, true, t, 0, each);
    CHECK(cv[0].sent_at >= 3319);
    // no cinematic: sent as soon as the 3 s after the editor are over
    std::vector<Waiter> d(2);
    for (auto& g : d)
        g.needs = true;
    for (double t = 4000; t < 4010; t += 1)
        share_frame(d, global, false, t, 4003, each);
    CHECK(d[0].sent_at == 4003 && d[1].sent_at == 4003);
}

// BUG-0258: the game's save name reads "snapshot.spo" after the live Creature -> Tribe change; the
// planet played before it stays the player's world (world_name.h). --nofix: the raw name (fails).
static void test_world_name()
{
    using world_name::effective;
    bool fix = !g_nofix;
    world_name::State s;
    CHECK(effective(s, "SaveGame", fix) == "SaveGame");       // galaxy menu
    CHECK(effective(s, "snapshot.spo", fix) == "snapshot.spo"); // nothing known: as is
    CHECK(effective(s, "Scalinus.spo", fix) == "Scalinus.spo"); // the game loaded
    CHECK(effective(s, "snapshot.spo", fix) == "Scalinus.spo"); // the Tribe intro: still Scalinus
    CHECK(effective(s, "snapshot.spo", fix) == "Scalinus.spo"); // ... for minutes
    CHECK(effective(s, "Scalinus.spo", fix) == "Scalinus.spo"); // the next save: the same place
    CHECK(effective(s, "SaveGame", fix) == "SaveGame");         // back in the menu: forgotten
    CHECK(effective(s, "snapshot.spo", fix) == "snapshot.spo"); // a new game never inherits it
    CHECK(effective(s, "Земля.spo", fix) == "Земля.spo");
    CHECK(effective(s, "Cadian.spo", fix) == "Cadian.spo");     // another planet loaded
    CHECK(effective(s, "snapshot.spo", fix) == "Cadian.spo");
    CHECK(!world_name::is_planet("snapshot.spo") && !world_name::is_planet("a\\b.spo") && !world_name::is_planet(".spo"));
}

// BUG-0175: a mirror that cannot move by itself (the 2024 guest's paused game time after a load)
// follows only by drive::step's per-frame pull: how far behind a target
// moving at v it settles at a frame rate. Returns the error after 10 s.
static float pulled_lag(float k60, float v, double fps, bool fix)
{
    const double dt = 1.0 / fps;
    float target = 0, cur = 0;
    for (int f = 0; f < (int)(10 * fps); f++) {
        target += v * (float)dt;
        cur += (target - cur) * drive::pull_k(k60, dt, fix);
    }
    return target - cur;
}

static void test_pull_per_second()
{
    printf("mirror pull per second, not per frame (BUG-0175)\n");
    const bool fix = !g_nofix;
    // a citizen walking at 5.5 m/s (drive::step's nudge, 15 % at 60 fps)
    float c60 = pulled_lag(0.15f, 5.5f, 60, fix), c15 = pulled_lag(0.15f, 5.5f, 15, fix);
    float c30 = pulled_lag(0.15f, 5.5f, 30, fix);
    printf("  citizen: %.2f m at 60 fps, %.2f m at 30 fps, %.2f m at 15 fps\n", c60, c30, c15);
    CHECK(std::fabs(c60 - 0.52f) < 0.05f); // 60 fps: the pull of before the fix
    CHECK(c15 <= c60 * 1.05f);             // a lower frame rate trails no further
    CHECK(c30 <= c60 * 1.05f);
    // the pull stays a pull: never past the target, and a long hitch is not a full teleport
    CHECK(drive::pull_k(0.15f, 1.0 / 60) > 0.149f && drive::pull_k(0.15f, 1.0 / 60) < 0.151f);
    CHECK(drive::pull_k(0.15f, 5.0) < 0.7f);
    CHECK(drive::pull_k(0.15f, 0.0) == 0.15f);
}

// BUG-0404: the busy gate (busy.h) - the reasons, their names and the wait of one action
static void test_busy_gate()
{
    printf("busy gate: reasons, names, waits (BUG-0404)\n");
    CHECK(busy::bits(false, 0, false, false, false) == 0);
    CHECK(busy::bits(true, 1, false, false, false) == (busy::kCinematic | busy::kInputLock));
    CHECK(busy::bits(false, 0, true, true, true) == (busy::kPaused | busy::kEditor | busy::kLoading));
    CHECK(busy::names(0) == "-");
    CHECK(busy::names(busy::kCinematic | busy::kPaused) == "cinematic,paused");
    // the gates: an editor waits for a cinematic and a pause; the still gate (census) for a pause, not a cinematic
    CHECK((busy::kEditorGate & busy::kCinematic) && (busy::kEditorGate & busy::kPaused));
    CHECK((busy::kStillGate & busy::kPaused) && !(busy::kStillGate & busy::kCinematic));
    // a cinematic of 7 s: held 7 s, then free, not expired
    busy::Wait w;
    bool exp = true;
    int held = 0;
    for (double t = 100; t < 120; t += 1)
        held += busy::hold(w, t < 107 ? busy::kCinematic : 0u, t, 60.0, busy::kPaused, 300.0, &exp);
    CHECK(held == 7 && !exp && w.since == 0);
    // a cinematic that never ends: held 60 s, then expired (the action's own fallback)
    w = {};
    double went = 0;
    for (double t = 100; t < 200 && !went; t += 1)
        if (!busy::hold(w, busy::kCinematic, t, 60.0, busy::kPaused, 300.0, &exp))
            went = t;
    CHECK(went == 160 && exp);
    // a pause alone waits 300 s; a cinematic that starts after 100 s of pause still gets its full 60 s
    w = {};
    held = 0;
    for (double t = 1; t < 100; t += 1)
        held += busy::hold(w, busy::kPaused, t, 60.0, busy::kPaused, 300.0, &exp);
    CHECK(held == 99 && w.since == 1 && w.other_since == 0);
    CHECK(busy::hold(w, busy::kCinematic | busy::kPaused, 100, 60.0, busy::kPaused, 300.0, &exp) && w.other_since == 100);
    CHECK(busy::hold(w, busy::kCinematic | busy::kPaused, 159, 60.0, busy::kPaused, 300.0, &exp));
    CHECK(!busy::hold(w, busy::kCinematic | busy::kPaused, 160, 60.0, busy::kPaused, 300.0, &exp) && exp);
    // a pause that never ends: 300 s, then expired
    w = {};
    went = 0;
    for (double t = 1000; t < 1400 && !went; t += 1)
        if (!busy::hold(w, busy::kPaused, t, 60.0, busy::kPaused, 300.0, &exp))
            went = t;
    CHECK(went == 1300 && exp);
    // the share gate is BUG-0019's cinematic only (a pause of the game's own held shares 120 s)
    CHECK(busy::kShareGate == busy::kCinematic);
}

// fields_wire.h (BUG-0419 G2): MSG_FIELDS round trip, chunking, malformed packets, the compare rule
static void test_fields_wire()
{
    fields_wire::Batch b;
    b.kind = 2;
    b.fids = {1};
    for (int i = 0; i < 500; i++) { // more than one packet
        b.ids.push_back((uint16_t)(i + 1));
        b.vals.push_back(0.5f + i * 0.01f);
    }
    auto ws = fields_wire::encode(b);
    CHECK(ws.size() >= 2);
    std::vector<uint16_t> ids;
    std::vector<float> vals;
    for (auto& w : ws) {
        CHECK(w.size() <= net::kMaxPacket - 64);
        net::Reader r(w.data.data(), w.data.size());
        fields_wire::Batch d;
        CHECK(fields_wire::decode(r, d) && d.kind == 2 && d.fids == b.fids);
        ids.insert(ids.end(), d.ids.begin(), d.ids.end());
        vals.insert(vals.end(), d.vals.begin(), d.vals.end());
    }
    CHECK(ids == b.ids && vals == b.vals);
    // a cut packet, a wrong count, no fields: refused
    net::Writer& w = ws[0];
    net::Reader cut(w.data.data(), w.data.size() - 1);
    fields_wire::Batch d;
    CHECK(!fields_wire::decode(cut, d));
    std::vector<uint8_t> bad = {2, 0, 0, 0};
    net::Reader nf(bad.data(), bad.size());
    CHECK(!fields_wire::decode(nf, d));
    fields_wire::Batch mism = b;
    mism.vals.pop_back();
    CHECK(fields_wire::encode(mism).empty());
    // compare: 2 % of the wanted value, NaN always different; sane range
    CHECK(!fields_wire::differs(1.01f, 1.0f, 0.02f));
    CHECK(fields_wire::differs(0.93f, 0.80f, 0.02f)); // the first detector finding (BUG-0461)
    CHECK(fields_wire::differs(std::nanf(""), 1.0f, 0.02f));
    CHECK(!fields_wire::sane(1e30f, 0.001f, 1000.f) && !fields_wire::sane(-1.f, 0.001f, 1000.f) &&
          fields_wire::sane(4.9f, 0.001f, 1000.f));
}


// W2 (BUG-0564): the whole-planet match and the extra streaks (realign_rule.h)
static void test_realign_rule()
{
    using realign::Obj;
    std::vector<Obj> host = {{0, 0, 0, 1000, realign::kFlPlayer}, {100, 0, 0, 500, 0}};
    std::vector<Obj> local = {{1, 0, 0, 1000, realign::kFlPlayer}, {130, 0, 0, 900, 0}, {300, 0, 0, 0, 0}, {5, 0, 0, 0, 0}};
    auto m = realign::match(host, local, 40.f);
    CHECK(m.bound.size() == 2);   // the player village with the player's, the NPC one 30 m off
    CHECK(m.lack.empty());
    CHECK(m.extra.size() == 2);   // 300 m away, and the NPC village 5 m from the player's (another flag)
    CHECK(realign::state_off(500, 900) && !realign::state_off(500, 515));
    auto m2 = realign::match(host, local, 20.f);
    CHECK(m2.lack.size() == 1 && m2.lack[0] == 1);
    realign::Streaks s;
    std::vector<uint64_t> due;
    s.step({7, 8}, 2, 4, due);
    CHECK(due.empty());            // one look is not enough
    s.step({7}, 2, 4, due);
    CHECK(due.size() == 1 && due[0] == 7);
    s.step({8}, 2, 4, due);
    CHECK(due.empty());            // 8 had a partner in between: it starts over
    s.step({8}, 0, 4, due);
    CHECK(due.empty() && s.n.empty()); // a host list without the kind while this game has several: no list of the planet
}

// BUG-1055: the lifecycle registry resets the entries of a scope (with their own switch on), and keeps what a reset left
static int g_lc_session = 0, g_lc_world = 0, g_lc_both = 0;
static bool g_lc_fix = true;
SESSION_STATE("t.session", nullptr, [] { g_lc_session = 0; }, []() -> size_t { return (size_t)g_lc_session; });
WORLD_STATE("t.world", &g_lc_fix, [] { g_lc_world = 0; }, []() -> size_t { return (size_t)g_lc_world; });
LIFECYCLE_STATE("t.both", lifecycle::kSession | lifecycle::kWorld, nullptr, [] { g_lc_both = 0; }, []() -> size_t { return (size_t)g_lc_both; });
static void test_lifecycle()
{
    g_lc_session = 2, g_lc_world = 3, g_lc_both = 1;
    CHECK(lifecycle::held() == "t.session 2\nt.world 3\nt.both 1\n");
    lifecycle::reset(lifecycle::kSession, !g_nofix);
    CHECK(g_lc_session == 0 && g_lc_both == 0 && g_lc_world == 3); // a session end keeps the world's state
    CHECK(lifecycle::left(lifecycle::kSession).empty());
    g_lc_fix = false; // the entry's own switch off: kept, and listed as left
    lifecycle::reset(lifecycle::kWorld, true);
    CHECK(g_lc_world == 3 && lifecycle::left(lifecycle::kWorld) == "t.world 3\n");
    g_lc_fix = true;
    g_lc_both = 4;
    lifecycle::reset(lifecycle::kWorld, true);
    CHECK(g_lc_world == 0 && g_lc_both == 0 && lifecycle::left(lifecycle::kWorld).empty());
    g_lc_session = 1;
    lifecycle::reset(lifecycle::kSession, false); // the registry's switch off: nothing reset
    CHECK(g_lc_session == 1 && lifecycle::left(lifecycle::kSession) == "t.session 1\n");
    CHECK(lifecycle::resets(lifecycle::kSession) == 2 && lifecycle::resets(lifecycle::kWorld) == 2);
    lifecycle::WorldWatch w;
    CHECK(!w.changed(5)); // the first call only records
    CHECK(!w.changed(5) && w.changed(6) && !w.changed(6));
}

// D2 (audit TST-4): the warm-reset fingerprint: every entry with a digest (its own, else held()) is listed with it, the
// hash follows the lines; reset_all clears both scopes; an entry whose reset forgets its state (a planted leftover)
// stays in the fingerprint after reset_all, and its own digest is used instead of held()
static uint32_t g_fp_value = 0, g_fp_forgot = 0;
LIFECYCLE_STATE_D("t.digest", lifecycle::kWorld, nullptr, [] { g_fp_value = 0; },
                  []() -> size_t { return g_fp_value ? 1 : 0; }, []() -> uint32_t { return g_fp_value; });
SESSION_STATE("t.forgets", nullptr, [] {}, []() -> size_t { return g_fp_forgot; });
static void test_fingerprint()
{
    lifecycle::reset_all(true);
    lifecycle::Fingerprint clean = lifecycle::fingerprint();
    CHECK(clean.lines.empty() && clean.holding == 0 && clean.entries == lifecycle::table().size());
    g_fp_value = 0xABC;
    g_lc_session = 2;
    lifecycle::Fingerprint f = lifecycle::fingerprint();
    CHECK(f.lines == "t.session 00000002\nt.digest 00000ABC\n" && f.holding == 2 && f.hash != clean.hash);
    g_fp_value = 0xABD; // the same count (held 1), another state: the digest tells them apart
    CHECK(lifecycle::fingerprint().hash != f.hash);
    lifecycle::reset_all(true);
    CHECK(lifecycle::fingerprint().hash == clean.hash && g_fp_value == 0 && g_lc_session == 0);
    g_fp_forgot = 3; // a module whose reset forgets this
    lifecycle::reset_all(true);
    f = lifecycle::fingerprint();
    CHECK(f.lines == "t.forgets 00000003\n" && f.hash != clean.hash);
    g_fp_forgot = 0;
    g_lc_world = 5;
    lifecycle::reset_all(false); // the registry's switch off: nothing reset, all of it in the fingerprint
    CHECK(lifecycle::fingerprint().lines == "t.world 00000005\n");
    lifecycle::reset_all(true);
    CHECK(lifecycle::fingerprint().hash == clean.hash);
}

// BUG-1047: the call guard registry: a hold sets and clears its guard; a hold a caught fault cut short (unwind) leaves
// it set, and the frame's reset clears every registered guard (a flag or a depth counter) that is still set
static bool g_cg_a, g_cg_b, g_cg_free;
static int g_cg_depth;
CALL_GUARD("t.a", g_cg_a);
CALL_GUARD("t.b", g_cg_b);
CALL_GUARD_DEPTH("t.depth", g_cg_depth);
static void test_call_guard()
{
    {
        callguard::Hold h(g_cg_a);
        CHECK(g_cg_a);
        {
            callguard::Depth d(g_cg_depth);
            CHECK(g_cg_depth == 1);
        }
        CHECK(g_cg_depth == 0);
    }
    CHECK(!g_cg_a);
    {
        callguard::Hold h(g_cg_b, false); // `flag = local`: may hold false
        CHECK(!g_cg_b);
    }
    CHECK(callguard::reset(true) == 0); // nothing set outside the holds
    CHECK(callguard::unwind("t.a") && callguard::unwind("t.depth") && !callguard::unwind("t.none"));
    {
        callguard::Hold h(g_cg_a);
        callguard::Depth d(g_cg_depth);
    }
    CHECK(g_cg_a && g_cg_depth == 1); // as a fault the game caught leaves them
    {
        callguard::Hold h(g_cg_a); // the next hold is a normal one again
    }
    CHECK(!g_cg_a);
    g_cg_a = true;
    g_cg_free = true; // not registered: never touched
    size_t n = callguard::reset(!g_nofix);
    CHECK(n == 2 && !g_cg_a && g_cg_depth == 0 && g_cg_free);
    CHECK(callguard::state().last == "t.a,t.depth");
    CHECK(callguard::status() == "t.a set=0 cleared=1\nt.b set=0 cleared=0\nt.depth set=0 cleared=1\n");
    g_cg_b = true;
    CHECK(callguard::reset(false) == 0 && g_cg_b); // the registry's switch off: left set (0.7.13)
    g_cg_b = false;
}

// A14 (BUG-1130): the off-main game calls, per call site and game function; the first call of a site is reported
static void test_off_main()
{
    offmain::Table t;
    CHECK(t.note(0x10001000, 0xC1A520, 7));   // first of its site
    CHECK(!t.note(0x10001000, 0xC1A520, 8));  // the same site again: counted, not reported
    CHECK(t.note(0x10001000, 0x9FBF00, 7));   // the same place calling another game function is another site
    CHECK(t.note(0x10002000, 0xC1A520, 7));
    CHECK(t.total == 4 && t.used == 3 && t.sites[0].n == 2 && t.sites[0].tid == 7);
    CHECK(t.status(0x10000000) == "off_main calls=4 sites=3\n"
                                  "  coop.dll+0x1000 -> game 0xC1A520 calls=2 first_thread=7\n"
                                  "  coop.dll+0x1000 -> game 0x9FBF00 calls=1 first_thread=7\n"
                                  "  coop.dll+0x2000 -> game 0xC1A520 calls=1 first_thread=7\n");
    for (uintptr_t i = 0; i < offmain::Table::kMax; i++)
        t.note(0x20000000 + i, 1, 9);
    CHECK(t.used == offmain::Table::kMax && t.dropped == 3 && t.total == 4 + offmain::Table::kMax);
    CHECK(t.status(0).find(" dropped=3\n") != std::string::npos);
    t.clear();
    CHECK(t.total == 0 && t.used == 0 && t.status(0) == "off_main calls=0 sites=0\n");
}

// D3 (npc_proxy_rules.h): the guest's proxy rules of npc.cpp (staleness BUG-0009 / BUG-0611, a body that stays gone
// BUG-0383, the guest's own NPCs BUG-0412, the placing BUG-0550 / BUG-0281, the nest mound BUG-0562)
static void test_proxy_rules()
{
    printf("npc proxy rules\n");
    using namespace npc_rules;
    // the states flow only within 0.5 s of the last complete batch: a host standing still drops nothing
    CHECK(states_flow(10.0, 9.6) && !states_flow(10.0, 9.4));
    // fresh within 5 s of its own last state
    CHECK(stale(100.0, 95.5, 0.0) == Stale::Fresh);
    CHECK(stale(100.0, 95.0, 0.0) == Stale::Fresh); // (exactly 5 s: still fresh, `> 5` drops)
    CHECK(stale(100.0, 94.0, 0.0) == Stale::Drop);
    // BUG-0611: after a freeze every proxy's last state is old; for 5 s after the states flow again none is dropped
    CHECK(stale(100.0, 60.0, 97.0) == Stale::Thaw);
    CHECK(stale(100.0, 60.0, 95.0) == Stale::Thaw);
    CHECK(stale(100.0, 60.0, 94.9) == Stale::Drop);
    // BUG-0383: a body this game made and removed stays gone; a body first seen dead is made (it plays its death)
    CHECK(!remake(true, true) && remake(true, false) && remake(false, true) && remake(false, false));
    // BUG-0412: the own NPCs stay while the host is away, or while nothing is mirrored and the host is not here
    CHECK(keep_locals(true, false, true, true));
    CHECK(keep_locals(false, true, true, false));
    CHECK(!keep_locals(false, true, true, true));   // the host is here: its NPCs come
    CHECK(!keep_locals(false, false, true, false)); // proxies exist: the host owns NPCs
    CHECK(!keep_locals(false, true, false, false)); // (no host_here callback: no hold)
    // the placing
    PlaceIn in;
    in.speed = 4.f;
    in.off = 1.f;
    CHECK(place(in) == Place::Walk);
    in.host_air = true;
    CHECK(place(in) == Place::Air);   // BUG-0550: off its ground on the host, before any walk
    in.host_under = true;
    CHECK(place(in) == Place::Walk);  // BUG-1357: under the host's ground is not in the air: walked on this game's ground
    in.speed = 0.f;
    CHECK(place(in) == Place::Stand); // (standing: driven, drive::step puts it onto the ground)
    in.speed = 4.f;
    in.host_under = false;
    in.host_dead = true;
    CHECK(place(in) == Place::Walk);  // a dead NPC is never put in the air
    in.host_air = in.host_dead = false;
    in.off = 9.f;
    CHECK(place(in) == Place::Stand); // too far from its target: driven (put in place) first
    in.off = 1.f;
    in.speed = 0.3f;
    CHECK(place(in) == Place::Stand); // standing
    in.speed = 4.f;
    in.stage = false;
    CHECK(place(in) == Place::Stand); // not a stage whose proxies are walked
    in.stage = true;
    in.killed = true;
    CHECK(place(in) == Place::Corpse);
    in.host_air = true;
    CHECK(place(in) == Place::Corpse); // a corpse is never in the air or walked
    // BUG-0562: a target 0.5 m inside the mound (surface 1000.5 over terrain 1000) goes 5 cm under the surface
    float k = mound_lift(1000.f, 1000.5f, 1000.f);
    CHECK(k > 1.f && std::fabs(1000.f * k - 1000.45f) < 1e-3f);
    CHECK(mound_lift(1000.f, 1000.05f, 999.f) == 1.f); // on the surface already
    CHECK(mound_lift(1000.f, 1003.f, 999.f) == 1.f);   // 3 m under: not a mound (a cliff, a cave)
    CHECK(mound_lift(1000.f, 1000.5f, 1000.45f) == 1.f); // the surface is the terrain (no mound)
    CHECK(mound_lift(0.5f, 1000.f, 999.f) == 1.f);     // no position
    // BUG-1174 (owner decision 2): held through the host's editor and the second after it, not through a menu or load
    CHECK(editor_hold(true, true, false, false));   // the host is away in its editor
    CHECK(editor_hold(true, false, true, false));   // back in its stage, the away state not over yet
    CHECK(!editor_hold(true, false, false, false)); // away for another reason from the start: the hand-over
    CHECK(!editor_hold(true, true, true, true));    // editor -> main menu / load: away for real
    CHECK(!editor_hold(false, true, true, false));  // not away: nothing to hold
}


// BUG-1292 (found by the Lineages fork, its BUG-9157): a model of the game's base max health (0xC04170) with a null
// species read: every branch that reads species +0x56C faults on a null species, the hook answers those without the
// native. FAILS with --nofix (the switch off: the native runs and the model faults)
static void test_max_hp_null_species()
{
    printf("base max health with a null species (BUG-1292)\n");
    const bool fix = !g_nofix;
    struct Creature {
        bool species = true, adventure = false, has_herd = true, has_tuning = true;
        bool of_player_species = false, ps_null = false; // its species is the world's PlayerSpecies / PlayerSpecies is null
        float herd_hp = 0, tuning_hp = 0, species_hp = 0, kept = 0;
    };
    // the native as the disassembly reads (fault = a read through the null species at 0xC0419D / 0xC04252)
    auto native = [](const Creature& c, bool& fault) {
        fault = false;
        bool is_ps = c.species ? c.of_player_species : c.ps_null; // a null species equals a null PlayerSpecies
        if (c.adventure || is_ps) {
            fault = !c.species;
            return 100.f + (c.species ? c.species_hp : 0);
        }
        if (c.has_herd && c.herd_hp > 0.f)
            return c.herd_hp;
        if (c.has_tuning)
            return c.tuning_hp;
        fault = !c.species;
        return 100.f + (c.species ? c.species_hp : 0);
    };
    auto hook = [&](const Creature& c, bool& fault) {
        if (max_hp_rule::guard(fix, !c.species, c.adventure, c.ps_null, c.has_herd, c.herd_hp, c.has_tuning)) {
            fault = false;
            return max_hp_rule::fallback(c.kept);
        }
        return native(c, fault);
    };
    bool fault = false;
    // the fork's case: no species, no herd health, no tuning
    Creature a;
    a.species = false; a.has_herd = false; a.has_tuning = false; a.kept = 44.f;
    float v = hook(a, fault);
    CHECK(!fault && v == 44.f);
    // a herd without health (0.0) is the same
    Creature b = a;
    b.has_herd = true; b.herd_hp = 0.f; b.kept = 0.f;
    v = hook(b, fault);
    CHECK(!fault && v == 1.f);
    // an adventure reads the species first
    Creature c = a;
    c.adventure = true; c.has_herd = true; c.herd_hp = 30.f;
    v = hook(c, fault);
    CHECK(!fault && v == 44.f);
    // a null species while PlayerSpecies is null too counts as the player species
    Creature d = a;
    d.ps_null = true; d.has_herd = true; d.herd_hp = 30.f;
    v = hook(d, fault);
    CHECK(!fault && v == 44.f);
    // the native stays the answer where it never reads the species: herd health, a tuning, any creature with a species
    Creature e = a;
    e.has_herd = true; e.herd_hp = 30.f;
    CHECK(hook(e, fault) == 30.f && !fault);
    Creature f = a;
    f.has_tuning = true; f.tuning_hp = 12.f;
    CHECK(hook(f, fault) == 12.f && !fault);
    Creature g;
    g.has_herd = false; g.has_tuning = false; g.species_hp = 5.f;
    CHECK(hook(g, fault) == 105.f && !fault);
    CHECK(max_hp_rule::fallback(NAN) == 1.f && max_hp_rule::fallback(-3.f) == 1.f);
}


// BUG-1294 (found by the Lineages fork, its BUG-9166): a waiting share joins only a save posted when or after it was
// asked for. FAILS with --nofix (any save in flight: the share goes out with an older save)
static void test_share_fresh()
{
    printf("a share joins only a save made after it was asked for (BUG-1294)\n");
    using share_wait::Merge;
    const bool fresh = !g_nofix;
    // two shares asked in one frame (BUG-0112): one save for both
    CHECK(share_wait::merge(fresh, 1000, true, 1000) == Merge::Join);
    // a share asked 1 s after another share's save was posted: it waits for its own save
    CHECK(share_wait::merge(fresh, 2000, true, 1000) == Merge::Wait);
    // a share asked before a save that is in flight now: joins it
    CHECK(share_wait::merge(fresh, 500, true, 1000) == Merge::Join);
    // nothing in flight: its own save
    CHECK(share_wait::merge(fresh, 2000, false, 1000) == Merge::None);
}

int main(int argc, char** argv)
{
    g_nofix = argc > 1 && std::string(argv[1]) == "--nofix";
    test_share_wait();
    test_busy_gate();
    test_stage_epoch();
    test_own_height();
    test_world_name();
    test_savestrip_rule();
    test_drive();
    test_pull_per_second();
    test_ids();
    test_hits();
    test_ledger();
    test_fields_wire();
    test_realign_rule();
    test_lifecycle();
    test_fingerprint();
    test_call_guard();
    test_off_main();
    test_proxy_rules();
    test_max_hp_null_species();
    test_share_fresh();
    printf(g_fail ? "npc tests: %d FAILED\n" : "npc tests: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
