// BUG-1055 (class: module state that outlives its session or its world): one registry of every module's state that
// belongs to a session or to a world, in the style of player_life.h (BUG-0422). Each module registers its globals
// next to their definition:
//
//   SESSION_STATE("tribe.ring", &g_fix_x, [] { ...clear... }, []() -> size_t { return ...how much is held...; });
//   WORLD_STATE(...)                         reset when the game's world changes (a load, another stage)
//   LIFECYCLE_STATE(name, lifecycle::kSession | lifecycle::kWorld, fix, reset, held)   both
//
// coop.cpp calls lifecycle::reset(kSession) on every session end (end_session block 3: a Leave, a drop, a new
// host / join, the unload) and lifecycle::reset(kWorld) once per frame when the stage counters of stage_epoch.h moved
// (a load, a received world, another stage; an editor visit is no new world). `fix` (may be null) is the entry's own
// coop.fix switch (one bug per switch, fixes.h R3): its reset runs only when that switch and the registry's
// (`core.lifecycle_reset`, the `on` argument) are on. Right after a reset the registry asks every entry of that scope
// what it still holds and keeps the list (`left`): `coop.lifecycle held` prints it with what is held now, so a test
// sees what a reset missed even when the next world fills the state again.
// Rules: a reset is mod state only (no game calls, no sends, no waiting: it may run inside g_sess.update()), and
// idempotent. Portable (no Windows, no SporeKit): tests/npc_tests.cpp runs it natively.
// docs/01-architecture.md#arch-lifecycle-registry.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace lifecycle {

enum Scope : uint8_t { kSession = 1, kWorld = 2 };

struct Entry {
    const char* name;
    uint8_t scope; // kSession, kWorld or both
    bool* fix;     // the entry's own switch (null: only the registry's)
    void (*reset)();
    size_t (*held)();
    uint32_t (*digest)(); // D2: a small digest of the entry's state (null: held() is its digest)
};

struct State {
    std::vector<Entry> table;
    std::string left[2]; // "<name> <n>\n" per entry still held right after the last reset of the scope
    uint32_t resets[2] = {};
};

inline State& state()
{
    static State s;
    return s;
}
inline std::vector<Entry>& table() { return state().table; }

struct Reg {
    Reg(const char* name, uint8_t scope, bool* fix, void (*reset)(), size_t (*held)(), uint32_t (*digest)() = nullptr)
    {
        table().push_back({name, scope, fix, reset, held, digest});
    }
};

inline int slot(Scope s) { return s == kSession ? 0 : 1; }

// "<name> <n>\n" for every entry of `scope` (0: all) that holds something now
inline std::string held(uint8_t scope = 0)
{
    std::string out;
    for (auto& e : table())
        if (!scope || (e.scope & scope))
            if (size_t n = e.held()) {
                out += e.name;
                out += ' ';
                out += std::to_string(n);
                out += '\n';
            }
    return out;
}

// every entry of scope `s` is reset (when `on` and its own switch are on); then what is still held is kept in left()
inline void reset(Scope s, bool on)
{
    for (auto& e : table())
        if ((e.scope & s) && on && (!e.fix || *e.fix))
            e.reset();
    state().left[slot(s)] = held(s);
    state().resets[slot(s)]++;
}

inline const std::string& left(Scope s) { return state().left[slot(s)]; }

// ---- D2 of docs/plan-0.7.15.md (audit TST-4): the warm-reset fingerprint ----
// Every entry reports a small digest of its state (its own digest(), else what held() counts; 0 = holds nothing).
// fingerprint() lists "<name> <digest hex>\n" for every entry whose digest is not 0 (registration order: the link
// order, the same in every game of one build) and its fnv-1a hash (`hash`): a fresh game holds nothing, and a game
// whose resets just ran must hold the same. The lab (tools/tests/twoinst.py) compares a warm game's fingerprint after
// `coop.testreset` (reset_all, then fingerprint) with a cold start's and starts the games again on a difference,
// naming the entries (docs/06-testing.md#testing-warm-fingerprint).
inline uint32_t digest_of(const Entry& e) { return e.digest ? e.digest() : (uint32_t)e.held(); }

