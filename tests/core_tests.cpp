// Native tests for the shared mechanisms of wave C of docs/plan-0.7.15.md (make test, under ASan / UBSan and TSan):
//   C1 mirror_registry.h   id <-> object binding, liveness, delete-unseen with holds, the deferred path, the save filter
//   C4 net/snapshot_rx.h   chunked snapshot cycles (tribe / civ / squadron policies) and the hand-off to C1
//   C2 world_id.h          the world identity (owner decision 2: an editor visit is no world change)
//      lifecycle.h         the ordered observers: order, needs_game, re-entrancy, a hook that throws
//   C3 busy_model.h        the host's busy state, the message, the guest's view, the stage rows, the held commands
//      share_logic.h       BUG-1434 (0.7.17 WP-W1): a share sends the played planet's file and the galaxy's files only
// Each case names the stage code it stands for (the variants of the audit, docs/audit-0.7.15/stages.md STG-1..4).
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "arrival_rule.h"
#include "busy_model.h"
#include "captain_logic.h"
#include "lifecycle.h"
#include "mirror_registry.h"
#include "net/snapshot_rx.h"
#include "share_logic.h"
#include "world_id.h"
#include "worldobj_orn.h"

static int g_fail = 0;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                             \
        }                                                         \
    } while (0)

// ---------------------------------------------------------------- a fake game world for the liveness rule
// address -> the serial of the object that lives there now (absent: freed); the noun of each
static std::map<void*, std::pair<uint32_t, uint32_t>> g_world;
static uint32_t g_serial_next = 100;
static uint8_t g_mem[64];
static void* addr(int i) { return &g_mem[i]; }
static mirror::Ref make_obj(int i, uint32_t noun = 7)
{
    uint32_t s = g_serial_next++;
    g_world[addr(i)] = {s, noun};
    return {addr(i), s, noun};
}
static void free_obj(int i) { g_world.erase(addr(i)); }
static bool fake_alive(const mirror::Ref& r)
{
    auto it = g_world.find(r.p);
    return it != g_world.end() && it->second.first == r.serial && (!r.noun || it->second.second == r.noun);
}

struct GData {
    int hp = 0;
};
using Reg = mirror::Registry<uint16_t, GData>;

// ---------------------------------------------------------------- C1
static void registry_bind()
{
    g_world.clear();
    Reg reg("test", fake_alive);
    auto a = make_obj(1), b = make_obj(2);
    CHECK(reg.bind(10, a, true));
    CHECK(reg.find(10) && reg.find(10)->ref.p == a.p && reg.find(10)->made);
    uint16_t id = 0;
    CHECK(reg.id_of(a.p, id) && id == 10);
    // two ids for one pointer: refused, nothing changes (civ.cpp's g_gv_of[...] == id checks, BUG-0951)
    CHECK(!reg.bind(11, a, false));
    CHECK(reg.stats().refused == 1 && !reg.has(11));
    CHECK(reg.id_of(a.p, id) && id == 10);
    // the same id rebound to another object: the old pointer leaves the reverse index
    CHECK(reg.bind(10, b, false));
    CHECK(!reg.bound(a.p) && reg.bound(b.p));
    CHECK(reg.consistent());
    // an entry without an object (the host named a mirror that is not made yet: npc's proxy before its spawn)
    reg.at(12).data.hp = 5;
    CHECK(reg.has(12) && !reg.find(12)->ref && reg.get_live(12) == nullptr);
    reg.detach(10);
    CHECK(reg.has(10) && !reg.bound(b.p) && reg.consistent());
    reg.erase(12);
    CHECK(!reg.has(12));
    CHECK(!reg.bind(13, mirror::Ref{}, false)); // no object: nothing bound
}

// BUG-0844 / BUG-0842 / BUG-0951: another object built at a freed mirror's address is not the mirror
static void registry_liveness()
{
    g_world.clear();
    Reg reg("tribe.gm", fake_alive);
    auto a = make_obj(3);
    reg.bind(1, a, true);
    CHECK(reg.get_live(1) == a.p);
    free_obj(3);
    auto other = make_obj(3); // the game gives the block to the next citizen: same address, another serial
    CHECK(other.p == a.p);
    uint16_t id = 0;
    CHECK(!reg.id_of_live(other.p, id)); // never driven / removed as the mirror
    CHECK(reg.get_live(1) == nullptr);   // and the stale binding is forgotten (no game call)
    CHECK(reg.stats().stale >= 1 && reg.has(1) && !reg.bound(a.p));
    // a mirror rebound later works again
    CHECK(reg.bind(1, other, false) && reg.get_live(1) == other.p);
    // check_live: every dead object of the table at once (npc check_identity)
    auto c = make_obj(4);
    reg.bind(2, c, true);
    free_obj(4);
    CHECK(reg.check_live() == 1 && reg.find(2) && !reg.find(2)->ref);
    // the noun matters (a pointer listed as another noun is not it)
    auto d = make_obj(5, 9);
    mirror::Ref dn = d;
    dn.noun = 7;
    reg.bind(3, dn, false);
    CHECK(reg.get_live(3) == nullptr);
    size_t live = 0;
    reg.for_each_live([&](Reg::Entry&) { live++; });
    CHECK(live == 1);
    CHECK(reg.consistent());
}

// host tables (tribe g_hm, civ g_hv, npc g_host_npcs): an object of the game's lists keeps its id; another object
// at a known address gets a new one and the old id is retired first
static void registry_adopt()
{
    g_world.clear();
    Reg reg("tribe.hm", fake_alive);
    uint16_t next = 1;
    auto alloc = [&]() -> uint16_t { return next++; };
    std::vector<uint16_t> retired;
    auto on_reused = [&](uint16_t id) { retired.push_back(id); };
    auto a = make_obj(6);
    uint16_t id1 = 0, id2 = 0, id3 = 0;
    bool fresh = false;
    CHECK(reg.adopt(a, alloc, on_reused, id1, &fresh) && fresh && id1 == 1);
    CHECK(reg.adopt(a, alloc, on_reused, id2, &fresh) && !fresh && id2 == 1);
    free_obj(6);
    auto b = make_obj(6); // BUG-0951: a new citizen at a freed member's address
    CHECK(reg.adopt(b, alloc, on_reused, id3, &fresh) && fresh && id3 == 2);
    CHECK(retired.size() == 1 && retired[0] == 1 && !reg.has(1) && reg.stats().reused == 1);
    // no id free (alloc answers 0): nothing bound
    auto c = make_obj(7);
    uint16_t id4 = 0;
    CHECK(!reg.adopt(c, [] { return (uint16_t)0; }, on_reused, id4) && !reg.bound(c.p));
    CHECK(reg.consistent());
}

