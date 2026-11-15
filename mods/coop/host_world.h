// "Is the host's world the one I play?" (BUG-0231, GitHub #165): the one rule every module asks
// before it applies anything of the host's world on a guest, or sends anything of its own world to
// the host. Kept apart from the game so `make test` can check it (tests/validate_tests.cpp).
//
// The perspective of one machine: each guest classifies the host's latest state against its own
// game (a 3-4 player session can have one guest elsewhere and another in the host's world). The
// same question is the base of the per-player perspective of a rivals session (docs/11-rivals.md
// 11.8): "whose world is this machine in" is answered here, the modules decide what to share.
//
// World identity is the CRC of the save name (`CurrentGameName`). Two different planets saved
// under the same name count as one world: a known limit, fixing it would change the protocol.
//
// Callers (coop.cpp `host_world()`, `guest_elsewhere()`, `host_absent_for()`):
//   * the message gate (world_state_msg): the host's Cell / Creature / Tribe / Civ state is dropped
//     while Other;
//   * the npc, tribe and civ contexts: their "host away" is true while Other (host_absent_for);
//   * the DNA routes (dna_route.h rows 2 and 6);
//   * the shared resources: held while Other (shared.cpp, res.elsewhere).
#pragma once

#include <cstdint>

#include "net/manifest.h"
#include "net/protocol.h"

