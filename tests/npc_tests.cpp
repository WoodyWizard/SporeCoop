// Native tests for the NPC id allocator (npc_ids.h, BUG-0023 / BUG-0044) and the joint-hunt hit
// table (hunt_hits.h, BUG-0099) (make test; built with AddressSanitizer).
#include <cstdio>
#include <set>
#include <unordered_map>

#include "drive_plan.h"
#include "hunt_hits.h"
#include "npc_ids.h"
#include "savestrip_rule.h"
#include "stage_epoch.h"
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

int main()
{
    test_stage_epoch();
    test_savestrip_rule();
    test_drive();
    test_ids();
    test_hits();
    test_ledger();
    printf(g_fail ? "npc tests: %d FAILED\n" : "npc tests: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