// the sweep after a complete snapshot: Remove calls the stage only for live objects; Keep stays; Defer waits in the
// deferred list until its hold lifts (civ BUG-0493: the planner, a cinematic)
static void registry_sweep()
{
    g_world.clear();
    Reg reg("civ.gv", fake_alive);
    auto a = make_obj(10), b = make_obj(11), c = make_obj(12), d = make_obj(13), e = make_obj(14);
    reg.bind(1, a, true);  // seen
    reg.bind(2, b, true);  // unseen, alive: removed
    reg.bind(3, c, true);  // unseen, dead: only forgotten
    reg.bind(4, d, true);  // unseen, held in a cinematic: kept
    reg.bind(5, e, false); // unseen, in the planner: deferred
    free_obj(12);
    std::vector<uint16_t> destroyed;
    size_t n = reg.sweep([](uint16_t id) { return id == 1; },
                         [](Reg::Entry& en, bool) {
                             return en.id == 4 ? mirror::Unseen::Keep : en.id == 5 ? mirror::Unseen::Defer : mirror::Unseen::Remove;
                         },
                         [&](Reg::Entry& en, bool alive) {
                             if (alive)
                                 destroyed.push_back(en.id);
                         });
    CHECK(n == 3);
    CHECK(destroyed.size() == 1 && destroyed[0] == 2);
    CHECK(reg.has(1) && !reg.has(2) && !reg.has(3) && reg.has(4) && !reg.has(5));
    CHECK(reg.deferred(e.p) && reg.deferred_count() == 1);
    CHECK(reg.stats().kept == 1 && reg.stats().deferred == 1 && reg.stats().removed == 2);
    // the hold holds, then lifts
    bool held = true;
    std::vector<void*> killed;
    reg.pump([&](const mirror::Ref&) { return held; }, [&](const mirror::Ref& r) { killed.push_back(r.p); });
    CHECK(killed.empty() && reg.deferred_count() == 1);
    held = false;
    reg.pump([&](const mirror::Ref&) { return held; }, [&](const mirror::Ref& r) { killed.push_back(r.p); });
    CHECK(killed.size() == 1 && killed[0] == e.p && reg.deferred_count() == 0);
    // a deferred object the game removed first is dropped without a call
    auto f = make_obj(15);
    reg.defer(f, false);
    free_obj(15);
    reg.pump([](const mirror::Ref&) { return false; }, [&](const mirror::Ref& r) { killed.push_back(r.p); });
    CHECK(killed.size() == 1 && reg.stats().deferred_gone == 1);
    // a deferred object bound as a mirror again is not destroyed (civ guest_tick: "a mirror again")
    auto g = make_obj(16);
    reg.defer(g, false);
    reg.bind(6, g, false);
    reg.pump([](const mirror::Ref&) { return false; }, [&](const mirror::Ref& r) { killed.push_back(r.p); });
    CHECK(killed.size() == 1 && reg.deferred_count() == 0 && reg.has(6));
    CHECK(reg.consistent());
}

// the save filter (CLAUDE.md 2): tribe strips the mirrors it made (not the game's own members bound as mirrors),
// civ every bound vehicle and every deferred one (civ save_filter: g_gv_of or g_kill_later)
static void registry_save_filter()
{
    g_world.clear();
    Reg reg("save", fake_alive);
    auto made = make_obj(20), own = make_obj(21), later = make_obj(22), stranger = make_obj(23);
    reg.bind(1, made, true);
    reg.bind(2, own, false);
    reg.defer(later, false);
    CHECK(reg.ours(made.p, 7));
    CHECK(!reg.ours(own.p, 7));       // tribe: a member of the guest's own world stays in its save
    CHECK(reg.ours(own.p, 7, true));  // civ: every bound vehicle leaves the save
    CHECK(reg.ours(later.p, 7, true));
    CHECK(!reg.ours(later.p, 7));     // (not made by us, deferred: civ's rule only)
    CHECK(!reg.ours(stranger.p, 7, true));
    CHECK(!reg.ours(made.p, 8));      // another noun at that address is not ours
    CHECK(reg.stats().stripped == 3);
    reg.clear();
    CHECK(reg.empty() && reg.held() == 0 && reg.stats().bound == 2); // the counters stay
    CHECK(reg.status().find("save: n=0") == 0);
}

// ---------------------------------------------------------------- C4
using Rx = net::SnapshotReceiver<uint16_t>;

static void rx_cycle(Rx& rx, uint16_t cycle, uint8_t chunk, uint8_t chunks, std::vector<uint16_t> ids, bool ok, bool* done,
                     bool* accepted = nullptr)
{
    bool a = rx.begin(cycle, chunk, chunks);
    if (accepted)
        *accepted = a;
    if (!a) {
        *done = false;
        return;
    }
    for (uint16_t id : ids)
        rx.saw(id);
    *done = rx.end(ok);
}

static void snapshot_tribe_civ()
{
    Rx rx; // tribe / civ: 255 chunks, half the range is older, the last chunk completes
    bool done = false, acc = false;
    rx_cycle(rx, 5, 0, 3, {1, 2}, true, &done);
    CHECK(!done);
    rx_cycle(rx, 5, 1, 3, {3}, true, &done);
    CHECK(!done);
    rx_cycle(rx, 5, 2, 3, {4}, true, &done);
    CHECK(done && rx.complete == 1 && rx.seen(1) && rx.seen(4) && !rx.seen(9));
    rx_cycle(rx, 5, 2, 3, {4}, true, &done); // a duplicate of the last chunk: no second sweep
    CHECK(!done && rx.complete == 1);
    // a lost chunk: the cycle never completes, nothing is removed; the next cycle starts empty (abandoned 1)
    rx_cycle(rx, 6, 0, 3, {1}, true, &done);
    rx_cycle(rx, 6, 2, 3, {4}, true, &done);
    CHECK(!done && rx.incomplete == 1);
    uint32_t gen = rx.generation;
    rx_cycle(rx, 7, 0, 1, {8}, true, &done);
    CHECK(done && rx.abandoned == 1 && rx.generation == gen + 1 && !rx.seen(1) && rx.seen(8));
    // BUG-0612: a late chunk of an older cycle is dropped, the newer cycle keeps its entries
    rx_cycle(rx, 6, 1, 3, {2}, true, &done, &acc);
    CHECK(!acc && rx.older == 1 && rx.seen(8) && !rx.seen(2));
    // a cut chunk (the reader failed in the middle) does not count: the cycle stays incomplete
    rx_cycle(rx, 8, 0, 2, {1}, true, &done);
    rx_cycle(rx, 8, 1, 2, {2}, false, &done);
    CHECK(!done && rx.cut == 1);
    // a header that cannot belong to a cycle (civ's fast state chunk 0xFE / chunks 0, squadron's shot list)
    CHECK(!rx.begin(9, 0xFE, 0) && !rx.begin(9, 3, 3) && rx.bad == 2);
    // the u16 counter wraps: 0 after 65535 is newer
    Rx w;
    rx_cycle(w, 65535, 0, 1, {1}, true, &done);
    rx_cycle(w, 0, 0, 1, {2}, true, &done, &acc);
    CHECK(acc && done && w.cur == 0 && w.older == 0);
    // net.cycle_order off (0.7.8): a late older chunk restarts that cycle
    Rx old(net::SnapshotPolicy{255, false, 0x8000, true, true});
    rx_cycle(old, 10, 0, 2, {1}, true, &done);
    rx_cycle(old, 9, 0, 2, {5}, true, &done, &acc);
    CHECK(acc && old.cur == 9 && !old.seen(1) && old.older == 0);
    // BUG-1051: after a session / world end the next host's counter (starting at 1 again) is taken
    rx.reset();
    rx_cycle(rx, 1, 0, 1, {3}, true, &done, &acc);
    CHECK(acc && done && rx.older == 1);
    CHECK(rx.held() == 2); // (the cycle and its one id)
}

