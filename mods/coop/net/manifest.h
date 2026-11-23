// The tables generated from the message manifest (net/messages.def, B3 of 0.7.15, BUG-1136): direction, dispatch
// route and family of every top-level message type, the host's direction gate, and the family revisions (B6, BUG-1137).
// Portable: tests/validate_tests.cpp checks them against the tables of 0.7.14 (golden) and the sources.
#pragma once

#include <cstdint>

#include "protocol.h"

namespace net {

// who sends a type on the wire (messages.def `dir`); Unknown: not a type of this build (a newer peer)
enum class MsgDir : uint8_t { Unknown, Any, Host, Guest };
// which game's dispatch takes it (messages.def `recv`)
enum class MsgRecv : uint8_t { Both, Host, Client };
// the handler of session_glue.cpp's dispatch (messages.def `route`); None: net::Session handles it, or nobody
enum class Route : uint8_t {
    None, World, Diag, Anim, Settings, Colors, Report, HostWorld, Effect, Dna, DnaAdd, WorldTime, Vote, Notice, Role,
    Ready, Lobby, Shared, PingMark, Space, Squadron, Civ, Creation, Cell, Tribe, Npc, Social, Nest, Fields, Wsync, Parts,
    Wobj, PlayerRule, MirrorFx, HostBusy, SpeciesLine,
};
// the world-state class (host_world::MsgClass has the same values and meaning, BUG-0420)
enum class MsgCls : uint8_t { Session, HostWorld, Module, Unknown };

// message families (messages.def FAMILY): ids as on the wire
enum class Family : uint8_t {
    None = 0,
#define FAMILY(name, id, rev, minor, stages, title, files) name = id,
#include "messages.def"
};

// stage bits of a family's `stages`
constexpr uint32_t STAGE_CELL = 1, STAGE_CREATURE = 2, STAGE_TRIBE = 4, STAGE_CIV = 8, STAGE_SPACE = 16,
                   STAGE_ALWAYS = 0x80000000u;

struct MsgInfo {
    bool known = false;
    const char* name = "?";
    MsgDir dir = MsgDir::Unknown;
    MsgCls cls = MsgCls::Unknown;
    Route route = Route::None;
    MsgRecv recv = MsgRecv::Both;
    Family family = Family::None;
};

inline MsgInfo msg_info(uint8_t type)
{
    switch (type) { // a duplicate id in messages.def is a duplicate case label: no build
#define MSG(id, name, chan, dir, cls, route, recv, family, what) \
    case id: return {true, #name, MsgDir::dir, MsgCls::cls, Route::route, MsgRecv::recv, Family::family};
#include "messages.def"
    }
    return {};
}
inline MsgDir msg_dir(uint8_t type) { return msg_info(type).dir; }
inline const char* msg_name(uint8_t type) { return msg_info(type).name; }

// the route session_glue.cpp's dispatch calls for a type on this game (Route::None: nothing; a type this build does not
// know, a type net::Session handles, or a type for the other role: in 0.7.14 such a type fell to the end of the
// hand-written chain, where world::on_message ignored it)
inline Route dispatch_route(uint8_t type, bool is_host)
{
    MsgInfo m = msg_info(type);
    if (m.recv == MsgRecv::Host && !is_host)
        return Route::None;
    if (m.recv == MsgRecv::Client && is_host)
        return Route::None;
    return m.route;
}

// BUG-1136 (the host's direction gate, `coop.fix net.dir_gate`): a message that only the host sends (dir Host) arriving
// at the host from a guest is dropped before any handler and counted. Every handler ignored these already or should
// have; the gate makes it one rule instead of one check per handler. Types this build does not know pass (a newer
// guest's addition; the handlers ignore them)
inline bool dir_gate_drops(uint8_t type, bool gate = true) // not a lab switch: the caller's (net::g_fix.dir_gate)
{
    return gate && msg_dir(type) == MsgDir::Host;
}

// ---- families (B6) ----
struct FamilyInfo {
    Family id = Family::None;
    const char* name = "?";
    uint16_t rev = 0, minor = 0;
    uint32_t stages = 0;
    const char* title = "?";
};
constexpr FamilyInfo kFamilies[] = {
#define FAMILY(name, id, rev, minor, stages, title, files) {Family::name, #name, rev, minor, stages, title},
#include "messages.def"
};
constexpr int kFamilyCount = (int)(sizeof kFamilies / sizeof kFamilies[0]);
inline const FamilyInfo* family_info(uint8_t id)
{
    for (auto& f : kFamilies)
        if ((uint8_t)f.id == id)
            return &f;
    return nullptr;
}

// ---- ids reserved for the work packages of 0.7.17 (WP-0 of docs/plan-0.7.17.md) ----
// Parallel branches took the same id before (BUG-1288's branch and PR #205 both took 74). A WP adds its MSG row to
// messages.def with exactly this id and name, in this family (Family::None: the WP picks it); every other new type
// takes the next free id after 78. tests/validate_tests.cpp test_manifest: a reserved id is unknown to this build, or
// known under its reserved name (and family). Declared only: nothing sends or handles these ids yet.
struct ReservedMsg {
    uint8_t id;
    const char* name;
    Family family;
    const char* wp;
};
constexpr ReservedMsg kReservedMsgs[] = {
    {74, "CIV_WALLET", Family::civ, "WP-M1 own Civ money (feat/w68-wallet): report, credit, charge, kept, end"},
    {75, "PLAYER_HISTORY", Family::session, "WP-H1 own stage history (feat/w70-history): cards, trait progress, archetype"},
    {76, "PROGRESS", Family::None, "WP-P1/P2 own progress (feat/w69-progress): gain, values, bar, held, level line; "
                                   "creature 1.3 and cell 1.2 gate it, the row's family is the WP's choice"},
    {77, "RESCUE", Family::creature, "WP-R1 revive (feat/w71-revive): K_DOWN, K_RESULT, K_LINES"},
    {78, "UFO_LEAD", Family::space, "WP-S1 fleets (fix/w73-fleet, BUG-1288; the agy branch's 74 moves here)"},
};
inline const ReservedMsg* reserved_msg(uint8_t id)
{
    for (auto& r : kReservedMsgs)
        if (r.id == id)
            return &r;
    return nullptr;
}

} // namespace net
