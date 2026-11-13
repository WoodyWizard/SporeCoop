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
#include <string>
#include <vector>

namespace lifecycle {

enum Scope : uint8_t { kSession = 1, kWorld = 2 };

struct Entry {
    const char* name;
    uint8_t scope; // kSession, kWorld or both
    bool* fix;     // the entry's own switch (null: only the registry's)
    void (*reset)();
    size_t (*held)();
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
    Reg(const char* name, uint8_t scope, bool* fix, void (*reset)(), size_t (*held)())
    {
        table().push_back({name, scope, fix, reset, held});
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

} // namespace lifecycle

#define LIFECYCLE_CAT2(a, b) a##b
#define LIFECYCLE_CAT(a, b) LIFECYCLE_CAT2(a, b)
#define LIFECYCLE_STATE(name, scope, fix, reset, held)                                                                \
    static lifecycle::Reg LIFECYCLE_CAT(g_lifecycle_state_, __LINE__)(name, scope, fix, reset, held)
#define SESSION_STATE(name, fix, reset, held) LIFECYCLE_STATE(name, lifecycle::kSession, fix, reset, held)
#define WORLD_STATE(name, fix, reset, held) LIFECYCLE_STATE(name, lifecycle::kWorld, fix, reset, held)