static void snapshot_squadron()
{
    // squadron: at most 31 chunks (its mask), 99 behind is older, farther is a restarted host; any chunk that fills
    // the set completes the cycle (a late chunk 0 after the last one)
    Rx rx(net::SnapshotPolicy{31, true, 99, true, false});
    bool done = false, acc = false;
    CHECK(!rx.begin(1, 0, 32));
    rx_cycle(rx, 200, 1, 2, {2}, true, &done);
    CHECK(!done && rx.incomplete == 1);
    rx_cycle(rx, 200, 0, 2, {1}, true, &done);
    CHECK(done && rx.seen(1) && rx.seen(2));
    rx_cycle(rx, 150, 0, 1, {7}, true, &done, &acc); // 50 behind: older, dropped
    CHECK(!acc && rx.older == 1);
    rx_cycle(rx, 50, 0, 1, {7}, true, &done, &acc); // 150 behind: a restarted host, a new cycle
    CHECK(acc && done && rx.cur == 50);
}

// C4 -> C1: only a complete cycle removes, and only what it did not name
static void snapshot_handoff()
{
    g_world.clear();
    Reg reg("tribe.gm", fake_alive);
    for (int i = 0; i < 4; i++)
        reg.bind((uint16_t)(i + 1), make_obj(30 + i), true);
    Rx rx;
    std::vector<uint16_t> removed;
    auto decide = [](Reg::Entry&, bool) { return mirror::Unseen::Remove; };
    auto remove = [&](Reg::Entry& e, bool) { removed.push_back(e.id); };
    rx.begin(1, 0, 2);
    rx.saw(1);
    rx.saw(2);
    CHECK(!rx.end(true));
    // chunk 1 of 2 lost: no sweep
    rx.begin(2, 0, 2);
    rx.saw(1);
    CHECK(!rx.end(true));
    rx.begin(2, 1, 2);
    rx.saw(3);
    CHECK(rx.end(true));
    CHECK(rx.finish(reg, decide, remove) == 2);
    CHECK(removed.size() == 2 && reg.has(1) && !reg.has(2) && reg.has(3) && !reg.has(4));
}

// ---------------------------------------------------------------- C2: world_id
static world_id::Frame fr(uint32_t mode, uint32_t count, uint32_t save = 0, const void* anchor = nullptr)
{
    return world_id::Frame{mode, count, save, anchor};
}

static void world_ids()
{
    using namespace stage_epoch;
    world_id::Tracker t;
    world_id::Id from;
    world_id::Why why;
    int x = 0, y = 0;
    CHECK(!t.step(fr(kTribe, 1, 0xAA, &x))); // the first frame only records (a hot reload)
    CHECK(t.cur.open() && t.cur.stage == 2 && t.cur.serial == 1);
    world_id::Id tribe1 = t.cur;
    // decision 2: an editor visit is the same world (kEditor, then Tribe with the same counter)
    CHECK(!t.step(fr(kEditor, 0)));
    CHECK(!t.step(fr(kEdit, 0)));
    CHECK(!t.step(fr(kTribe, 1, 0xAA, &x)));
    CHECK(t.cur == tribe1);
    // the save name read only now (it was 0): filled in, no change
    world_id::Tracker t2;
    t2.step(fr(kCiv, 3, 0));
    CHECK(!t2.step(fr(kCiv, 3, 0x55)) && t2.cur.save == 0x55);
    // another save in the stage (a reload that showed no other mode): a change
    CHECK(t.step(fr(kTribe, 1, 0xBB, &x), &from, &why) && why == world_id::Why::Save && from == tribe1);
    world_id::Id tribe2 = t.cur;
    CHECK(tribe2 != tribe1 && tribe2.serial > tribe1.serial);
    // another root object (the player tribe of a reloaded world at another address): a change
    CHECK(t.step(fr(kTribe, 1, 0xBB, &y), &from, &why) && why == world_id::Why::Anchor);
    // a frame without the root object (null) says nothing (BUG-0223: a frame without the city anchor)
    CHECK(!t.step(fr(kTribe, 1, 0xBB, nullptr)));
    // the galaxy menu: the world is closed; entered again (counter + 1): opened
    CHECK(t.step(fr(0x02CCD1D2, 0), &from, &why) && why == world_id::Why::Closed && !t.cur.open());
    CHECK(!t.step(fr(0x02CCD1D2, 0)));
    CHECK(t.step(fr(kTribe, 2, 0xBB, &y), &from, &why) && why == world_id::Why::Opened && !from.open());
    // the stage entered again without a frame outside it (the counter moved): a change
    CHECK(t.step(fr(kTribe, 3, 0xBB, &y), &from, &why) && why == world_id::Why::Count);
    // another stage
    CHECK(t.step(fr(kCiv, 1, 0xCC), &from, &why) && why == world_id::Why::Stage && from.stage == 2 && t.cur.stage == 3);
    // an editor of another stage is not this stage's editor... the editors keep every stage; kScenario keeps Space only
    CHECK(t.step(fr(kScenario, 0), &from, &why) && why == world_id::Why::Closed);
    world_id::Tracker s;
    s.step(fr(kSpace, 1, 0x11));
    CHECK(!s.step(fr(kScenario, 0)) && s.cur.open());
    CHECK(!s.step(fr(kSpace, 1, 0x11)));
    CHECK(t.changes == 7 && t.status().find("changes 7") != std::string::npos);
}

// ---------------------------------------------------------------- C2: lifecycle observers
static std::vector<std::string> g_log;
static void h_a(const lifecycle::Ctx&) { g_log.push_back("a"); }
static void h_b(const lifecycle::Ctx&) { g_log.push_back("b"); }
static void h_c(const lifecycle::Ctx&) { g_log.push_back("c"); }
static void h_game(const lifecycle::Ctx& c) { g_log.push_back(c.event == lifecycle::kWorldChange ? "game:w" : "game:s"); }
static void h_throw(const lifecycle::Ctx&)
{
    g_log.push_back("throw");
    throw std::runtime_error("a hook fails");
}
static void h_refire(const lifecycle::Ctx& c)
{
    g_log.push_back("refire");
    if (c.event == lifecycle::kSessionEnd) {
        lifecycle::Ctx w;
        w.event = lifecycle::kWorldChange;
        lifecycle::fire(w, true); // a session end that also ends the world: queued, not nested
        g_log.push_back("refire-after");
    }
}
static void h_join(const lifecycle::Ctx& c) { g_log.push_back("join" + std::to_string(c.player)); }
static bool g_state_on;
static void st_reset() { g_log.push_back("state"); }
static size_t st_held() { return 0; }
static bool faulty_runner(lifecycle::HookFn fn, const lifecycle::Ctx& c)
{
    if (fn == h_b)
        return false; // as a caught game fault (SEH) inside the hook
    fn(c);
    return true;
}

