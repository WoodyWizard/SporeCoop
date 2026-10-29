// Which world of a stage the game plays, as counters (BUG-0217, BUG-0223). Portable (no Windows,
// no SporeKit): tests/npc_tests.cpp runs it natively; game.cpp keeps one State and steps it once
// per frame (game::stage_epoch_tick).
//
// One counter per stage mode (Cell, Creature, Tribe, Civ, Space), all 0 at first. `open` is the
// stage whose world is open. Each frame with mode m:
//   * m is stage S: if open != S, count[S] goes up and open = S;
//   * m is in keep(open): nothing changes. keep(S) = kEdit, kEditor for every stage, plus
//     kScenario for Space;
//   * anything else (kLoad, the galaxy menu, an unknown mode): open = none.
// So "editor, then Done" keeps the counter; "editor, then menu, then load", a load and another
// stage raise it. A reload that never shows a non-stage mode to a frame is NOT seen: callers add
// their own in-stage signals (Watch below).
#pragma once

#include <cstdint>

namespace stage_epoch {

// the game's mode ids (modes.h, which is not portable; game.cpp static_asserts they agree)
constexpr uint32_t kCell = 0x1654C00, kCreature = 0x1654C01, kTribe = 0x1654C02, kCiv = 0x1654C04,
                   kSpace = 0x1654C05, kEdit = 0x1654C06, kScenario = 0x1654C10, kEditor = 0x00DBDBA1;
constexpr int kStages = 5;

inline int index_of(uint32_t m)
{
    switch (m) {
    case kCell: return 0;
    case kCreature: return 1;
    case kTribe: return 2;
    case kCiv: return 3;
    case kSpace: return 4;
    default: return -1;
    }
}

inline bool keeps(int open, uint32_t m)
{
    return open >= 0 && (m == kEdit || m == kEditor || (m == kScenario && open == 4));
}

struct State {
    uint32_t count[kStages] = {};
    int open = -1;
};

inline void step(State& s, uint32_t m)
{
    int i = index_of(m);
    if (i >= 0) {
        if (s.open != i) {
            s.count[i]++;
            s.open = i;
        }
    } else if (!keeps(s.open, m))
        s.open = -1;
}

inline uint32_t get(const State& s, uint32_t stage_mode)
{
    int i = index_of(stage_mode);
    return i >= 0 ? s.count[i] : 0;
}

// A module's view of "my stage's world changed": the stage counter plus in-stage signals (the
// player's tribe / civilization object and the save name), which also count while the mode stays
// the stage. The first call only records (a hot reload restarts the counters).
// Limit: a reload of the same save that reuses the object's address and never shows a non-stage
// mode to a frame is not seen.
struct Watch {
    bool recorded = false;
    uint32_t epoch = 0;
    void* obj = nullptr; // last non-null object seen in the stage
    uint32_t world = 0;  // last save name crc seen in the stage (0: none yet)
};

// epoch: stage_epoch(stage); in_stage: the mode is the stage itself (not an editor); obj: the
// player's tribe / civ now (may be null); world: modes::current_world() now. True: a new world.
inline bool changed(Watch& w, uint32_t epoch, bool in_stage, void* obj, uint32_t world)
{
    if (!w.recorded) {
        w = Watch{true, epoch, in_stage ? obj : nullptr, in_stage ? world : 0};
        return false;
    }
    bool ch = epoch != w.epoch;
    w.epoch = epoch;
    if (ch) { // a new world of the stage: start the in-stage signals again
        w.obj = in_stage ? obj : nullptr;
        w.world = in_stage ? world : 0;
        return true;
    }
    if (!in_stage) // editors, menus: the in-stage signals wait
        return false;
    if (w.obj && obj != w.obj)
        ch = true;
    if (w.world && world && world != w.world)
        ch = true;
    if (obj)
        w.obj = obj;
    else if (ch)
        w.obj = nullptr;
    if (world)
        w.world = world;
    return ch;
}

} // namespace stage_epoch