struct Fingerprint {
    std::string lines; // "<name> <digest hex>\n" per entry with a digest
    uint32_t hash = 2166136261u;
    size_t entries = 0, holding = 0;
};

inline Fingerprint fingerprint()
{
    Fingerprint f;
    char b[24];
    for (auto& e : table()) {
        f.entries++;
        uint32_t d = digest_of(e);
        if (!d)
            continue;
        f.holding++;
        snprintf(b, sizeof b, " %08X\n", d);
        std::string line = std::string(e.name) + b;
        f.lines += line;
        for (char c : line)
            f.hash = (f.hash ^ (uint8_t)c) * 16777619u;
    }
    return f;
}

// both scopes reset (the registry's `on` and each entry's own switch as in reset()): what a fresh game holds
inline void reset_all(bool on)
{
    reset(kSession, on);
    reset(kWorld, on);
}
inline uint32_t resets(Scope s) { return state().resets[slot(s)]; }

// the world counter: the sum of the stage counters (stage_epoch.h); a change is a new world
struct WorldWatch {
    bool recorded = false;
    uint32_t sum = 0;
    // true when `sum` differs from the last call's (the first call only records: a hot reload)
    bool changed(uint32_t now)
    {
        bool c = recorded && now != sum;
        recorded = true;
        sum = now;
        return c;
    }
};

// ---- observers (C2 of docs/plan-0.7.15.md, audit CORE-3): one ordered fan-out for the events the modules call by
// hand today (end_session's three module lists, avatar_change_check's six calls, the player callbacks) ----
//
//   LIFECYCLE_HOOK("tribe.mirrors", lifecycle::kSessionEnd | lifecycle::kWorldChange, -20, on_world_change, true);
//
// fire(event, ctx, game_ok) runs every hook of the event in `order` (lower first; equal orders in registration
// order, which is the link order across files: give hooks that must follow each other different orders). The
// registry's own state resets of the event's scope (LIFECYCLE_STATE: kSessionEnd -> kSession, kWorldChange ->
// kWorld) run at order 0: hooks with order < 0 run before them (a despawn that reads the table), order >= 0 after.
// `needs_game`: the hook calls the game (destroys objects, plays animations). It runs only when fire() is called
// with game_ok (from on_frame, outside g_sess.update(): BUG-0029); otherwise it is queued with its ctx and run by
// pump_game() from on_frame, in its order. Its module keeps what it needs for that (its own table: the plain resets
// of the event may run first).
// Re-entrancy: a fire() from inside a hook is queued and runs after the current fan-out ends (no nested fan-out,
// FIFO). A hook that throws is counted (faults, last_fault) and the fan-out goes on with the next hook; a game fault
// (SEH) is not a C++ exception: the game glue may install a runner (set_runner) that calls the hook under its guard.
// Portable (no Windows, no SporeKit): tests/core_tests.cpp checks the order, the queue, re-entrancy and a throwing
// hook. docs/01-architecture.md#arch-world-id.
// Who fires: kSessionEnd session_glue.cpp end_session, kWorldChange coop.cpp world_id_tick (both game_ok), kPlayerJoin /
// kPlayerLeave session_glue.cpp's player callbacks through fire_player (BUG-1533; inside g_sess.update(): game_ok
// false, a needs_game hook waits for pump_game, which coop.cpp frame_receive calls right after the update).
enum Event : uint8_t { kSessionEnd = 1, kWorldChange = 2, kPlayerJoin = 4, kPlayerLeave = 8 };
constexpr uint8_t kCellToo = 1;