static void lifecycle_hooks()
{
    using namespace lifecycle;
    Fanout saved = fanout();
    auto saved_table = table();
    fanout() = Fanout{};
    table().clear();
    table().push_back({"t.state", kSession | kWorld, &g_state_on, st_reset, st_held});
    g_state_on = true;
    {
        HookReg r1("t.c", kSessionEnd, 5, h_c, false);
        HookReg r2("t.a", kSessionEnd | kWorldChange, -10, h_a, false);
        HookReg r3("t.b", kSessionEnd, -10, h_b, false);
        HookReg r4("t.game", kSessionEnd | kWorldChange, -5, h_game, true);
        HookReg r5("t.join", kPlayerJoin, 0, h_join, false);
    }
    Ctx s;
    s.event = kSessionEnd;
    // order: -10 (a, then b: registration order), -5, the state resets at 0, then 5
    g_log.clear();
    fire(s, true);
    CHECK((g_log == std::vector<std::string>{"a", "b", "game:s", "state", "c"}));
    CHECK(resets(kSession) == 1);
    // inside g_sess.update() (game_ok false): the game hook waits for pump_game, the rest runs now in order
    g_log.clear();
    fire(s, false);
    CHECK((g_log == std::vector<std::string>{"a", "b", "state", "c"}));
    CHECK(fanout().game_queue.size() == 1 && fanout().queued == 1);
    pump_game();
    CHECK(g_log.back() == "game:s" && fanout().game_queue.empty());
    // events: a join hook does not run for a session end, and gets the player
    g_log.clear();
    Ctx j;
    j.event = kPlayerJoin;
    j.player = 3;
    fire(j, true);
    CHECK((g_log == std::vector<std::string>{"join3"}));
    // a world change: only its hooks, its scope's resets
    g_log.clear();
    Ctx w;
    w.event = kWorldChange;
    fire(w, true);
    CHECK((g_log == std::vector<std::string>{"a", "game:w", "state"}));
    CHECK(resets(kWorld) == 1);
    // a hook that throws: counted, the fan-out goes on
    {
        HookReg r6("t.throw", kSessionEnd, -7, h_throw, false);
    }
    g_log.clear();
    fire(s, true);
    CHECK((g_log == std::vector<std::string>{"a", "b", "throw", "game:s", "state", "c"}));
    CHECK(fanout().faults == 1 && fanout().last_fault == "t.throw");
    // re-entrancy: a hook that fires another event: it runs after this fan-out, not inside it
    {
        HookReg r7("t.refire", kSessionEnd, 1, h_refire, false);
    }
    g_log.clear();
    fire(s, true);
    CHECK((g_log == std::vector<std::string>{"a", "b", "throw", "game:s", "state", "refire", "refire-after", "c", "a",
                                            "game:w", "state"}));
    CHECK(fanout().nested == 1 && !fanout().running);
    // a runner that reports a caught fault for one hook
    set_runner(faulty_runner);
    uint32_t f0 = fanout().faults;
    g_log.clear();
    fire(w, true);
    CHECK(fanout().faults == f0); // (b is not a world hook)
    fire(s, true);
    CHECK(fanout().faults >= f0 + 2 && fanout().hooks[2].faults == 1); // t.b (index 2) and t.throw
    set_runner(nullptr);
    // the registry's switch off: the state resets do not run, the hooks do
    fanout().state_resets = false;
    g_log.clear();
    fire(w, true);
    CHECK((g_log == std::vector<std::string>{"a", "game:w"}));
    CHECK(hooks_status(kSessionEnd).find("-10 t.a SW calls=") == 0);
    CHECK(hooks_status().find("t.game SW game calls=") != std::string::npos);
    fanout() = saved;
    table() = saved_table;
}

// ---------------------------------------------------------------- C3
using namespace busy_model;

static Reading stand(std::vector<std::pair<uint32_t, int>> reasons, bool modal = false, bool cine = false)
{
    Reading r;
    r.time_stands = true;
    r.reasons = std::move(reasons);
    r.modal_window = modal;
    r.cinematic = cine;
    return r;
}

static void busy_classify()
{
    Reading menu;
    menu.menu = true;
    CHECK(classify(menu).state == State::Away && classify(menu).why == kWhyMenu);
    Reading load;
    load.loading = true;
    CHECK(classify(load).state == State::Away && (classify(load).why & kWhyLoad));
    Reading ed;
    ed.editor = true;
    ed.time_stands = true; // (an editor stands the time too: it is the editor)
    CHECK(classify(ed).state == State::Editor);
    CHECK(classify(Reading{}).state == State::None && classify(Reading{}).why == 0);
    // the explicit pauses (lane 4, 2026-11-15: Creature, Tribe, Civ, Space): the pause menu 0x4BF38A4, P 0x4BF38A8
    CHECK(classify(stand({{kReasonMenu, 1}}, true)).state == State::Paused);
    CHECK(classify(stand({{kReasonMenu, 1}}, true)).why & kWhyPauseMenu);
    CHECK(classify(stand({{kReasonKeyP, 1}})).state == State::Paused);
    // a tutorial box: the UI-modal reason with a modal window
    CHECK(classify(stand({{kReasonModal, 1}}, true)).state == State::Paused);
    // the game's own stands are no pause: the Tribe entry (0x4BF38A7 without a window, BUG-0313), a caption's cinematic
    // pause
    Classified entry = classify(stand({{kReasonModal, 1}}));
    CHECK(entry.state == State::None && (entry.why & kWhyCaption));
    CHECK(classify(stand({{kReasonCine1, 1}}, false, true)).state == State::None);
    CHECK(classify(stand({{kReasonModal, 1}, {kReasonCine2, 1}}, true, true)).state == State::None); // the host's caption
    // the Sporepedia (lane 4: 0x4BF38A6, flags 1) is a pause; the same reason while an editor is coming is not
    CHECK(classify(stand({{kReasonEditor, 1}})).state == State::Paused);
    CHECK(classify(stand({{kReasonEditor, 1}})).why & kWhySporepedia);
    Reading coming = stand({{kReasonEditor, 1}});
    coming.editor_coming = true;
    CHECK(classify(coming).state == State::None && (classify(coming).why & kWhyCaption));
    // Cell (lane 4, 2026-11-15): its pause menu sets no time manager reason; its window is the sign
    Reading cell;
    cell.pause_menu_window = true;
    CHECK(classify(cell).state == State::Paused && classify(cell).why == kWhyPauseMenu);
    // P under a caption is still the player's pause
    CHECK(classify(stand({{kReasonCine1, 1}, {kReasonKeyP, 1}}, false, true)).state == State::Paused);
    // BUG-1217: the host's own save stands its game (the write, the "game saved" box): neither a caption nor a pause
    Reading save = stand({{kReasonModal, 1}}, true);
    save.saving = true;
    CHECK(classify(save).state == State::None && classify(save).why == 0);
    Reading save_bare = stand({});
    save_bare.saving = true;
    CHECK(classify(save_bare).state == State::None && classify(save_bare).why == 0);
    save_bare.saving = false; // (the same stand without the save: the game's own, told to Tribe)
    CHECK(classify(save_bare).why & kWhyCaption);
    // the explicit pause during a save is still the player's pause (the pause menu's own Save)
    Reading save_menu = stand({{kReasonMenu, 1}});
    save_menu.saving = true;
    CHECK(classify(save_menu).state == State::Paused && classify(save_menu).why == kWhyPauseMenu);
    CHECK(std::string(state_name(State::Paused)) == "paused");
}

