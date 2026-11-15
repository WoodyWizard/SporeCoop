// A14 (CORE-6, BUG-1130): the game calls the mod makes off the game's main thread. Everything that touches game
// objects runs on the main thread (docs/01-architecture.md#arch-threads); a game call through the skg helpers
// (sporekit_game.hpp: thiscall, cdecl_call, stdcall_call, vcall) from any other thread is a bug. game.cpp installs
// skg::call_probe, which asks game::on_main() and, off the main thread, notes the call here: per call site (the
// return address in coop.dll) and game function, a count, the first time a log line. `coop.threads` prints it.
// Portable (no Windows, no SporeKit): tests/npc_tests.cpp runs it natively. The caller locks (several threads).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace offmain {

struct Site {
    uintptr_t from;     // the mod's call site (the return address of the probe call)
    uintptr_t fn;       // the game function called (its address, or the vtable slot's target)
    uint32_t n;         // calls seen
    unsigned long tid;  // the thread of the first one
};

struct Table {
    static constexpr size_t kMax = 64;
    Site sites[kMax] = {};
    size_t used = 0;
    uint64_t total = 0;   // every off-main game call
    uint64_t dropped = 0; // calls of sites past kMax (counted in total only)

    // one off-main call: true the first time for this (from, fn), the caller logs it then
    bool note(uintptr_t from, uintptr_t fn, unsigned long tid)
    {
        total++;
        for (size_t i = 0; i < used; i++)
            if (sites[i].from == from && sites[i].fn == fn) {
                sites[i].n++;
                return false;
            }
        if (used == kMax) {
            dropped++;
            return false;
        }
        sites[used++] = {from, fn, 1, tid};
        return true;
    }

    void clear() { *this = Table{}; }

    // "off_main calls=<total> sites=<used>[ dropped=<n>]\n" then one line per site; `base` is subtracted from
    // `from` (coop.dll's load address: the line reads coop.dll+0x..., as crash_triage symbolises it)
    std::string status(uintptr_t base) const
    {
        char b[160];
        snprintf(b, sizeof b, "off_main calls=%llu sites=%zu", (unsigned long long)total, used);
        std::string out = b;
        if (dropped) {
            snprintf(b, sizeof b, " dropped=%llu", (unsigned long long)dropped);
            out += b;
        }
        out += '\n';
        for (size_t i = 0; i < used; i++) {
            const Site& s = sites[i];
            snprintf(b, sizeof b, "  coop.dll+0x%llX -> game 0x%llX calls=%u first_thread=%lu\n",
                     (unsigned long long)(s.from - base), (unsigned long long)s.fn, s.n, s.tid);
            out += b;
        }
        return out;
    }
};

} // namespace offmain
