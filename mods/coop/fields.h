// Replicated state fields of mirrored objects (docs/sync-architecture-plan.md L2, class G2 BUG-0419,
// docs/sync-architecture-plan.md#sync-fields).
//
// Before: every module sent the state it thought of and the receiver applied some of it (NpcSpawn.scale
// was sent and never read: BUG-0461). Now a module that mirrors a kind of object registers ONE row: how
// the owner (the host) lists its objects with their net ids, how a receiver finds its mirror of an id,
// and the kind's fields, each with a `read` from the game and an `apply` into the game. The engine is
// stage-free: the host sends every field of every object (deltas, a full refresh every few seconds),
// a guest keeps the newest value per (kind, id, field) and applies it to its mirror whenever the
// mirror's own value differs (a mirror made later, a game that resets it). A field without an apply
// cannot be registered, so "sent but never applied" cannot happen for a registered kind.
//
// Wire (MSG_FIELDS, unreliable, host -> guest): u8 kind, u8 nf, nf x u8 field id, u16 n,
// n x {u16 id, nf x f32}. Older SporeCoop ignores the type.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "net/session.h"
#include "sporekit.h"

namespace fields {

enum FieldId : uint8_t {
    F_SCALE = 1, // cSpatialObject GetScale (vt 0x34) / SetScale (vt 0x40)
    F_TOOL = 2,  // cCreatureCitizen +0x102C mSpecializedTool / SetSpecializedTool (tribe.cpp, BUG-0514)
    F_LEADER = 3, // 2 = the member leads its tribe (cCommunity +0x11C), 1 = not; cTribe::SetChieftain (tribe.cpp, BUG-0652)
    F_MAX_HP = 4, // cCombatant vt 0x58 the maximum health; on a proxy through creature_hp's GetMaxHp hook (npc.cpp, BUG-0710)
    F_ITEM_SCALE = 6, // the scale of the model in a tribe member's hand (the model's transform; tribe.cpp, BUG-0912)
    F_HELD_ITEM = 5, // cCreatureCitizen +0xFFC mCurrentHandheldItem + 1 / the game's attach 0xC250A0 (tribe.cpp, BUG-0906)
    F_HUNGER = 7,    // cCreatureBase +0xBBC mHunger (0..100) / its clamped setter 0xC0BB30 (tribe.cpp, BUG-0935)
};

struct Field {
    uint8_t id;
    const char* name;
    float (*read)(void* obj);
    void (*apply)(void* obj, float v);
    float tol; // relative: |mirror - wanted| > tol * |wanted| is a difference
};

using EachFn = void (*)(void (*cb)(uint16_t id, void* obj, void* u), void* u);

struct Kind {
    uint8_t id;              // on the wire, unique
    const char* name;
    const bool* on;          // the kind's fix switch (COOP_FIX in the module that owns the kind)
    const Field* fields;     // every field of the kind
    int nfields;
    EachFn each;             // host: every object of the kind it owns, with its net id
    void* (*mirror)(uint16_t id); // guest: its mirror object of the host's id (nullptr: none yet)
    bool (*active)();        // this kind is in play now (its stage); nullptr: always
};

enum KindId : uint8_t {
    K_NPC = 1,          // creature NPCs (npc.cpp: Creature and Tribe animals)
    K_TRIBE_MEMBER = 2, // tribe members of every tribe (tribe.cpp)
    K_UFO = 3,          // NPC ships of the host's place (spacecoop.cpp)
    K_CIV_CITIZEN = 4,  // Civ city citizens bound by the world-object kind `citizen` (worldobj.cpp, BUG-0475 / BUG-0476)
    K_TRIBE_TOOL = 5,   // tribe members' held tools (tribe.cpp, BUG-0514: its own switch tribe.tool_hold)
    K_SPACE_CITIZEN = 6, // Space: colony citizens bound by the world-object kind `space_citizen` (worldobj.cpp, BUG-0490)
    K_SPACE_VEHICLE = 7, // Space: colony vehicles bound by the world-object kind `space_vehicle` (worldobj.cpp, BUG-0490)
    K_CIV_VEHICLE = 8,   // Civ vehicles: the host's by net id, a guest's mirrors (civ.cpp, BUG-0549)
    K_TRIBE_ITEM_SCALE = 10, // the size of what a tribe member holds (tribe.cpp, BUG-0912: its own switch tribe.item_scale)
    K_TRIBE_ITEM = 9,    // what a tribe member holds in its hand (tribe.cpp, BUG-0906: its own switch tribe.held_item)
    K_TRIBE_HUNGER = 11, // a tribe member's hunger (tribe.cpp, BUG-0935: its own switch tribe.mirror_hunger)
};

struct Ctx {
    net::Session* sess = nullptr;
    std::function<bool(uint8_t id)> in_place; // host: guest `id` plays our world
};

// generic reads / applies
float read_scale(void* obj);
void apply_scale(void* obj, float v);
extern const Field kScale;

void init(const SkHost* host, SkMod* mod, const Ctx& ctx);
bool add_kind(const Kind& k); // false: refused (no apply, duplicate id)
void tick(double now);         // every frame
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_world_change();
void on_session_end();
std::string status();
// tests: `obj <kind> <id> <field>=<value> ...` per object (host: its own; guest: mirror and host value)
std::string list(const char* kind_name);

// tests: how many applies a kind had on this machine, and differences left after the last check
struct Stats {
    uint32_t sent = 0, received = 0, applied = 0, reapplied = 0, pending = 0, checked = 0, differ = 0;
};
Stats stats(uint8_t kind);

} // namespace fields