static void busy_codec()
{
    Msg m{1, State::Paused, (uint16_t)(kWhyKeyP | kWhyStill), 0x1654C02, 0x9652F8AD, 0x0102};
    std::vector<uint8_t> b = encode(m);
    const uint8_t golden[kMsgSize] = {0x01, 0x01, 0x48, 0x00, 0x02, 0x4C, 0x65, 0x01, 0xAD, 0xF8, 0x52, 0x96, 0x02, 0x01};
    CHECK(b.size() == kMsgSize && memcmp(b.data(), golden, kMsgSize) == 0);
    Msg d;
    CHECK(decode(b.data(), b.size(), d) && d.state == State::Paused && d.why == m.why && d.mode == m.mode &&
          d.world == m.world && d.seq == m.seq);
    std::vector<uint8_t> longer = b;
    longer.push_back(0xEE); // a newer host's tail
    CHECK(decode(longer.data(), longer.size(), d));
    CHECK(!decode(b.data(), kMsgSize - 1, d)); // short
    std::vector<uint8_t> bad = b;
    bad[1] = 9; // a state this build does not know
    CHECK(!decode(bad.data(), bad.size(), d));
    bad = b;
    bad[0] = 0;
    CHECK(!decode(bad.data(), bad.size(), d));
}

static void busy_announcer()
{
    Announcer a;
    Msg m;
    Classified none;
    CHECK(a.step(0.0, none, 1, 2, &m) && m.state == State::None && m.seq == 1); // the first state of a session
    CHECK(!a.step(0.1, none, 1, 2, &m));
    Classified p{State::Paused, kWhyPauseMenu | kWhyStill};
    CHECK(!a.step(1.0, p, 1, 2, &m));
    CHECK(!a.step(1.9, p, 1, 2, &m)); // BUG-1086's second: a frame's stand is not told
    CHECK(a.step(2.05, p, 1, 2, &m) && m.state == State::Paused && m.seq == 2);
    CHECK(a.step(2.1, none, 1, 2, &m) && m.state == State::None); // the end at once
    // a short pause (0.5 s) is never told
    CHECK(!a.step(3.0, p, 1, 2, &m));
    CHECK(!a.step(3.5, none, 1, 2, &m));
    // an editor at once (the guests keep their own 4 s wait)
    CHECK(a.step(4.0, Classified{State::Editor, 0}, 1, 2, &m) && m.state == State::Editor);
    // a guest that joins now gets the state as last told
    Msg j = a.current(1, 2);
    CHECK(j.state == State::Editor && j.seq == a.seq);
    a.reset();
    CHECK(a.step(5.0, none, 1, 2, &m) && m.seq == 1);
}

static std::vector<uint8_t> msg_bytes(State s, uint16_t seq, uint16_t why = 0)
{
    return encode(Msg{1, s, why, 0x1654C02, 7, seq});
}

// BUG-1419 (PR #216 by IltonBJSilva): the host tells "an editor's load is coming" at once (no debounce), and a guest waits longer for a
// host whose last told state is such a stand; the next state is told as usual
static void busy_long_wait()
{
    Announcer a;
    Msg m;
    CHECK(a.step(0.0, Classified{}, 0x1654C02, 7, &m) && m.seq == 1);
    Msg lw = a.announce({State::Editor, (uint16_t)kWhyLongWait}, 0x1654C02, 7);
    CHECK(lw.seq == 2 && lw.state == State::Editor && (lw.why & kWhyLongWait) && a.sent.state == State::Editor);
    // the editor is open a frame later (why 0): told, a newer seq
    Classified ed{State::Editor, 0};
    CHECK(a.step(1.0, ed, 0xDBDBA1, 7, &m) && m.seq == 3 && m.why == 0);
    CHECK(!a.step(1.1, ed, 0xDBDBA1, 7, &m)); // nothing new
    std::vector<uint8_t> b = encode(lw);
    Msg d;
    CHECK(decode(b.data(), b.size(), d) && (d.why & kWhyLongWait));
    // the guest's timeout
    CHECK(guest_timeout_ms(false, lw, 30000) == 30000); // nothing heard (an older host): the session's own
    CHECK(guest_timeout_ms(true, lw, 30000) == kLongWaitMs);
    CHECK(guest_timeout_ms(true, Msg{1, State::Editor, 0, 0, 0, 3}, 30000) == kLongWaitMs);
    CHECK(guest_timeout_ms(true, Msg{1, State::Away, (uint16_t)kWhyLoad, 0, 0, 4}, 30000) == kLongWaitMs);
    CHECK(guest_timeout_ms(true, Msg{1, State::Away, (uint16_t)kWhyMenu, 0, 0, 4}, 30000) == 30000);
    CHECK(guest_timeout_ms(true, Msg{1, State::Paused, (uint16_t)kWhyPauseMenu, 0, 0, 5}, 30000) == 30000);
    CHECK(guest_timeout_ms(true, Msg{1, State::None, 0, 0, 0, 6}, 30000) == 30000);
    CHECK(kLongWaitMs >= 120000); // longer than the 51.8 s seen (PR #216) with room
}

static void busy_host_view()
{
    HostView v;
    Fallback none;
    // decision 1: a guest's pause stops nobody: a busy message from a guest (id 2) is not taken
    auto g = msg_bytes(State::Paused, 1);
    CHECK(!v.on_message(2, g.data(), g.size()) && !v.heard && v.dropped == 1);
    // the host's pause: at once (the host held it 1 s)
    auto p = msg_bytes(State::Paused, 1, kWhyPauseMenu);
    CHECK(v.on_message(0, p.data(), p.size()));
    Edge e = v.step(10.0, none);
    CHECK(e.changed() && e.to == State::Paused);
    CHECK(note(e, "Alice", Act::Wait) == "Alice paused the game: yours waits until Alice goes on");
    // an older seq (reordered after a newer) is dropped
    auto old = msg_bytes(State::None, 0);
    CHECK(!v.on_message(0, old.data(), old.size()));
    auto go = msg_bytes(State::None, 2);
    CHECK(v.on_message(0, go.data(), go.size()));
    e = v.step(10.1, none);
    CHECK(e.from == State::Paused && e.to == State::None && note(e, "Alice", Act::On) == "Alice (host) goes on: so does your game");
    // the host's editor: after the 4 s of 0.7.14 (a vote's shared editor opening here a moment later is not away)
    auto ed = msg_bytes(State::Editor, 3);
    v.on_message(0, ed.data(), ed.size());
    CHECK(!v.step(11.0, none).changed());
    CHECK(!v.step(14.9, none).changed());
    e = v.step(15.1, none);
    CHECK(e.to == State::Editor && note(e, "Alice", Act::Local).find("is in an editor") != std::string::npos);
    // back: after 1 s
    auto back = msg_bytes(State::None, 4);
    v.on_message(0, back.data(), back.size());
    CHECK(!v.step(15.5, none).changed());
    e = v.step(16.6, none);
    CHECK(e.to == State::None && note(e, "Alice", Act::On) == "Alice (host) is back");

    // a 0.7.14 host (no MSG_HOST_BUSY): its PlayerState mode, as coop.cpp host_out_of_stage
    HostView o;
    Fallback menu{'m', false};
    CHECK(!o.step(0.0, menu).changed());
    CHECK(!o.step(1.4, menu).changed());
    e = o.step(1.6, menu);
    CHECK(e.to == State::Away && (e.why & kWhyMenu));
    CHECK(note(e, "Alice", Act::Local).find("left for the main menu") != std::string::npos);
    Fallback in{0, false};
    o.step(2.0, in);
    CHECK(o.step(3.1, in).to == State::None);
    // and Tribe's kind 21 (a 0.7.14 host in Tribe): its pause at once
    Fallback tp{0, true};
    CHECK(o.step(4.0, tp).to == State::Paused);
    // a passing load (3 s) is not away
    Fallback ld{'l', false};
    HostView q;
    q.step(0.0, ld);
    CHECK(!q.step(3.0, ld).changed());
    CHECK(!q.step(3.1, in).changed() && q.settled == State::None);
}