struct Ctx {
    uint8_t event = 0;
    uint8_t player = 0xFF;        // kPlayerJoin / kPlayerLeave: the id
    uint8_t path = 0;             // kSessionEnd: the caller's EndPath (coop_state.h), 0 if none
    uint8_t flags = 0;            // kSessionEnd: kCellToo (end_session's `cell`: the Cell modules end too)
    uint32_t from_world = 0;      // kWorldChange: world_id serial before / now (world_id.h)
    uint32_t to_world = 0;
    int8_t from_stage = -1, to_stage = -1;
    uint8_t why = 0;              // kWorldChange: world_id::Why (the log, coop.lifecycle)
};

using HookFn = void (*)(const Ctx&);
// calls fn(ctx); false when a fault was caught (counted as a fault of the hook)
using Runner = bool (*)(HookFn fn, const Ctx& ctx);

struct Hook {
    const char* name;
    uint8_t events;
    int order;
    HookFn fn;
    bool needs_game;
    uint32_t calls = 0, faults = 0;
};

struct Fanout {
    std::vector<Hook> hooks;
    std::vector<std::pair<size_t, Ctx>> game_queue; // needs_game hooks waiting for pump_game (hook index, ctx)
    std::vector<std::pair<Ctx, bool>> pending;      // fire() calls made inside a fan-out (ctx, game_ok)
    bool running = false;
    Runner runner = nullptr;
    bool state_resets = true; // the LIFECYCLE_STATE resets at order 0 (the `on` argument of reset())
    uint32_t fired[4] = {};
    uint32_t faults = 0, nested = 0, queued = 0;
    std::string last_fault;
};

inline Fanout& fanout()
{
    static Fanout f;
    return f;
}

struct HookReg {
    HookReg(const char* name, uint8_t events, int order, HookFn fn, bool needs_game)
    {
        fanout().hooks.push_back({name, events, order, fn, needs_game});
    }
};

inline void set_runner(Runner r) { fanout().runner = r; }

inline int event_slot(uint8_t e) { return e == kSessionEnd ? 0 : e == kWorldChange ? 1 : e == kPlayerJoin ? 2 : 3; }

inline void call_hook(size_t i, const Ctx& c)
{
    Fanout& f = fanout();
    Hook& h = f.hooks[i];
    h.calls++;
    bool ok = true;
    try {
        ok = f.runner ? f.runner(h.fn, c) : (h.fn(c), true);
    } catch (...) {
        ok = false;
    }
    if (!ok) {
        h.faults++;
        f.faults++;
        f.last_fault = h.name;
    }
}

// the hooks of `event`, sorted by order (stable: registration order for equal orders)
inline std::vector<size_t> hooks_of(uint8_t event)
{
    Fanout& f = fanout();
    std::vector<size_t> idx;
    for (size_t i = 0; i < f.hooks.size(); i++)
        if (f.hooks[i].events & event)
            idx.push_back(i);
    for (size_t a = 1; a < idx.size(); a++) // insertion sort: stable, the lists are short
        for (size_t b = a; b > 0 && f.hooks[idx[b]].order < f.hooks[idx[b - 1]].order; b--)
            std::swap(idx[b], idx[b - 1]);
    return idx;
}

inline void run_event(const Ctx& c, bool game_ok)
{
    Fanout& f = fanout();
    f.fired[event_slot(c.event)]++;
    uint8_t scope = c.event == kSessionEnd ? kSession : c.event == kWorldChange ? kWorld : 0;
    bool states_done = !scope;
    for (size_t i : hooks_of(c.event)) {
        if (!states_done && f.hooks[i].order >= 0) {
            reset((Scope)scope, f.state_resets);
            states_done = true;
        }
        if (f.hooks[i].needs_game && !game_ok) {
            f.game_queue.push_back({i, c});
            f.queued++;
            continue;
        }
        call_hook(i, c);
    }
    if (!states_done)
        reset((Scope)scope, f.state_resets);
}

