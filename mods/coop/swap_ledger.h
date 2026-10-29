// A ledger of game state that a detour changes around a call into the game and puts back when the
// call returns (BUG-0110). The game's own handler may catch a fault inside that call and unwind
// past our frame: the put-back never runs. Each such change is opened here with its undo before
// the call and closed after it; a change still open where no detour runs (once per frame) is a
// leftover and is undone then. Portable (unit test: tests/npc_tests.cpp).
#pragma once

#include <cstddef>
#include <cstdint>

namespace swapledger {

using Undo = void (*)(void* p, uintptr_t v);

struct Entry {
    Undo undo;
    void* p;
    uintptr_t v;
    const char* what;
};

template <size_t N> struct Ledger {
    Entry e[N];
    size_t n = 0;
    bool guard = true;  // tests: off = leftovers stay; not a lab switch: game.cpp copies npc.swapguard into it
    bool fault = false; // tests: the next close is skipped, as after an unwind
    unsigned restored = 0;
    const char* last = nullptr;

    // before the call: returns the mark to close with (the entry is not kept when the ledger is
    // full: the change is then made without a guard, as before)
    size_t open(Undo u, void* p, uintptr_t v, const char* what)
    {
        size_t mark = n;
        if (n < N)
            e[n++] = {u, p, v, what};
        return mark;
    }
    // after the call returned: the `own` entries from the mark on are the caller's (dropped: it
    // restores them itself), entries above them are leftovers of an inner call that did not return
    // (undone now)
    void close(size_t mark, size_t own = 1)
    {
        if (fault) {
            fault = false;
            return;
        }
        if (mark > n)
            return; // already undone by a tick (cannot happen inside a call; defensive)
        if (n > mark + own) {
            restored += (unsigned)(n - mark - own);
            undo_above(mark + own);
        }
        n = mark;
    }
    // once per frame, outside every detour: everything still open is a leftover
    unsigned tick()
    {
        if (!guard || !n)
            return 0;
        unsigned k = (unsigned)n;
        undo_above(0);
        n = 0;
        restored += k;
        return k;
    }

  private:
    void undo_above(size_t from)
    {
        while (n > from) {
            Entry& x = e[--n];
            last = x.what;
            if (x.undo)
                x.undo(x.p, x.v);
        }
    }
};

} // namespace swapledger
