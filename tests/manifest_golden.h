// Golden copies of the tables that net/messages.def and net/space_kinds.def replaced (B3 of 0.7.15, BUG-1136), verbatim
// from 0.7.14 (rel/0.7.15 d0cdc8ea): tests/validate_tests.cpp checks that the generated tables equal them for every
// type, kind and role. Never edit them to make a test pass: a deliberate change of a row is listed in the test.
#pragma once

#include <cstdint>

#include "host_world.h"
#include "net/manifest.h"
#include "net/protocol.h"
#include "net/validate.h"

namespace golden0714 {

// net/protocol.h enum MsgType (id, name)
struct TypeName {
    uint8_t id;
    const char* name;
};
constexpr TypeName kTypes[] = {
    {1, "HELLO"}, {2, "WELCOME"}, {3, "REJECT"}, {4, "PLAYER_JOIN"}, {5, "PLAYER_LEAVE"}, {6, "PLAYER_STATE"},
    {7, "CHAT"}, {8, "PING"}, {9, "PONG"}, {10, "BYE"}, {11, "ABILITY"}, {12, "DAMAGE"}, {13, "NPC_STATE"},
    {14, "WORLD_OFFER"}, {15, "WORLD_FILE"}, {16, "WORLD_CHUNK"}, {17, "WORLD_DONE"}, {18, "ANIM"}, {19, "NPC_SPAWN"},
    {20, "NPC_DESPAWN"}, {21, "DAMAGE_REQ"}, {22, "WORLD_TIME"}, {23, "DNA"}, {24, "DNA_ADD"}, {25, "RES"},
    {26, "RES_ADD"}, {27, "PING_MARK"}, {28, "READY"}, {29, "LOBBY"}, {30, "NOTICE"}, {31, "TRIBE_STATE"},
    {32, "TRIBE_CMD"}, {33, "CELL_STATE"}, {34, "CELL_HIT"}, {35, "CELL_EAT"}, {36, "CELL_FOOD"}, {37, "CIV_STATE"},
    {38, "CIV_CMD"}, {39, "CIV_BUILD"}, {40, "UFO_STATE"}, {41, "UFO_HIT"}, {42, "UFO_DAMAGE"}, {43, "VOTE"},
    {44, "CELL_KO"}, {45, "CREATION"}, {46, "ROLE"}, {47, "CIV_ALARM"}, {48, "SPACE"}, {49, "CELL_BITTEN"},
    {50, "SPECIES"}, {51, "SOCIAL"}, {52, "NEST"}, {53, "PARTS"}, {54, "WOBJ"}, {56, "FIELDS"}, {57, "NPC_RESYNC"},
    {59, "NPC_ANIM"}, {60, "DIAG"}, {61, "EFFECT"}, {62, "WSTATE"}, {63, "PLAYER_RULE"}, {64, "MIRROR_FX"},
    {65, "NPC_MEAT"}, {66, "SETTINGS"}, {67, "COLORS"}, {68, "REPORT"}, {69, "HOST_WORLD"},
};

// host_world.h msg_class
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
inline host_world::MsgClass msg_class(uint8_t type)
{
    switch (static_cast<net::MsgType>(type)) {
    case net::MSG_HELLO: case net::MSG_WELCOME: case net::MSG_REJECT: case net::MSG_PLAYER_JOIN:
    case net::MSG_PLAYER_LEAVE: case net::MSG_PLAYER_STATE: case net::MSG_CHAT: case net::MSG_PING: case net::MSG_PONG:
    case net::MSG_BYE: case net::MSG_WORLD_OFFER: case net::MSG_WORLD_FILE: case net::MSG_WORLD_CHUNK:
    case net::MSG_WORLD_DONE: case net::MSG_DNA_ADD: case net::MSG_RES_ADD: case net::MSG_PING_MARK: case net::MSG_READY:
    case net::MSG_LOBBY: case net::MSG_NOTICE: case net::MSG_ROLE: case net::MSG_CREATION: case net::MSG_DIAG:
    case net::MSG_ABILITY: case net::MSG_ANIM: case net::MSG_DAMAGE: // a partner's own creature (its puppet)
        return host_world::MsgClass::Session;
    case net::MSG_SETTINGS: // the host's session settings: the session's rules wherever the guest plays
        return host_world::MsgClass::Session;
    case net::MSG_COLORS: // the player colours (F2): the session's, wherever the guest plays
    case net::MSG_REPORT: // the session report (BUG-0767): a player's own report, asked wherever the guest plays
    case net::MSG_HOST_WORLD: // the host's save now (BUG-0933): what "elsewhere" is measured against, wherever we play
        return host_world::MsgClass::Session;
    case net::MSG_VOTE: // session-wide; the editor start it brings is gated in its handler (core.vote_world)
    case net::MSG_RES:  // held inside shared.cpp (res.elsewhere), not at dispatch
        return host_world::MsgClass::Session;
    case net::MSG_DNA: case net::MSG_WORLD_TIME: case net::MSG_CELL_STATE: case net::MSG_CELL_HIT:
    case net::MSG_CELL_EAT: case net::MSG_CELL_FOOD: case net::MSG_CELL_KO: case net::MSG_CELL_BITTEN:
    case net::MSG_TRIBE_STATE: case net::MSG_TRIBE_CMD: case net::MSG_SPECIES: case net::MSG_SOCIAL: case net::MSG_NEST:
    case net::MSG_PARTS: case net::MSG_FIELDS: case net::MSG_WSTATE: case net::MSG_PLAYER_RULE: case net::MSG_MIRROR_FX:
    case net::MSG_CIV_STATE: case net::MSG_CIV_BUILD: case net::MSG_CIV_ALARM:
        return host_world::MsgClass::HostWorld;
    case net::MSG_NPC_STATE: case net::MSG_NPC_SPAWN: case net::MSG_NPC_DESPAWN: case net::MSG_DAMAGE_REQ:
    case net::MSG_NPC_RESYNC: case net::MSG_NPC_ANIM: case net::MSG_NPC_MEAT: case net::MSG_WOBJ: case net::MSG_EFFECT:
    case net::MSG_CIV_CMD: case net::MSG_UFO_STATE: case net::MSG_UFO_HIT: case net::MSG_UFO_DAMAGE: case net::MSG_SPACE:
        return host_world::MsgClass::Module;
    case net::MSG_HOST_BUSY: // (added after 0.7.14, BUG-1142: a type 0.7.14 did not know; its default deny)
        return host_world::MsgClass::Unknown;
    case net::MSG_TRIBE_ANIM: // (added after 0.7.14, C7 BUG-1160: a type 0.7.14 did not know)
        return host_world::MsgClass::Unknown;
    case net::MSG_CELL_MATE: // (added after 0.7.15.1, BUG-1246: a type 0.7.15.1 did not know)
        return host_world::MsgClass::Unknown;
    case net::MSG_SPECIES_LINE: // (added after 0.7.15.2, WP-0 BUG-1270: a type 0.7.15.2 did not know)
        return host_world::MsgClass::Unknown;
    case net::MSG_CIV_WALLET: // (added after 0.7.16.1, WP-M1 BUG-1468: a type 0.7.16.1 did not know)
    case net::MSG_PLAYER_HISTORY: // (added in 0.7.17, WP-H1 BUG-1470: a type 0.7.16.1 did not know)
    case net::MSG_UFO_LEAD: // (added after 0.7.16.1, WP-S1 BUG-1288: a type 0.7.16.1 did not know)
    case net::MSG_PROGRESS: // (added in 0.7.17, WP-P1/P2 BUG-1469: a type 0.7.16.1 did not know)
    case net::MSG_RESCUE: // (added in 0.7.17, WP-R1 BUG-1472: a type 0.7.16.1 did not know)
        return host_world::MsgClass::Unknown;
    }
    return host_world::MsgClass::Unknown;
}
#pragma GCC diagnostic pop

// net/validate.h space_dir
inline net::SpaceDir space_dir(uint8_t kind)
{
    switch (kind) {
    // spacecoop.cpp: K_DOWN 1, K_RAID 2, K_MISSION 3, K_TERRA 4, K_GIFT 5, K_GIFT_BACK 6, K_SCULPT 7, K_COLOUR 8, K_TERRA_REC 9
    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9:
        return net::SpaceDir::Any;
    // space_map: K_ROSTER 10, K_ROSTER_REQ 11 (any); K_KEPT 12 (host); K_KEPT_DONE 13 (guest)
    case 10: case 11: return net::SpaceDir::Any;
    case 12: return net::SpaceDir::Host;
    case 13: return net::SpaceDir::Guest;
    // space_contracts: K_STATE 20, K_RESULT 22 (host); K_ACCEPT 21 (guest)
    case 20: case 22: return net::SpaceDir::Host;
    case 21: return net::SpaceDir::Guest;
    // space_diplomacy: K_EVENT 30, K_STATE 31, K_CONTACT 34 (any); K_SNAP 32 (host); K_ASK 33 (guest)
    case 30: case 31: case 34: return net::SpaceDir::Any;
    case 32: return net::SpaceDir::Host;
    case 33: return net::SpaceDir::Guest;
    // space_finale: K_STATE 40, K_WAIT 41, K_REACHED 42 (any)
    case 40: case 41: case 42: return net::SpaceDir::Any;
    // space_rivalry: K_STATE 50, K_EVENT 52, K_SET 57 (host); K_REQ 51, K_MERGE_FIX 55, K_ASK 56 (guest);
    // K_HIT 53, K_MONEY 54 (any)
    case 50: case 52: case 57: return net::SpaceDir::Host;
    case 51: case 55: case 56: return net::SpaceDir::Guest;
    case 53: case 54: return net::SpaceDir::Any;
    // space_allies: K_RAIDS 60, K_DEFENDED 61 (any)
    case 60: case 61: return net::SpaceDir::Any;
    // space_trade: K_TOOL .. K_OFFER_CANCEL 70..76 (any, addressed); K_INV 77, K_INV_DONE 79 (guest); K_INV_KEPT 78 (host)
    case 70: case 71: case 72: case 73: case 74: case 75: case 76: return net::SpaceDir::Any;
    case 77: case 79: return net::SpaceDir::Guest;
    case 78: return net::SpaceDir::Host;
    // space_planets: K_SPICE 80 (host); K_SPICE_TAKEN 81 (guest)
    case 80: return net::SpaceDir::Host;
    case 81: return net::SpaceDir::Guest;
    // space_badges: K_BADGES 85, K_BADGES_DONE 87 (guest); K_BADGES_KEPT 86 (host)
    case 85: case 87: return net::SpaceDir::Guest;
    case 86: return net::SpaceDir::Host;
    default:
        return net::SpaceDir::Unknown; // no feature reads it: passed on as before (a newer peer's kind)
    }
}

// session_glue.cpp g_sess.cb.custom: which handler the hand-written chain called on this game (a guest's from is
// always 0). The world gate (from == 0 && gate_drops) sat after the Effect branch; Route::None: the chain fell to its
// end, where a guest's world::on_message ignored every type but the four world transfer ones
inline net::Route route(uint8_t type, bool host)
{
    using namespace net;
    using R = Route;
    if (type == MSG_DIAG) return R::Diag;
    if (type == MSG_ANIM) return R::Anim;
    if (type == MSG_SETTINGS) return R::Settings;
    if (type == MSG_COLORS) return R::Colors;
    if (type == MSG_REPORT) return R::Report;
    if (type == MSG_HOST_WORLD) return R::HostWorld;
    if (type == MSG_EFFECT) return R::Effect;
    // (the world gate)
    if (type == MSG_DNA && !host) return R::Dna;
    if (type == MSG_DNA_ADD && host) return R::DnaAdd;
    if (type == MSG_WORLD_TIME && !host) return R::WorldTime;
    if (type == MSG_VOTE && !host) return R::Vote;
    if (type == MSG_NOTICE) return R::Notice;
    if (type == MSG_ROLE && host) return R::Role;
    if (type == MSG_READY && host) return R::Ready;
    if (type == MSG_LOBBY && !host) return R::Lobby;
    if (type == MSG_RES || type == MSG_RES_ADD) return R::Shared;
    if (type == MSG_PING_MARK) return R::PingMark;
    if (type == MSG_SPACE) return R::Space;
    if (type >= MSG_UFO_STATE && type <= MSG_UFO_DAMAGE) return R::Squadron;
    if ((type >= MSG_CIV_STATE && type <= MSG_CIV_BUILD) || type == MSG_CIV_ALARM) return R::Civ;
    if (type == MSG_CREATION) return R::Creation;
    if ((type >= MSG_CELL_STATE && type <= MSG_CELL_FOOD) || type == MSG_CELL_KO || type == MSG_CELL_BITTEN) return R::Cell;
    if (type == MSG_TRIBE_STATE || type == MSG_TRIBE_CMD) return R::Tribe;
    switch (type) {
    case MSG_NPC_SPAWN: case MSG_NPC_DESPAWN: case MSG_NPC_STATE:
    case MSG_DAMAGE_REQ: case MSG_DAMAGE: case MSG_NPC_RESYNC: case MSG_NPC_ANIM:
    case MSG_NPC_MEAT:
        return R::Npc;
    case MSG_SPECIES: case MSG_SOCIAL: return R::Social;
    case MSG_NEST: return R::Nest;
    case MSG_FIELDS: return R::Fields;
    case MSG_WSTATE: return R::Wsync;
    case MSG_PARTS: return R::Parts;
    case MSG_WOBJ: return R::Wobj;
    case MSG_PLAYER_RULE: return R::PlayerRule;
    case MSG_MIRROR_FX: return R::MirrorFx;
    }
    if (!host && type >= MSG_WORLD_OFFER && type <= MSG_WORLD_DONE) return R::World;
    return R::None;
}
// the routes the chain tested before the world gate
inline bool before_gate(net::Route r)
{
    using R = net::Route;
    return r == R::Diag || r == R::Anim || r == R::Settings || r == R::Colors || r == R::Report || r == R::HostWorld ||
           r == R::Effect;
}

} // namespace golden0714
