// BUG-1047 (class: a module's re-entrancy flag around a game call stays set after a fault the game catches): one
// registry of every module's call guard, reset once a frame outside every game call (the rule of BUG-0882's
// reset_call_guards: these guards are false / 0 outside game calls).
//
// A module registers its guard next to its definition and holds it around the game call:
//
//   static bool g_applying;                          // replaying a partner's change: the hooks send nothing
//   CALL_GUARD("squadron.applying", g_applying);
//   ...
//   {
//       callguard::Hold applying(g_applying);         // true here, false after (as `flag = true; ...; flag = false;`)
//       o_TakeDamage(...);
//   }
//
//   static int g_depth;                              // a hook's depth counter (inside the game's own call)
//   CALL_GUARD_DEPTH("space_diplomacy.depth", g_depth);
//   { callguard::Depth d(g_depth); o_ApplyEv(...); }
//
// A fault inside the game call that the game's own handler (or SporeKit's) catches unwinds past our frame without
// running the destructor (the game's SEH unwind does not run C++ destructors of the mod's frames): the flag stays set
// and the module's relay or filter was off for the rest of the session. game.cpp's reset_call_guards (the frame's
// swap guard tick, outside every game call) calls callguard::reset(), which clears every registered guard that is
// still set and counts it. Not registered: guards that are meant to stay set across frames, thread_local guards (a
// static initializer would register the loader thread's copy) and guards of other threads (adapters.cpp's worker).
// Tests: unwind(name) makes the next Hold / Depth of that guard leave it as a caught fault does (`coop.guard unwind`).
// Portable (no Windows, no SporeKit): tests/npc_tests.cpp runs it natively. docs/06-testing.md#testing-call-guards.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace callguard {

struct Entry {
    const char* name;
    bool* flag;       // a bool guard, or
    int* depth;       // a depth counter
    uint32_t unwind;  // tests: the next n holds leave the guard set
    uint32_t cleared; // resets that found it set
};

struct State {
    std::vector<Entry> table;
    uint32_t armed = 0;   // sum of the entries' unwind (the holds' fast path)
    uint32_t cleared = 0; // guards found set by reset() since the start
    std::string last;     // names the last reset that cleared anything cleared
};

inline State& state()
{
    static State s;
    return s;
}
inline std::vector<Entry>& table() { return state().table; }

struct Reg {
    Reg(const char* name, bool* flag) { table().push_back({name, flag, nullptr, 0, 0}); }
    Reg(const char* name, int* depth) { table().push_back({name, nullptr, depth, 0, 0}); }
};

inline Entry* find(const char* name)
{
    for (auto& e : table())
        if (!strcmp(e.name, name))
            return &e;
    return nullptr;
}

// a hold's end: true when a test asked this guard to be left as a caught fault leaves it
inline bool unwound(const void* guard)
{
    State& s = state();
    if (!s.armed)
        return false;
    for (auto& e : s.table)
        if ((e.flag == guard || e.depth == guard) && e.unwind) {
            e.unwind--;
            s.armed--;
            return true;
        }
    return false;
}

// `flag` is `on` (true by default) for the life of the hold and false after it, as the bare set / clear was
struct Hold {
    bool* f;
    explicit Hold(bool& flag, bool on = true) : f(&flag) { flag = on; }
    ~Hold()
    {
        if (!unwound(f))
            *f = false;
    }
    Hold(const Hold&) = delete;
    Hold& operator=(const Hold&) = delete;
};

// a depth counter one deeper for the life of the hold
struct Depth {
    int* d;
    explicit Depth(int& depth) : d(&depth) { depth++; }
    ~Depth()
    {
        if (!unwound(d))
            (*d)--;
    }
    Depth(const Depth&) = delete;
    Depth& operator=(const Depth&) = delete;
};

inline bool set(const Entry& e) { return e.flag ? *e.flag : *e.depth != 0; }

// once a frame, outside every game call: every registered guard still set is a caught fault's leftover and is cleared
// (`on`: the registry's switch, core.call_guard_reset). Returns how many were set.
inline size_t reset(bool on)
{
    if (!on)
        return 0;
    size_t n = 0;
    std::string names;
    for (auto& e : table())
        if (set(e)) {
            if (e.flag)
                *e.flag = false;
            else
                *e.depth = 0;
            e.cleared++;
            n++;
            names += names.empty() ? "" : ",";
            names += e.name;
        }
    if (n) {
        state().cleared += (uint32_t)n;
        state().last = names;
    }
    return n;
}

// tests: the next `n` holds of the guard `name` end as a caught fault ends them (the guard stays set)
inline bool unwind(const char* name, uint32_t n = 1)
{
    Entry* e = find(name);
    if (!e)
        return false;
    e->unwind += n;
    state().armed += n;
    return true;
}

// "<name> set=<0|1> cleared=<n>[ unwind=<n>]\n" per entry
inline std::string status()
{
    std::string out;
    for (auto& e : table()) {
        out += e.name;
        out += set(e) ? " set=1" : " set=0";
        out += " cleared=" + std::to_string(e.cleared);
        if (e.unwind)
            out += " unwind=" + std::to_string(e.unwind);
        out += '\n';
    }
    return out;
}

} // namespace callguard

#define CALL_GUARD_CAT2(a, b) a##b
#define CALL_GUARD_CAT(a, b) CALL_GUARD_CAT2(a, b)
// one line next to the guard's definition (namespace scope); a bool registers a flag, an int a depth counter
#define CALL_GUARD(name, var) static callguard::Reg CALL_GUARD_CAT(g_call_guard_reg_, __LINE__)(name, &(var))
#define CALL_GUARD_DEPTH(name, var) CALL_GUARD(name, var)
