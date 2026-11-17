// Decision 5O A (docs/12-server-and-players.md#server-front-door): the galaxy server is the one address every player
// connects to. It runs no Spore: it lists the planets, decides which player hosts each planet now and relays the
// players' traffic to that host. A planet's session is an ordinary SporeCoop session (net::Session) whose packets travel
// inside relay frames: the game's RelayTransport (galaxy_client.cpp) gives the session virtual endpoints, one per
// player slot on the galaxy server, so nothing above the transport changes.
//
// Portable: the galaxy server (server/galaxy) and the game share this file.
//
// Every datagram to or from the galaxy server starts with one byte:
//   kControl  'G'  u8 type, then the type's fields (below)
//   kRelay    'R'  u16 slot, then a session packet as it is. Player -> server: slot = where it goes (a slot on the
//                  same planet); server -> player: slot = where it came from.
#pragma once

#include <cstdint>
#include <string>

#include "buffer.h"
#include "udp.h"

namespace net::galaxy {

constexpr uint16_t kDefaultPort = 47900;
constexpr uint32_t kProto = 1;
constexpr uint8_t kControl = 'G', kRelay = 'R';

enum Type : uint8_t {
    HELLO = 1,    // player -> server: u32 proto, u64 identity, str8 name, str8 version
    WELCOME = 2,  // server -> player: u16 its slot, str8 galaxy name
    LIST = 3,     // player -> server
    PLANETS = 4,  // server -> player: u8 n, n x {str8 planet, u16 players, u16 host slot (0 none)}
    ENTER = 5,    // player -> server: str8 planet (made when it does not exist yet)
    ROLE = 6,     // server -> player: str8 planet, u8 role (kHostRole / kGuestRole), u16 the host's slot
    LEAVE = 7,    // player -> server: leaves its planet
    PING = 8,     // player -> server (every 2 s); the server answers PONG
    PONG = 9,
    REFUSED = 10, // server -> player: str8 why
    HOSTGONE = 11, // server -> the planet's players: str8 planet (its host left; the hand-over is a later step)
};
constexpr uint8_t kHostRole = 1, kGuestRole = 2;

// A player slot as the session's address: 10.88.<slot hi>.<slot lo>, port kDefaultPort. (10/8 is private: logs keep
// it as it is, docs/04-netcode.md.)
inline Endpoint slot_endpoint(uint16_t slot)
{
    Endpoint e;
    const uint32_t host_order = 0x0A580000u | slot;
    e.ip = ((host_order & 0xFF) << 24) | ((host_order & 0xFF00) << 8) | ((host_order >> 8) & 0xFF00) | (host_order >> 24);
    e.port = kDefaultPort;
    return e;
}

// the slot of a virtual endpoint, 0 when it is none
inline uint16_t endpoint_slot(const Endpoint& e)
{
    const uint32_t h = ((e.ip & 0xFF) << 24) | ((e.ip & 0xFF00) << 8) | ((e.ip >> 8) & 0xFF00) | (e.ip >> 24);
    return e.port == kDefaultPort && (h & 0xFFFF0000u) == 0x0A580000u ? uint16_t(h & 0xFFFF) : uint16_t(0);
}

inline Writer control(Type t)
{
    Writer w;
    w.u8(kControl);
    w.u8(t);
    return w;
}

} // namespace net::galaxy