static void busy_rows()
{
    Edge paused{State::None, State::Paused, kWhyPauseMenu};
    Edge editor{State::None, State::Editor, 0};
    Edge away{State::None, State::Away, kWhyMenu};
    Edge caption{State::None, State::None, kWhyCaption};
    Edge none{State::None, State::None, 0};
    // decision 1: the host's explicit pause makes the guests wait in every stage ...
    for (int s = 0; s < stage_epoch::kStages; s++) {
        CHECK(act(s, paused, false) == Act::Wait);
        // ... but not in Rivals mode
        CHECK(act(s, paused, true) == Act::On);
        CHECK(act(s, none, false) == Act::On);
    }
    // host away / editor stay their own state: the guest plays on alone (Cell, Creature, Tribe, Civ: BUG-0151 /
    // BUG-0130 / BUG-0205); Space's modules keep their own rules
    for (int s = 0; s < 4; s++) {
        CHECK(act(s, editor, false) == Act::Local);
        CHECK(act(s, away, false) == Act::Local);
        CHECK(act(s, editor, true) == Act::Local);
    }
    CHECK(act(4, editor, false) == Act::On && act(4, away, false) == Act::On);
    // the game's own stand (a caption): Tribe waits as since BUG-1086, the others play on; Rivals never waits
    for (int s = 0; s < stage_epoch::kStages; s++)
        CHECK(act(s, caption, false) == (s == 2 ? Act::Wait : Act::On));
    CHECK(act(2, caption, true) == Act::On);
    // no stage (a menu of this guest): nothing
    CHECK(act(-1, paused, false) == Act::On && act(7, paused, false) == Act::On);
    // the held commands: Tribe and Civ (BUG-0693), Civ says the command waits
    CHECK(row_of(2)->hold_cmds && row_of(3)->hold_cmds && !row_of(0)->hold_cmds && !row_of(1)->hold_cmds);
    CHECK(row_of(3)->say_cmd_waits && !row_of(2)->say_cmd_waits);
    CHECK(std::string(row_of(4)->name) == "space");
}

static void busy_pump()
{
    const Row& tribe = *row_of(2);
    const Row& civ = *row_of(3);
    const Row& cell = *row_of(0);
    HeldPump p;
    uint8_t cmd[3] = {1, 2, 3};
    CHECK(!p.hold(cell, 1, cmd, 3, 0xAA, 0.0)); // a row that does not hold: dropped as before
    CHECK(p.hold(tribe, 1, cmd, 3, 0xAA, 0.0) && p.hold(tribe, 2, cmd, 3, 0xAA, 0.1) && p.hold(tribe, 1, cmd, 2, 0xBB, 0.2));
    CHECK(p.held_of(1) == 2);
    std::vector<uint8_t> ran;
    auto run = [&](const net::HeldCommands::Cmd& c) { ran.push_back(c.from); };
    CHECK(p.tick(tribe, false, 0xAA, 1.0, run) == 0); // the stage does not run
    size_t early = 0;
    for (double t = 10.0; t < 12.95; t += 0.1) // it just came back: it settles kSettle s first (a tick per frame)
        early += p.tick(tribe, true, 0xAA, t, run);
    CHECK(early == 0);
    CHECK(p.tick(tribe, true, 0xAA, 13.1, run) == 2); // the two of this world; the one of another save is dropped
    CHECK(ran.size() == 2 && p.q.dropped == 1 && p.q.q.empty());
    // a gap of more than 1 s in the stage's ticks is a new settle (an editor visit in between)
    p.hold(tribe, 3, cmd, 3, 0xAA, 13.2);
    CHECK(p.tick(tribe, true, 0xAA, 20.0, run) == 0);
    // a leaver's held commands go with it (player_life.h)
    p.forget(3);
    CHECK(p.q.q.empty());
    // the guest's note: Civ only, at most every 5 s
    HeldPump g;
    CHECK(g.say_due(civ, 0.0) && !g.say_due(civ, 4.0) && g.say_due(civ, 5.1));
    CHECK(!g.say_due(tribe, 100.0));
}

// BUG-1336: the first Creature editor's vote after Cell waits for a guest whose arrival is quick (lab times: the host
// saves for the share ~2 s after the vote's start, a normal link sends the world in ~1 s, the guest loads it in ~5 s)
static void arrival_wait()
{
    using arrival_rule::waits_for;
    CHECK(waits_for(false, true, 0.1, 1e9));   // the vote starts before the host has asked for the share
    CHECK(waits_for(false, true, 4.0, 2.0));   // the share is being saved / sent
    CHECK(waits_for(false, false, 6.0, 4.0));  // acknowledged: the guest loads the world
    CHECK(waits_for(false, false, 30.0, 35.0));
    CHECK(!waits_for(false, true, 8.5, 6.0));  // BUG-1245: a slow link still sending after 8 s: not waited for
    CHECK(!waits_for(false, false, 45.0, 50.0)); // the world long out and no arrival: not waited for
    CHECK(!waits_for(false, false, 1.0, 1e9)); // no share at all (another world): not waited for
    CHECK(!waits_for(true, true, 0.1, 1e9));   // BUG-1239: in its own editor
    printf("  arrival_wait ok\n");
}

// ---------------------------------------------------------------- BUG-1434: the files of a share of one planet
struct ShareFile {
    std::string rel;
};
static void share_one_planet()
{
    using namespace share_logic;
    CHECK(is_planet("Szaros.spo") && is_planet("old.SPO") && !is_planet("snapshot.spo") && !is_planet("Snapshot.SPO"));
    CHECK(!is_planet("stars.db") && !is_planet("GGEUserData.dat") && !is_planet("sub/x.spo") && !is_planet(".spo"));
    CHECK(left_out("Afrincsia.spo", "Szaros.spo") && !left_out("szaros.SPO", "Szaros.spo"));
    CHECK(!left_out("snapshot.spo", "Szaros.spo") && !left_out("lastSave.pld", "Szaros.spo"));
    // the report's slot: the played planet, two old ones and the galaxy's files
    std::vector<ShareFile> f = {{"stars.db"}, {"Afrincsia.spo"}, {"planetRecords.pkp"}, {"Szaros.spo"},
                                {"GGEUserData.dat"}, {"snapshot.spo"}, {"Old.spo"}, {"lastSave.pld"}};
    CHECK(keep_played(f, "Szaros.spo") == 2);
    CHECK(f.size() == 6);
    bool played = false, old = false;
    for (auto& x : f) {
        played |= x.rel == "Szaros.spo";
        old |= x.rel == "Afrincsia.spo" || x.rel == "Old.spo";
    }
    CHECK(played && !old);
    // the played planet not in the list (a menu share, a planet not saved yet): the slot goes as it is
    std::vector<ShareFile> g = {{"stars.db"}, {"A.spo"}, {"B.spo"}};
    CHECK(keep_played(g, "C.spo") == 0 && g.size() == 3);
    CHECK(keep_played(g, "SaveGame") == 0 && keep_played(g, "") == 0 && g.size() == 3);
    // only to a guest with the world family 1.1 or newer and the same rev (an older guest: the whole slot)
    CHECK(peer_takes_one(true, true, 1, 1, 1) && peer_takes_one(true, true, 1, 2, 1));
    CHECK(!peer_takes_one(true, true, 1, 0, 1) && !peer_takes_one(false, false, 0, 0, 1) && !peer_takes_one(true, false, 0, 0, 1));
    CHECK(!peer_takes_one(true, true, 2, 1, 1));
    // the copy notice: a copy of one planet has no old planets
    CHECK(other_planets({"Szaros.spo", "stars.db", "snapshot.spo"}, "Szaros") == 0);
    CHECK(other_planets({"Szaros.spo", "Afrincsia.spo", "Old.spo"}, "Szaros") == 2);
}