// one event for every hook of it (see above). Inside a fan-out it is queued and runs after it.
inline void fire(const Ctx& c, bool game_ok)
{
    Fanout& f = fanout();
    if (f.running) {
        f.pending.push_back({c, game_ok});
        f.nested++;
        return;
    }
    f.running = true;
    run_event(c, game_ok);
    for (size_t k = 0; k < f.pending.size(); k++) { // (a hook may fire again: the list grows while it is read)
        auto p = f.pending[k];
        run_event(p.first, p.second);
    }
    f.pending.clear();
    f.running = false;
}

// BUG-1533: kPlayerJoin / kPlayerLeave were declared and listed but never fired (a hook on them was never called). The
// session glue's join / leave callbacks fire them for `id` (inside g_sess.update(): game_ok false). False when the
// switch is off (`coop.fix core.player_events off`, 0.7.16.1): nothing fired, the caller makes the hand calls.
inline bool fire_player(bool fix, uint8_t event, uint8_t id)
{
    if (!fix || (event != kPlayerJoin && event != kPlayerLeave))
        return false;
    Ctx c;
    c.event = event;
    c.player = id;
    fire(c, false);
    return true;
}

// on_frame (game calls allowed): the needs_game hooks queued by fire() calls made without game_ok, in the order
// they were queued (each event's hooks are in their order already)
inline void pump_game()
{
    Fanout& f = fanout();
    if (f.running || f.game_queue.empty())
        return;
    f.running = true;
    auto q = std::move(f.game_queue);
    f.game_queue.clear();
    for (auto& [i, c] : q)
        call_hook(i, c);
    f.running = false;
    if (!f.pending.empty()) { // fire() calls of those hooks
        auto p = std::move(f.pending);
        f.pending.clear();
        for (auto& [c, ok] : p)
            fire(c, ok);
    }
}

// "<order> <name> <events> [game] calls=N faults=M\n" per hook, in the order of `event` (0: every hook)
inline std::string hooks_status(uint8_t event = 0)
{
    Fanout& f = fanout();
    std::string out;
    std::vector<size_t> idx;
    if (event)
        idx = hooks_of(event);
    else
        for (size_t i = 0; i < f.hooks.size(); i++)
            idx.push_back(i);
    char b[160];
    for (size_t i : idx) {
        const Hook& h = f.hooks[i];
        snprintf(b, sizeof b, "%d %s %s%s%s%s%s calls=%u faults=%u\n", h.order, h.name, h.events & kSessionEnd ? "S" : "",
                 h.events & kWorldChange ? "W" : "", h.events & kPlayerJoin ? "J" : "", h.events & kPlayerLeave ? "L" : "",
                 h.needs_game ? " game" : "", h.calls, h.faults);
        out += b;
    }
    return out;
}

} // namespace lifecycle

#define LIFECYCLE_CAT2(a, b) a##b
#define LIFECYCLE_CAT(a, b) LIFECYCLE_CAT2(a, b)
#define LIFECYCLE_STATE(name, scope, fix, reset, held)                                                                \
    static lifecycle::Reg LIFECYCLE_CAT(g_lifecycle_state_, __LINE__)(name, scope, fix, reset, held)
// LIFECYCLE_STATE_D(name, scope, fix, reset, held, digest): with a digest of the state for the fingerprint (D2)
#define LIFECYCLE_STATE_D(name, scope, fix, reset, held, digest)                                                      \
    static lifecycle::Reg LIFECYCLE_CAT(g_lifecycle_state_, __LINE__)(name, scope, fix, reset, held, digest)
#define SESSION_STATE(name, fix, reset, held) LIFECYCLE_STATE(name, lifecycle::kSession, fix, reset, held)
#define WORLD_STATE(name, fix, reset, held) LIFECYCLE_STATE(name, lifecycle::kWorld, fix, reset, held)
// LIFECYCLE_HOOK(name, events, order, fn, needs_game): an observer of the events (see Fanout above)
#define LIFECYCLE_HOOK(name, events, order, fn, needs_game) \
    static lifecycle::HookReg LIFECYCLE_CAT(g_lifecycle_hook_, __LINE__)(name, events, order, fn, needs_game)
