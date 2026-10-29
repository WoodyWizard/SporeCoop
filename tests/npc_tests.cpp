// Native tests for the NPC id allocator (npc_ids.h, BUG-0023 / BUG-0044) and the joint-hunt hit
// table (hunt_hits.h, BUG-0099) (make test; built with AddressSanitizer).
#include <cstdio>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>

#include "drive_plan.h"
#include "hunt_hits.h"
#include "npc_ids.h"
#include "share_wait.h"
#include "swap_ledger.h"

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
    for (int legacy = 0; legacy < 2; legacy++) {
        teamplay::HitTable t(6.0);
        t.legacy = legacy;
        // 1000 creatures hit once each, one per second: only those of the last 6 s matter
        static char targets[1000];
        for (int i = 0; i < 1000; i++)
            t.hit(&targets[i], 1, i);
        if (legacy)
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

int main(int argc, char** argv)
{
    g_nofix = argc > 1 && std::string(argv[1]) == "--nofix";
    test_share_wait();
    test_drive();
    test_ids();
    test_hits();
    test_ledger();
    printf(g_fail ? "npc tests: %d FAILED\n" : "npc tests: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