// BUG-1261: Creature interactive ornament fade-out and kGone sync rules (worldobj_orn.h)
static void ornament_fade_rules()
{
    using namespace wobj_orn;
    // Flag bit decoding (+0x238: bit 0 blocks Update, bit 1 fading, bit 2 hide instead of destroy)
    CHECK(!flags_fading_to_destroy(0u));
    CHECK(flags_fading_to_destroy(2u));
    CHECK(!flags_fading_to_destroy(3u)); // bit 0 set: Update skips fade
    CHECK(!flags_fading_to_destroy(6u)); // bit 2 set: hide mode, not destroy
    CHECK(flags_fading_to_destroy(0x102u));

    // State encoding (fix on vs off, wire-compatible with 0.7.15 bit 0)
    CHECK(encode_state(false, 0u, true) == 0u);
    CHECK(encode_state(true, 0u, true) == kUsed);
    CHECK(encode_state(false, 2u, true) == kFading);
    CHECK(encode_state(true, 2u, true) == (kUsed | kFading));
    CHECK(encode_state(true, 2u, false) == kUsed);
    CHECK(encode_state(false, 2u, false) == 0u);

    // Non-fading use (coop.wobj use without fade, wobj_ornament / wobj_ornament_model tests): never fades
    {
        Plan p = apply_plan(0u, kUsed, true);
        CHECK(p.use && !p.fade && behind(0u, kUsed, true));
        CHECK(!behind(kUsed, kUsed, true));
        CHECK(acted(0u, kUsed, 0u, true));
        CHECK(!acted(kUsed, kUsed, 0u, true));
    }

    // Real host dig (kUsed | kFading): guest runs use + fade once, never restarts fade
    {
        Plan p = apply_plan(0u, kUsed | kFading, true);
        CHECK(p.use && p.fade && behind(0u, kUsed | kFading, true));
        Plan p2 = apply_plan(kUsed, kUsed | kFading, true);
        CHECK(!p2.use && p2.fade && behind(kUsed, kUsed | kFading, true));
        CHECK(!behind(kUsed | kFading, kUsed | kFading, true));
        CHECK(!behind(kUsed | kFading, kGone, true));
        CHECK(!behind(kFading, kGone, true));
    }

    // Host TierBoneCleanup on-screen (kFading without kUsed): guest marks unclickable + fades, no echo back to host
    {
        Plan p = apply_plan(0u, kFading, true);
        CHECK(p.use && p.fade && behind(0u, kFading, true));
        CHECK(!acted(0u, kUsed | kFading, kFading, true));
    }

    // Host kGone (far dig, TierBoneCleanup off-screen, or old 0.7.15 host): guest marks unclickable + fades once
    {
        Plan p0 = apply_plan(0u, kGone, true);
        CHECK(p0.use && p0.fade && behind(0u, kGone, true));
        Plan p1 = apply_plan(kUsed, kGone, true);
        CHECK(!p1.use && p1.fade && behind(kUsed, kGone, true));
        // Switch off (0.7.15 behavior): kGone and kFading are ignored
        Plan off0 = apply_plan(0u, kGone, false);
        CHECK(!off0.use && !off0.fade && !behind(0u, kGone, false));
        Plan off1 = apply_plan(kUsed, kUsed | kFading, false);
        CHECK(!off1.use && !off1.fade && !behind(kUsed, kUsed | kFading, false));
    }

    // Real guest dig -> host act (including mid-dig Update -> Exit split and gone_acted fallback)
    {
        CHECK(acted(0u, kUsed | kFading, 0u, true));
        CHECK(acted(kUsed, kUsed | kFading, kUsed, true));
        CHECK(acted(kUsed | kFading, kUsed | kFading, kUsed, true));
        CHECK(!acted(kUsed | kFading, kUsed | kFading, kUsed | kFading, true));
        CHECK(acted(kUsed | kFading, kGone, kUsed, true));
        CHECK(!acted(0u, kGone, 0u, true));
        CHECK(!acted(kUsed, kGone, kUsed, true));
        CHECK(!acted(kUsed | kFading, kGone, kUsed, false));
    }

    // Invariant: behind(l, r, f) == (apply_plan(l, r, f).use || apply_plan(l, r, f).fade)
    const bool fixes[] = {false, true};
    const uint32_t locals[] = {0u, kUsed, kFading, kUsed | kFading};
    const uint32_t remotes[] = {0u, kUsed, kFading, kUsed | kFading, kGone};
    for (bool fix : fixes)
        for (uint32_t l : locals)
            for (uint32_t r : remotes) {
                Plan p = apply_plan(l, r, fix);
                CHECK(behind(l, r, fix) == (p.use || p.fade));
            }
    // the review's fixes (0.7.17): kGone only from a host of kFadeMinor, kGoneSettleS after it was away
    CHECK(gone_trusted(true, true, 100.0, 80.0));
    CHECK(!gone_trusted(true, true, 100.0, 95.0));
    CHECK(!gone_trusted(true, false, 100.0, 0.0));
    CHECK(!gone_trusted(false, true, 100.0, 0.0));
    // a guest under an old host reports uses only (acted called with fade_fix && host_new == false)
    CHECK(!acted(0u, kFading, 0u, false) && acted(0u, kUsed | kFading, 0u, false) && !acted(kUsed | kFading, kGone, kUsed, false));
    printf("  ornament_fade_rules ok\n");
}



