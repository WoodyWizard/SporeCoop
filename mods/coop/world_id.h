// C2 of docs/plan-0.7.15.md (audit STG-3, CORE-3; umbrella BUG-0427 "one verified world-identity service"): one
// answer to "which world does this game play, and did it change?", in place of the three detectors that fire today
// (coop.cpp avatar_change_check: the local entity's pointer changed, editors included; tribe.cpp / civ.cpp
// world_check: stage_epoch.h's Watch; coop.cpp lifecycle_world_tick: the sum of the stage counters) and the two
// counters next to them (game::world_epoch for Creature, BUG-0119; spaceext::world_epoch for Space, BUG-0225).
//
// Owner decision 2 (2026-11-14): an editor visit is the host being away (C3, busy_model.h), NOT a world change: the
// mirrors stay, and an object the game rebuilt meanwhile is caught per object by C1's liveness (mirror_registry.h).
// A world change is only a change of the world id: another stage, another save (planet file), or the same stage
// entered again (a load, a received world, a reload).
//
// What the game exposes, and what this header takes from it each frame (the game glue fills a Frame; RE facts in
// docs/re/tribe.md#tribe-stage-epoch, #tribe-snapshot-name, docs/re/creature.md#creature-world-epoch,
// docs/re/space.md#space-object-serial and the section of this change in docs/re/host-centric.md):
//   mode     game::mode_id() (the mode manager's current mode id; stage_epoch.h's ids)
//   count    game::stage_epoch(mode) when mode is a stage (stage_epoch.h: +1 when the stage is entered from anything
//            but its own editors kEdit / kEditor (and kScenario for Space); the galaxy menu and kLoad close it)
//   save     crc32 of the planet file the game plays (modes::current_world on top of world_name.h: the last real
//            "<planet>.spo" while the game says snapshot.spo, BUG-0258), 0 when not known ("SaveGame" in menus)
//   anchor   the stage's own root object (Tribe: the player tribe, Civ: the player civilization, Space: the player
//            cEmpire), null when not read. Creature and Cell pass null: the avatar's address is no identity (the
//            game pools creatures, BUG-0119) and whether an editor visit gives the avatar another object is open.
// The rule (step):
//   * mode is a stage S: the world is open. It is a new world when the previous frame had no open world, another
//     stage, another `count`, or, within the same stage, `save` went from one known value to another, or `anchor`
//     from one non-null object to another (the in-stage signals of stage_epoch.h Watch: a reload that shows no
//     non-stage frame);
//   * mode keeps the open stage (its editors; kScenario in Space): nothing changes (decision 2);
//   * any other mode (menu, load, unknown): the world is closed (a change to "none" when one was open).
// The first step only records (a hot reload starts the counters again): no change is reported for it.
// Id::serial counts the worlds of this process (+1 per change, never reused): two equal serials are the same world.
// Across machines a world is named by {stage mode, save} (the host's PlayerState `world`, MSG_HOST_WORLD); serials
// are never compared between machines.
// Portable (no Windows, no SporeKit): tests/core_tests.cpp checks it natively. docs/01-architecture.md#arch-world-id.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "stage_epoch.h"

namespace world_id {

struct Frame {
    uint32_t mode = 0;
    uint32_t count = 0;          // stage_epoch of the mode's stage (0 outside a stage)
    uint32_t save = 0;           // crc of the planet file, 0 unknown
    const void* anchor = nullptr; // the stage's root object, null unknown / not used
};

struct Id {
    int8_t stage = -1;   // stage_epoch::index_of (0 Cell .. 4 Space), -1: no world open
    uint32_t serial = 0; // this process's world number (0: none yet)
    uint32_t mode = 0;   // the stage's mode id (0: none)
    uint32_t save = 0;   // the save it was opened with (updated while it was 0)
    bool open() const { return stage >= 0; }
    bool operator==(const Id& o) const { return stage == o.stage && serial == o.serial; }
    bool operator!=(const Id& o) const { return !(*this == o); }
};

// why step() reported a change (coop.lifecycle, the log)
enum class Why : uint8_t { None, Opened, Closed, Stage, Count, Save, Anchor };
inline const char* why_name(Why w)
{
    switch (w) {
    case Why::None: return "none";
    case Why::Opened: return "opened";
    case Why::Closed: return "closed";
    case Why::Stage: return "another stage";
    case Why::Count: return "the stage entered again";
    case Why::Save: return "another save";
    case Why::Anchor: return "another root object";
    }
    return "?";
}

struct Tracker {
    Id cur;
    bool recorded = false;
    uint32_t count = 0;           // the stage counter of cur
    const void* anchor = nullptr; // the last non-null anchor of cur
    uint32_t serial_next = 1;
    uint32_t changes = 0;
    Why last_why = Why::None;

    // one frame; true when the world changed (then *from is the world before, cur the world now)
    bool step(const Frame& f, Id* from = nullptr, Why* why = nullptr)
    {
        int s = stage_epoch::index_of(f.mode);
        Why w = Why::None;
        Id before = cur;
        if (s < 0) {
            if (cur.open() && stage_epoch::keeps(cur.stage, f.mode)) {
                // an editor of the open stage (or Space's scenario): the same world (decision 2)
            } else if (cur.open() || !recorded) {
                if (recorded && cur.open())
                    w = Why::Closed;
                cur = Id{};
                count = 0;
                anchor = nullptr;
            }
        } else if (!cur.open() || cur.stage != s || f.count != count) {
            w = !cur.open() ? Why::Opened : cur.stage != s ? Why::Stage : Why::Count;
            open(s, f);
        } else {
            if (cur.save && f.save && f.save != cur.save)
                w = Why::Save;
            else if (anchor && f.anchor && f.anchor != anchor)
                w = Why::Anchor;
            if (w != Why::None)
                open(s, f);
            else {
                if (!cur.save && f.save)
                    cur.save = f.save;
                if (f.anchor)
                    anchor = f.anchor;
            }
        }
        if (!recorded) { // the first frame only records
            recorded = true;
            return false;
        }
        if (w == Why::None)
            return false;
        changes++;
        last_why = w;
        if (from)
            *from = before;
        if (why)
            *why = w;
        return true;
    }
    // "world <serial> stage <i> mode <m> save <crc> (changes N, last: <why>)" for coop.lifecycle
    std::string status() const
    {
        char b[160];
        snprintf(b, sizeof b, "world %u stage %d mode %08X save %08X (changes %u, last: %s)", cur.serial, (int)cur.stage,
                 cur.mode, cur.save, changes, why_name(last_why));
        return b;
    }

private:
    void open(int s, const Frame& f)
    {
        cur.stage = (int8_t)s;
        cur.serial = serial_next++;
        cur.mode = f.mode;
        cur.save = f.save;
        count = f.count;
        anchor = f.anchor;
    }
};

// the game's one tracker (coop.cpp frame_observe steps it; the modules read cur)
inline Tracker& tracker()
{
    static Tracker t;
    return t;
}

} // namespace world_id
