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

// BUG-0420 (class: world state applied without a world filter): every message type has one class here, so that
// a new message is never open by default. The switch below has no default: GCC fails the build of coop.dll and of
// tests/validate_tests.cpp on a MsgType that is not listed (-Werror=switch), which is the "every MSG_ classified"
// check of docs/generalisation-plan.md G3. A value that is not a MsgType at all (a newer peer) is Unknown.
enum class MsgClass {
    Session,   // the session itself or a player's own data (join, chat, ready, a creation, a guest's report to
               // the host): applied wherever the guest plays
    HostWorld, // the host's world state: a guest elsewhere drops it at dispatch (coop.cpp gate_drops)
    Module,    // the host's world, gated by its module per place (npc: same_place / host_absent_for; wobj, tribe,
               // civ: host_away; Space: squadron / spacecoop with_host, my_place; effects: C.here)
    Unknown,   // not a message type of this build: a guest elsewhere drops it (default deny)
};

#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
inline MsgClass msg_class(uint8_t type)
{
    switch (static_cast<net::MsgType>(type)) {
    case net::MSG_HELLO: case net::MSG_WELCOME: case net::MSG_REJECT: case net::MSG_PLAYER_JOIN:
    case net::MSG_PLAYER_LEAVE: case net::MSG_PLAYER_STATE: case net::MSG_CHAT: case net::MSG_PING: case net::MSG_PONG:
    case net::MSG_BYE: case net::MSG_WORLD_OFFER: case net::MSG_WORLD_FILE: case net::MSG_WORLD_CHUNK:
    case net::MSG_WORLD_DONE: case net::MSG_DNA_ADD: case net::MSG_RES_ADD: case net::MSG_PING_MARK: case net::MSG_READY:
    case net::MSG_LOBBY: case net::MSG_NOTICE: case net::MSG_ROLE: case net::MSG_CREATION: case net::MSG_DIAG:
    case net::MSG_ABILITY: case net::MSG_ANIM: case net::MSG_DAMAGE: // a partner's own creature (its puppet)
        return MsgClass::Session;
    case net::MSG_SETTINGS: // the host's session settings: the session's rules wherever the guest plays
        return MsgClass::Session;
    case net::MSG_COLORS: // the player colours (F2): the session's, wherever the guest plays
    case net::MSG_REPORT: // the session report (BUG-0767): a player's own report, asked wherever the guest plays
    case net::MSG_HOST_WORLD: // the host's save now (BUG-0933): what "elsewhere" is measured against, wherever we play
        return MsgClass::Session;
    case net::MSG_VOTE: // session-wide; the editor start it brings is gated in its handler (core.vote_world)
    case net::MSG_RES:  // held inside shared.cpp (res.elsewhere), not at dispatch
        return MsgClass::Session;
    case net::MSG_DNA: case net::MSG_WORLD_TIME: case net::MSG_CELL_STATE: case net::MSG_CELL_HIT:
    case net::MSG_CELL_EAT: case net::MSG_CELL_FOOD: case net::MSG_CELL_KO: case net::MSG_CELL_BITTEN:
    case net::MSG_TRIBE_STATE: case net::MSG_TRIBE_CMD: case net::MSG_SPECIES: case net::MSG_SOCIAL: case net::MSG_NEST:
    case net::MSG_PARTS: case net::MSG_FIELDS: case net::MSG_WSTATE: case net::MSG_PLAYER_RULE: case net::MSG_MIRROR_FX:
    case net::MSG_CIV_STATE: case net::MSG_CIV_BUILD: case net::MSG_CIV_ALARM:
        return MsgClass::HostWorld;
    case net::MSG_NPC_STATE: case net::MSG_NPC_SPAWN: case net::MSG_NPC_DESPAWN: case net::MSG_DAMAGE_REQ:
    case net::MSG_NPC_RESYNC: case net::MSG_NPC_ANIM: case net::MSG_NPC_MEAT: case net::MSG_WOBJ: case net::MSG_EFFECT:
    case net::MSG_CIV_CMD: case net::MSG_UFO_STATE: case net::MSG_UFO_HIT: case net::MSG_UFO_DAMAGE: case net::MSG_SPACE:
        return MsgClass::Module;
    }
    return MsgClass::Unknown;
}
#pragma GCC diagnostic pop

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
