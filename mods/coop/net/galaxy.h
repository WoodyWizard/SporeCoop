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
//   kFan      'F'  decision 6F B (owner "6FC"): one unreliable message the server copies to several players of the
//                  planet. Host -> server: u8 n, n x u16 slot, then u8 message type and its body; server -> each of
//                  those players: u16 the host's slot, then the message type and body. Only the planet's host sends
//                  it, only to the planet's players: the host's upload carries one copy for players who stand together.
#pragma once

#include <cstdint>
#include <algorithm>
#include <string>
#include <vector>

#include "buffer.h"
#include "udp.h"

namespace net::galaxy {

constexpr uint16_t kDefaultPort = 47900;

// Step 4, part 1: eras (docs/12-server-and-players.md#server-eras, owner 2026-11-19 on 7C: "they should be in the same
// stage with other players in their stages"). A planet has one shared world per stage; each is its own planet session
// on the server, keyed "<planet>|<Era>" (Samuth|Creature, Samuth|Tribe): hosting, storage, hand-overs and joins work
// per era unchanged. A key without "|" is an older plain planet (no era: kept as it is).
enum Era : uint8_t { kEraNone = 0, kEraCell = 1, kEraCreature = 2, kEraTribe = 3, kEraCiv = 4, kEraSpace = 5 };
constexpr uint8_t kEraLast = kEraSpace;
constexpr char kEraSep = '|';
inline const char* era_name(uint8_t e)
{
    switch (e) {
    case kEraCell: return "Cell";
    case kEraCreature: return "Creature";
    case kEraTribe: return "Tribe";
    case kEraCiv: return "Civ";
    case kEraSpace: return "Space";
    default: return "";
    }
}
// the era of a key ("Samuth|Tribe": kEraTribe; no era or an unknown one: kEraNone)
inline uint8_t era_of(const std::string& key)
{
    const size_t i = key.rfind(kEraSep);
    if (i == std::string::npos)
        return kEraNone;
    for (uint8_t e = 1; e <= kEraLast; e++)
        if (key.compare(i + 1, std::string::npos, era_name(e)) == 0)
            return e;
    return kEraNone;
}
// the planet of a key ("Samuth|Tribe": "Samuth"; a plain name: itself)
inline std::string era_planet(const std::string& key)
{
    return era_of(key) ? key.substr(0, key.rfind(kEraSep)) : key;
}
inline std::string era_key(const std::string& planet, uint8_t era)
{
    return era ? era_planet(planet) + kEraSep + era_name(era) : era_planet(planet);
}
// Step 4 part 3 (docs/12-server-and-players.md#server-party-stage-end): a party crossing to the next stage plays in a
// session of its own, "<planet>~<party id>|<Era>" (never stored, never listed, only made by the server)
inline std::string crossing_key(const std::string& key, uint16_t party)
{
    return era_planet(key) + "~" + std::to_string(party) + kEraSep + era_name(era_of(key));
}
inline bool is_crossing(const std::string& key)
{
    const std::string p = era_planet(key);
    const size_t i = p.rfind('~');
    if (!era_of(key) || i == std::string::npos || i + 1 >= p.size())
        return false;
    for (size_t k = i + 1; k < p.size(); k++)
        if (p[k] < '0' || p[k] > '9')
            return false;
    return true;
}
// the planet a crossing belongs to ("Samuth~12|Creature": "Samuth"; anything else: era_planet)
inline std::string crossing_planet(const std::string& key)
{
    const std::string p = era_planet(key);
    return is_crossing(key) ? p.substr(0, p.rfind('~')) : p;
}
// for messages: "Samuth (Tribe)", a crossing "Samuth (your party's way to the next stage)"
inline std::string era_display(const std::string& key)
{
    if (is_crossing(key))
        return crossing_planet(key) + " (your party's crossing)";
    const uint8_t e = era_of(key);
    return e ? era_planet(key) + " (" + era_name(e) + ")" : key;
}
// the menu's tech level of an upload (5X B: 0 none or Cell, 1 Creature, 2 Tribe, 4 Civ, 5 Space) as an era
inline uint8_t era_of_tech(uint8_t tech)
{
    return tech == 1 ? kEraCreature : tech == 2 ? kEraTribe : tech == 4 ? kEraCiv : tech == 5 ? kEraSpace : kEraNone;
}
// a world blob (world.cpp galaxy_pack: u32 "SCW1", str8 the planet's save name, ...): its planet's name, "" if not one
inline std::string blob_planet(const std::vector<uint8_t>& blob)
{
    if (blob.size() < 5 || blob[0] != 'S' || blob[1] != 'C' || blob[2] != 'W' || blob[3] != '1')
        return "";
    const size_t n = blob[4];
    return blob.size() < 5 + n ? "" : std::string((const char*)blob.data() + 5, n);
}
constexpr uint32_t kProto = 9; // 2: stored worlds (5Q A); 3: the hand-over (5V A); 4: the menu facts (5X B);
                                // 5: the warm hand-over token (6B C); 6: the identity key and the address cookie (6S);
                                // 7: eras (step 4 part 1: an upload names its era, the server keeps 3 older saves);
                                // 8: a party's crossing to the next stage (MOVEON, MOVED, ARRIVE: step 4 part 3);
                                // 9: a player starts a stage the planet has no era of (STARTERA, BUG-9336)
constexpr uint8_t kControl = 'G', kRelay = 'R', kFan = 'F';
constexpr size_t kMaxFanSlots = 64; // (kMaxPlanetPlayers)
// decision 6I A: a guest that loaded the planet's stored world joins with this receipt; the host takes it (no world sent)
// when the stored world is its own last upload and nothing was saved since (galaxy_client.cpp accept_warm)
inline uint64_t stored_receipt(uint32_t crc, uint32_t world) { return ((uint64_t)crc << 32) | world; }
// the REFUSED text for a datagram from an address the server does not know (6H A: the game connects again on it)
constexpr const char* kNotConnected = "Not connected to the galaxy server: connect first.";

enum Type : uint8_t {
    HELLO = 1,    // player -> server: u32 proto, u64 identity, str8 name, str8 version, u64 key, u64 cookie (6: 6S)
    WELCOME = 2,  // server -> player: u16 its slot, str8 galaxy name, u8 the players a planet takes (6E B; older: 4),
                  //   u32 the nest refresh: seconds after which an extinct species comes back, 0 off (step 4 part 4,
                  //   BUG-9285; older servers send none: off), u32 the nest rotation: seconds after which a player's nest
                  //   whose species has had no player on the planet gets a new species, 0 off (7F, BUG-9286)
    LIST = 3,     // player -> server; S10: + u16 the first planet of the page (older players send none: 0)
    PLANETS = 4,  // server -> player: u8 n, n x {str8 planet, u16 players, u16 host slot (0 none), u32 stored world bytes,
                  //   u8 tech level, u32 x3 its creature's species key} (the last two: the galaxy menu, 5X B);
                  //   S10: + u16 the planets in all, u16 this page's first (an older server: none, one page)
    ENTER = 5,    // player -> server: str8 planet (made when it does not exist yet)
    ROLE = 6,     // server -> player: str8 planet, u8 role (kHostRole / kGuestRole), u16 the host's slot,
                  //   u32 the planet's stored world (bytes, 0 none: a host then FETCHes it, loads it and hosts it),
                  //   u64 the hand-over token (0 none; 6B C: a player still in the planet's world rejoins with it),
                  //   u32 the stored world's crc (6I A, 0 none; a guest: the planet's stored world is offered, it may
                  //   FETCH and load it, then join with the receipt stored_receipt(crc, world)), u32 its age in
                  //   seconds (a guest's ROLE; all ones: unknown)
    LEAVE = 7,    // player -> server: leaves its planet
    PING = 8,     // player -> server (every 2 s); the server answers PONG. Since the 2026-11-18 review it carries
                  //   str8 the planet the game is on ("" none), u8 kPingOnPlanet / kPingHosting: READY and LEAVE go
                  //   once over UDP, so the server takes a planet's state from the pings when one was lost (an older
                  //   game sends no body: nothing is taken from it)
    PONG = 9,     // server -> player: u64 the server's clock (Unix seconds; BUG-9285: the nest refresh's clock, the same
                  //   for every host of a planet; older servers send no body)
    REFUSED = 10, // server -> player: str8 why
    HOSTGONE = 11, // server -> the planet's players: str8 planet (its host left; the hand-over is a later step)
    READY = 12,    // host -> server: its session hosts the planet now (players entering before are asked to wait)
    FETCH = 13,    // player -> server: send me the planet's stored world (an XFER from the server follows; its host,
                   //   or 6I A a guest of the planet)
    XBEGIN = 14,   // sender -> receiver: u32 id, str8 planet, u32 bytes, u32 crc (a world: player uploads, server sends);
                   //   an upload adds u8 tech level, u32 x3 species key (the menu's facts of the planet, 5X B),
                   //   u8 its era (7: the world's stage; the server refuses a world of another era than the key's)
    XCHUNK = 15,   // u32 id, u32 index, the chunk (kChunk bytes, the last one shorter)
    XACK = 16,     // receiver -> sender: u32 id, u32 index (each chunk that arrived)
    XDONE = 17,    // receiver -> sender: u32 id, u8 ok, str8 why (stored / loaded, or why not)
    HOSTMOVE = 18, // server -> the planet's other players: str8 planet, u16 the new host's slot, u64 the hand-over
                   //   token (5V A: the host left; the new host keeps its world or loads the stored one; once it is
                   //   READY the server sends them ROLE guest again)
    // decision 6K (owner 2026-11-18: "Parties shouldn't be reset/disbanded if the host changes", "allow partying in the
    // galaxy menu"): the galaxy server keeps the parties (parties.h, by player slot), across planets and hand-overs
    PARTY = 19,     // player -> server: u8 kind (kPartyInvite .. kPartyStage), u64 the other player's identity (0 none),
                    //   u8 flag (an answer: 1 yes), u32 the sender's stage (its game mode: 0 a menu or a load)
    PARTYVIEW = 20, // server -> player: its party: u16 party id, u16 the leader's slot, u8 n, n x {u16 slot, u64
                    //   identity, str8 name, str8 planet, u32 stage} (the longest standing first), u16 who invites it
                    //   (0 none), u64 its identity, str8 its name
    PARTYNOTE = 21, // server -> player: u8 the request's kind, u8 parties::Result, str8 the other player's name
    WHO = 22,       // player -> server: the players online; S10: + u16 the first player of the page (older: 0)
    PLAYERS = 23,   // server -> player: u16 n, n x {u16 slot, u64 identity, str8 name, str8 planet, u16 party id};
                    //   S10: + u16 the players in all, u16 this page's first (an older server: none, one page)
    // 6L A (the Social Menu): a whisper to any player of the galaxy. player -> server: u64 the receiver's identity,
    // str16 text; server -> receiver: u64 the sender's identity, str8 its name, str16 text
    WHISPER = 24,
    // 6S (BUG-9273): server -> a new address: u64 cookie; the HELLO from that address carries it back (the address is
    // real: no slot, no answer bigger than the HELLO for a spoofed one)
    CHALLENGE = 25,
    // step 4 part 3: a party ends its stage (docs/12-server-and-players.md#server-party-stage-end)
    MOVEON = 26, // the party's leader -> server: take my party out of this era into its crossing (no body)
    MOVED = 27,  // server -> a player: str8 planet, u8 kind (kMovedSplit / kMovedRenamed / kMovedJoin), u16 its host's
                 //   slot, u64 the token (a split: this game keeps its world, the host hosts it in place, the others
                 //   rejoin with the token); kMovedJoin: leave the crossing and ENTER the planet named (its era has a
                 //   host)
    ARRIVE = 28, // the crossing's host -> server: u8 the era its game plays now (the next stage): the crossing becomes
                 //   that era if nobody hosts it (kMovedRenamed), else everyone joins its host (kMovedJoin)
    // step 4 part 5 (BUG-9287, docs/12-server-and-players.md#server-era-visitors-built): visitors across eras. A player
    // in a stage sends where its creature is; the server passes it on to the players of the same planet in its other
    // worlds (its other eras, a party's crossing), whose games show it look-only
    VISIT = 29,    // player -> server (twice a second): u8 its era, u32 x3 its creature's species key, f32 x3 position
    VISITOR = 30,  // server -> player: u16 the sender's slot, u64 its identity (BUG-9290), str8 its name, VISIT's body
    // BUG-9287: the galaxy's creation store, so a game can show what another world has and it does not (a visitor's
    // creature, another era's vehicles and buildings). A player puts creations of its game there (the mod's bundle of
    // the records, creations.cpp); a game asks for one by its owner and key. Kept in the server's worlds folder
    // (creations/), across restarts. BUG-9290: a creation is {its owner's identity, instance, type, group} (creation ids
    // count up per profile: two players' creations can share a key).
    CRTPUT = 31,   // player -> server: u32 instance, u32 type, u32 group, u32 total bytes, u32 crc, u32 offset, the piece
                   //   (kCrtChunk bytes, the last one shorter); kept as the sender's; the server answers CRTHAVE
    CRTGET = 32,   // player -> server: u64 owner, u32 instance, u32 type, u32 group
    CRTHAVE = 33,  // server -> player: u64 owner, u32 instance, type, group, u8 1 kept / 0 the server has none (a CRTGET)
    CRTPART = 34,  // server -> player: u64 owner, u32 instance, type, group, u32 total, u32 crc, u32 offset, the piece
    // BUG-9290: a Civ era's host sends its vehicles and buildings near the other eras' players; the server passes it to
    // the planet's other worlds (as VISIT), at most kVisitObjPerSecond a second, kVisitObjMax bytes
    VISITOBJ = 35, // host -> server: u8 n, n x {u32 id, u32 x3 model key, f32 x3 position, f32 x4 orientation, f32 x3
                   //   velocity, u8 kind (kVisitVehicle / kVisitBuilding)}; server -> player: u16 slot, u64 identity, then that
    // BUG-9294 (decision 7I A): cross-stage visibility is the player's choice, on until it says otherwise. Off: the server
    // passes none of its VISIT / VISITOBJ on and sends it none of the others'
    ERAS = 36, // player -> server: u8 1 sees and is seen by the planet's other eras, 0 not (sent on each WELCOME and change)
    // BUG-9336 (5R A): a player starts a stage its planet has no era of yet (Tribe on a planet of Creature only). The
    // server makes it a crossing of its own from the era below's stored world (key crossing_key(from, its slot)) and
    // answers ROLE host with that world (FETCHed as any stored world); the game plays the stage's end there and ARRIVEs
    // as a party's crossing does (the crossing becomes the new era, or joins it if another player made it first)
    STARTERA = 37, // player -> server: str8 the era it starts from ("Samuth|Creature"), u8 the era to start (the next)
    // decision 8D A (owner 2026-11-23): the galaxy server's creation ban list (its worlds folder's banned.txt, the format
    // of the mod's mods\lineages\banned.txt), so a new ban reaches the galaxy's players without a mod release
    BANS = 38,     // player -> server: u32 the crc of the server list it keeps (0 none); the server sends BANSPART until
                   //   a BANS names the current one (sent on each WELCOME and after a list arrived)
    BANSPART = 39, // server -> player: u32 total bytes, u32 crc, u32 offset, the piece (kCrtChunk bytes); total 0: no list
};
constexpr uint32_t kBansMaxBytes = 1u << 20;
constexpr uint8_t kVisitVehicle = 1, kVisitBuilding = 2;
constexpr size_t kVisitObjBytes = 4 + 12 + 12 + 16 + 12 + 1, kVisitObjMax = 1 + 24 * kVisitObjBytes;
constexpr uint32_t kCrtChunk = 1100;
constexpr uint32_t kCrtMaxBytes = 512u << 10; // a species bundle (its records and creaturedata) is far smaller
constexpr uint32_t kVisitEveryMs = 500; // a player's VISITs: the server drops ones sooner than this after the last
constexpr uint8_t kMovedSplit = 1, kMovedRenamed = 2, kMovedJoin = 3;
// 6S: the identity's key does not match the one the server keeps for it
constexpr const char* kIdentityTaken = "This player's identity is in use with another key on this galaxy: is another copy of your profile playing?";
enum PartyKind : uint8_t { kPartyInvite = 1, kPartyAnswer, kPartyLeave, kPartyLead, kPartyKick, kPartyStage };
constexpr uint8_t kHostRole = 1, kGuestRole = 2;
constexpr uint8_t kPingNone = 0, kPingOnPlanet = 1, kPingHosting = 2;
constexpr int kPingMismatches = 3; // pings in a row that disagree with the server before it follows them (6 s)
// decision 6E B (owner 2026-11-18): "Seats just mean how many players are allowed on a planet. We can set it to 16 per
// planet." The server's --planet-players sets it (2 .. kMaxPlanetPlayers); "Setting it to 17 shouldn't cause any issues"
constexpr uint8_t kPlanetPlayers = 16;
constexpr uint8_t kMaxPlanetPlayers = 64; // the games' player ceiling (net::kMaxPlayers, protocol.h; galaxy_client.cpp checks)

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

// A world travels in chunks with an ack each (UDP): the sender keeps kWindow chunks in flight and sends a chunk again
// when its ack has not come within kResendMs. Both sides of the transfer (the server and the game) use these.
constexpr uint32_t kChunk = 1100, kWindow = 96, kResendMs = 1000; // (400 ms: half the chunks of a download to a game at 10 FPS went twice)
constexpr uint32_t kMaxWorld = 128u << 20; // (as slotfs::kMaxWorldBytes)

inline uint32_t crc32(uint32_t crc, const uint8_t* p, size_t n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

struct XferOut {
    uint32_t id = 0;
    std::string planet;
    std::vector<uint8_t> data;
    std::vector<uint32_t> sent_ms; // 0 never sent, 1 acked
    uint32_t acked = 0, next_unacked = 0, resent = 0, begin_ms = 0; // begin_ms: its XBEGIN last sent (again until an ack)
    uint32_t crc = 0;     // of the data, once (begin() goes out again until the first ack: 128 MB twice a second before)
    uint32_t ack_ms = 0;  // the last ack's time, set by the owner (a download nobody acks is given up)
    bool on = false;
    uint32_t chunks() const { return (uint32_t)((data.size() + kChunk - 1) / kChunk); }
    void start(uint32_t xid, const std::string& p, std::vector<uint8_t> d)
    {
        id = xid;
        planet = p;
        data = std::move(d);
        crc = crc32(0, data.data(), data.size());
        sent_ms.assign(chunks(), 0);
        acked = next_unacked = resent = begin_ms = ack_ms = 0;
        on = true;
    }
    Writer begin() const
    {
        Writer w = control_msg(XBEGIN);
        w.u32(id);
        w.str8(planet);
        w.u32((uint32_t)data.size());
        w.u32(crc);
        return w;
    }
    void on_ack(uint32_t idx)
    {
        if (idx < sent_ms.size() && sent_ms[idx] != 1) {
            sent_ms[idx] = 1;
            acked++;
        }
        while (next_unacked < sent_ms.size() && sent_ms[next_unacked] == 1)
            next_unacked++;
    }
    bool complete() const { return on && acked == sent_ms.size(); }
    // the chunks to send now (new ones within the window, overdue ones again); `send` gets each frame
    template <class F> void pump(uint32_t now, F send)
    {
        if (!on)
            return;
        const uint32_t end = std::min<uint32_t>((uint32_t)sent_ms.size(), next_unacked + kWindow);
        for (uint32_t i = next_unacked; i < end; i++) {
            uint32_t& t = sent_ms[i];
            // (signed: the stamp `now | 2` was up to 2 ms ahead of the clock, and `now - t` wrapped to a huge age: a
            // fresh chunk counted as overdue and went again)
            if (t == 1 || (t && (int32_t)(now - t) < (int32_t)kResendMs))
                continue;
            if (t)
                resent++;
            t = now < 2 ? 2 : now; // (never 0 or 1)
            Writer w = control_msg(XCHUNK);
            w.u32(id);
            w.u32(i);
            const size_t off = (size_t)i * kChunk, n = std::min<size_t>(kChunk, data.size() - off);
            w.raw(data.data() + off, n);
            send(w);
        }
    }
    static Writer control_msg(Type t)
    {
        Writer w;
        w.u8(kControl);
        w.u8(t);
        return w;
    }
};

struct XferIn {
    uint32_t id = 0, size = 0, crc = 0, got = 0;
    std::string planet;
    std::vector<uint8_t> data;
    std::vector<uint8_t> have;
    bool on = false;
    // the XBEGIN's fields only: nothing is allocated before the receiver checked who sends it (2026-11-18 review: any
    // connected player could make the server allocate 128 MB a datagram)
    bool header(Reader& r, uint32_t max_bytes = kMaxWorld)
    {
        id = r.u32();
        planet = r.str8();
        size = r.u32();
        crc = r.u32();
        on = false;
        return r.ok() && size <= max_bytes;
    }
    void alloc()
    {
        data.assign(size, 0);
        have.assign((size + kChunk - 1) / kChunk, 0);
        got = 0;
        on = true;
    }
    bool begin(Reader& r)
    {
        if (!header(r))
            return false;
        alloc();
        return true;
    }
    // a chunk (after its u32 id): true when it belongs here (the ack is due either way then)
    bool chunk(uint32_t idx, Reader& r)
    {
        if (!on || idx >= have.size())
            return false;
        const size_t off = (size_t)idx * kChunk, n = std::min<size_t>(kChunk, size - off);
        if (r.left() != n)
            return false;
        if (!have[idx]) {
            r.raw(data.data() + off, n);
            have[idx] = 1;
            got++;
        }
        return true;
    }
    bool complete() const { return on && got == have.size(); }
    bool valid() const { return complete() && crc32(0, data.data(), data.size()) == crc; }
};

inline Writer control(Type t)
{
    Writer w;
    w.u8(kControl);
    w.u8(t);
    return w;
}

} // namespace net::galaxy