namespace host_world {

// the game mode ids (modes.h; static_assert there keeps them equal)
constexpr uint32_t kCell = 0x1654C00, kCreature = 0x1654C01, kTribe = 0x1654C02, kCiv = 0x1654C04;

enum class HostWorld {
    NotGated, // not a connected guest, a stage that decides per place (Space, adventures, menus,
              // editors, loads), or the host plays another stage (follow_phase pulls the guest)
    Same,     // the host's world is the one this guest plays
    Other,    // this guest plays another world of the host's stage (or cannot tell yet)
};

// the stages whose world is one save: Space (star systems) and adventures decide per place
// (squadron with_host, spacecoop my_place), they are never gated here
inline bool gated(uint32_t mode) { return mode == kCell || mode == kCreature || mode == kTribe || mode == kCiv; }

// my_mode / my_world: this frame's g_mode / g_world (save CRC; 0 = unknown);
// has_state / st_mode / st_world: the host's latest PlayerState, if any;
// welcome_world: Welcome::world of the host (0 = unknown or a 0.7.3.0 or older host).
// Recomputed on every call, never cached. Semantics callers rely on:
//   * NotGated when not a connected guest, when my_mode is not Cell, Creature, Tribe or Civ, or
//     when the host's state is in another mode;
//   * Same when the host's state (before the first one: welcome_world) names my_world != 0;
//   * Other otherwise in a gated stage: no state and no welcome world (any older host: one round
//     trip after a join), and my_world == 0.
inline HostWorld classify(bool connected_guest, uint32_t my_mode, uint32_t my_world, bool has_state, uint32_t st_mode,
                          uint32_t st_world, uint32_t welcome_world)
{
    if (!connected_guest || !gated(my_mode))
        return HostWorld::NotGated;
    if (has_state && st_mode != my_mode)
        return HostWorld::NotGated;
    uint32_t host = has_state ? st_world : welcome_world;
    return my_world != 0 && host == my_world ? HostWorld::Same : HostWorld::Other;
}

constexpr uint32_t kSpace = 0x1654C05;

// the world id the host announces in WELCOME: its save in a gated stage; in Space (BUG-0709) the CRC of its save name
// (`space_save`: the place in Space is the star system, not the save); else 0 (unknown)
inline uint32_t welcome_world(uint32_t host_mode, uint32_t host_world, uint32_t space_save = 0)
{
    return gated(host_mode) ? host_world : host_mode == kSpace ? space_save : 0;
}

// BUG-0709 (class BUG-0420): a guest in Space plays another Space save than the host's (its own empire, its own
// money): the shared Space money must not be applied there. Only when both saves are known and the host's latest
// state (if any) is in Space too; unknown counts as the same (the 0.7.10 behaviour, nothing held).
inline bool space_other(bool connected_guest, uint32_t my_mode, uint32_t my_save, bool has_state, uint32_t st_mode,
                        uint32_t host_save)
{
    if (!connected_guest || my_mode != kSpace || (has_state && st_mode != kSpace))
        return false;
    return my_save != 0 && host_save != 0 && my_save != host_save;
}

// BUG-0933: the host's save as a guest knows it. WELCOME names it once, at the join; a host that loads another save
// later (or whose save gets its name later) made every guest that joined before "a guest in another Space save" for
// the rest of the session (space_other above compared with the stale WELCOME value): no shared Space money.
//   * a guest follows the host's save: `known` (WELCOME, then MSG_HOST_WORLD from a 0.7.12 host) is replaced by
//     the host's state world whenever that state is in a gated stage (there the state's world is the save's CRC,
//     so this works with an older host too); 0 never replaces a known save;
//   * the host announces its world id again when it changed to another known one (0, a menu or a load: not said).
inline uint32_t host_save_now(uint32_t known, bool has_state, uint32_t st_mode, uint32_t st_world)
{
    return has_state && gated(st_mode) && st_world != 0 ? st_world : known;
}
inline bool announce_world(uint32_t now, uint32_t announced) { return now != 0 && now != announced; }

// O1 of 0.7.13 (BUG-0427's children): the same question asked by the host about one guest, from that guest's latest
// PlayerState. `peer_elsewhere`: the guest plays another world of the host's stage, exactly what that guest's own
// classify() says (Other: it ignores the host's votes and gated messages, BUG-0702). `peer_here`: the guest plays
// the host's stage and world now (not loading it, not in a menu, not elsewhere).
inline bool peer_elsewhere(bool has_state, uint32_t peer_mode, uint32_t peer_world, uint32_t host_mode, uint32_t host_world)
{
    return has_state && classify(true, peer_mode, peer_world, true, host_mode, host_world, 0) == HostWorld::Other;
}
inline bool peer_here(bool has_state, uint32_t peer_mode, uint32_t peer_world, uint32_t host_mode, uint32_t host_world)
{
    return has_state && peer_mode == host_mode && host_world != 0 && peer_world == host_world;
}

// BUG-0797: a guest in a gated stage whose host plays another stage (the host already in Creature while this guest is
// still in Cell, BUG-0638's path; or in Space): nothing of the host's world of this stage comes any more, the guest
// plays its own (as for Other). Only when the host's state names another stage (not a menu, an editor or a load:
// those are the "away" rules of coop.cpp host_out_of_stage).
inline bool host_in_other_stage(bool connected_guest, uint32_t my_mode, bool has_state, uint32_t st_mode)
{
    return connected_guest && gated(my_mode) && has_state && st_mode != my_mode && (gated(st_mode) || st_mode == kSpace);
}

// BUG-0420 (class: world state applied without a world filter): every message type has one class, so that a new
// message is never open by default. B3 of 0.7.15 (BUG-1136): the class is the `cls` column of net/messages.def, which
// lists every type exactly once (the enum MsgType is generated from it); tests/validate_tests.cpp compares the table
// with the 0.7.14 switch it replaced. A value that is not a MsgType at all (a newer peer) is Unknown.
enum class MsgClass {
    Session,   // the session itself or a player's own data (join, chat, ready, a creation, a guest's report to
               // the host): applied wherever the guest plays
    HostWorld, // the host's world state: a guest elsewhere drops it at dispatch (coop.cpp gate_drops)
    Module,    // the host's world, gated by its module per place (npc: same_place / host_absent_for; wobj, tribe,
               // civ: host_away; Space: squadron / spacecoop with_host, my_place; effects: C.here)
    Unknown,   // not a message type of this build: a guest elsewhere drops it (default deny)
};
static_assert((int)MsgClass::Session == (int)net::MsgCls::Session && (int)MsgClass::HostWorld == (int)net::MsgCls::HostWorld &&
              (int)MsgClass::Module == (int)net::MsgCls::Module && (int)MsgClass::Unknown == (int)net::MsgCls::Unknown);

inline MsgClass msg_class(uint8_t type) { return static_cast<MsgClass>(net::msg_info(type).cls); }

// does a guest elsewhere drop the host's message at dispatch? `classes` off (core.msg_classes): the hand list of
// 0.7.10 (the HostWorld rows; the Civ rows only with `civ_rows`, core.world_gate)
inline bool world_state_msg(uint8_t type, bool classes, bool civ_rows)
{
    MsgClass c = msg_class(type);
    bool civ = type == net::MSG_CIV_STATE || type == net::MSG_CIV_BUILD || type == net::MSG_CIV_ALARM;
    if (civ && !civ_rows)
        return false;
    if (c == MsgClass::HostWorld)
        return true;
    return classes && c == MsgClass::Unknown;
}

} // namespace host_world