// ---------------------------------------------------------------- WP-K1 (captain_logic.h): the per-player captain
static void be32(std::vector<uint8_t>& v, uint32_t x)
{
    for (int i = 3; i >= 0; i--)
        v.push_back((uint8_t)(x >> (8 * i)));
}
static void prop_head(std::vector<uint8_t>& v, uint32_t id, uint16_t type, uint16_t flags)
{
    be32(v, id);
    v.push_back((uint8_t)(type >> 8)), v.push_back((uint8_t)type);
    v.push_back((uint8_t)(flags >> 8)), v.push_back((uint8_t)flags);
}
// a captain's list as the game writes one: the item config (a key), the newer version link (a key), the name
// (string16), an array of unlocked item keys, a bool
static std::vector<uint8_t> captain_list(bool parent, bool newer)
{
    std::vector<uint8_t> v;
    be32(v, (parent ? 0x80000000u : 0u) | (newer ? 5u : 4u));
    if (parent)
        for (int i = 0; i < 3; i++)
            be32(v, 0x11111111u * (i + 1));
    prop_head(v, captain::kPropItems, 0x20, 0);
    for (int i = 0; i < 4; i++)
        be32(v, 0xA0 + i);
    if (newer) {
        prop_head(v, captain::kPropNewer, 0x20, 0);
        for (int i = 0; i < 4; i++)
            be32(v, 0xB0 + i);
    }
    prop_head(v, 0x75ED3A1C, 0x13, 0);
    be32(v, 3);
    for (char c : std::string("Bob"))
        v.push_back(0), v.push_back((uint8_t)c);
    prop_head(v, 0xBEAC70B6, 0x20, 0x30); // an array of 2 keys, item size 16
    be32(v, 2);
    be32(v, 16);
    for (int i = 0; i < 8; i++)
        be32(v, 0xC0 + i);
    prop_head(v, 0xC54C543B, 0x01, 0);
    v.push_back(1);
    return v;
}
static void captain_rules()
{
    using namespace captain;
    CHECK(prop_group(0x40626200u) == 0x4062E500u && is_prop_key(kPropType, 0x4062E500u) && !is_prop_key(kPropType, 0x40626200u));
    // the copy's key: 0x7A line, never 0x7A000000, the same on every game, another per owner and content
    CHECK(is_copy(copy_instance(1, 0x12345678)) && copy_instance(1, 0x12345678) == copy_instance(1, 0x12345678));
    CHECK(copy_instance(1, 0x12345678) != copy_instance(2, 0x12345678) && copy_instance(1, 1) != copy_instance(1, 2));
    CHECK(!is_copy(0x7C123456) && !is_copy(0x7A000000));
    for (bool parent : {false, true}) {
        std::vector<uint8_t> in = captain_list(parent, true), out;
        PropScan s;
        CHECK(strip_prop(in, kPropNewer, out, &s) && s.dropped && s.items && !s.newer && s.count == 4);
        CHECK(out.size() == in.size() - 24 && (out[0] & 0x80) == (parent ? 0x80 : 0) && out[3] == 4);
        std::vector<uint8_t> again;
        PropScan s2;
        CHECK(strip_prop(out, kPropNewer, again, &s2) && again == out && !s2.dropped && s2.items); // nothing more to drop
        std::vector<uint8_t> same;
        CHECK(strip_prop(in, 0, same) && same == in); // drop nothing: the bytes as they were
    }
    std::vector<uint8_t> out;
    std::vector<uint8_t> plain = captain_list(false, false);
    PropScan s;
    CHECK(strip_prop(plain, kPropNewer, out, &s) && !s.dropped && s.items);
    // bytes this code does not take: cut, a byte too many, type 0x11, flags 0x40, a huge count, a string past the end
    std::vector<uint8_t> cut(plain.begin(), plain.end() - 1), more = plain;
    more.push_back(0);
    CHECK(!strip_prop(cut, kPropNewer, out) && out.empty() && !strip_prop(more, kPropNewer, out));
    std::vector<uint8_t> bad;
    be32(bad, 1);
    prop_head(bad, 1, 0x11, 0);
    CHECK(!strip_prop(bad, 0, out));
    bad.clear();
    be32(bad, 1);
    prop_head(bad, 1, 0x09, 0x40);
    be32(bad, 0);
    CHECK(!strip_prop(bad, 0, out));
    bad.clear();
    be32(bad, 5000);
    CHECK(!strip_prop(bad, 0, out));
    bad.clear();
    be32(bad, 1);
    prop_head(bad, 1, 0x12, 0);
    be32(bad, 1000);
    CHECK(!strip_prop(bad, 0, out) && !strip_prop({}, 0, out));
    // BUG-1279: what a guest does with the host captain's list
    CHECK(host_prop(true, 7, false, 0, false, false) == HostProp::kRegister);
    CHECK(host_prop(true, 7, true, 7, true, true) == HostProp::kSame);
    CHECK(host_prop(true, 8, true, 7, true, false) == HostProp::kRegister); // a newer list of ours
    CHECK(host_prop(true, 7, false, 0, true, false) == HostProp::kSkipOwn);  // its own list of that key
    CHECK(host_prop(true, 7, false, 0, false, true) == HostProp::kSkipOwn);  // its own creation of that id (BUG-0278)
    CHECK(host_prop(false, 7, false, 0, false, false) == HostProp::kInvalid); // not a captain's list
    // the save guard
    CHECK(refuse_props_save(true, true, true, false, false));   // the host captain's list on a connected guest
    CHECK(!refuse_props_save(true, false, true, false, false)); // after the session: the game's
    CHECK(refuse_props_save(true, false, false, true, false));  // a creation of ours
    CHECK(!refuse_props_save(true, true, true, true, true));    // its own creation now under that id
    CHECK(!refuse_props_save(false, true, true, true, false));  // off: 0.7.16.1
    // BUG-1262: the puppet shows the copy only when baked here
    CHECK(puppet_shows_copy(true, true, 1) && !puppet_shows_copy(true, true, 0) && !puppet_shows_copy(true, true, -1));
    CHECK(!puppet_shows_copy(false, true, 1) && !puppet_shows_copy(true, false, 1));
    CHECK(captain_retry(0) == 2.0 && captain_retry(1) == 4.0 && captain_retry(3) == 16.0 && captain_retry(9) == 30.0);
    // BUG-1237: the swap waits for the bake, not longer than kWait, not without the fix
    CHECK(swap_waits(true, 0, 1.0) && swap_waits(true, -2, 3.0) && !swap_waits(true, 0, kWait));
    CHECK(!swap_waits(true, 1, 0.1) && !swap_waits(true, -1, 0.1) && !swap_waits(false, 0, 0.1));
    // BUG-1475: the own captain's steps (cur, mine, orig, want)
    CHECK(own_step(true, true, 6, 0x10, 0, 0x10, 0x20) == Own::kSet);
    CHECK(own_step(true, true, 4, 0x10, 0, 0x10, 0x20) == Own::kNone); // the world's own start first
    CHECK(own_step(false, true, 6, 0x10, 0, 0x10, 0x20) == Own::kNone && own_step(true, false, 6, 0x10, 0, 0x10, 0x20) == Own::kNone);
    CHECK(own_step(true, true, 6, 0x10, 0, 0x10, 0) == Own::kNone);     // nothing of its own
    CHECK(own_step(true, true, 6, 0, 0, 0, 0x20) == Own::kNone);        // no captain in the world: "Name Your Captain"
    CHECK(own_step(true, true, 6, 0x40, 0, 0, 0x20) == Own::kAdopt);    // the captain the player named there
    CHECK(own_step(true, true, 6, 0x20, 0x20, 0x10, 0x20) == Own::kHold);
    CHECK(own_step(true, true, 6, 0x10, 0x20, 0x10, 0x20) == Own::kSet); // the world's again: ours again
    CHECK(own_step(true, true, 6, 0, 0x20, 0x10, 0x20) == Own::kSet);
    CHECK(own_step(true, true, 6, 0x30, 0x20, 0x10, 0x20) == Own::kAdopt); // the outfitter's new captain
    CHECK(own_step(true, true, 6, 0x20, 0, 0x10, 0x20) == Own::kHold);
}

int main()
{
    ornament_fade_rules();
    arrival_wait();
    registry_bind();
    registry_liveness();
    registry_adopt();
    registry_sweep();
    registry_save_filter();
    snapshot_tribe_civ();
    snapshot_squadron();
    snapshot_handoff();
    world_ids();
    lifecycle_hooks();
    busy_classify();
    busy_codec();
    busy_long_wait();
    busy_announcer();
    busy_host_view();
    busy_rows();
    busy_pump();
    share_one_planet();
    captain_rules();
    if (g_fail) {
        printf("core_tests: %d FAILED\n", g_fail);
        return 1;
    }
    printf("core_tests: all passed\n");
    return 0;
}
