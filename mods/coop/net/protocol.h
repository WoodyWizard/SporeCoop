// SporeCoop wire protocol: message ids and payload encoders/decoders (portable).
// See docs/04-netcode.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "buffer.h"

namespace net {

constexpr uint32_t kMagic = 0x4F435053; // 'SPCO'
constexpr uint16_t kProtoVersion = 7; // 3: shared resources, ping marks; 4: lobby, LAN discovery; 5: 128-packet acks;
                                      // 6: connection id in the header, unreliable BYE; 7: space features (space_ext.h)
constexpr uint16_t kDefaultPort = 47810;
constexpr int kMaxPlayers = 4; // host + 3 guests (Spore pack size)
constexpr size_t kMaxPacket = 1200;

enum MsgType : uint8_t {
    MSG_HELLO = 1,
    MSG_WELCOME = 2,
    MSG_REJECT = 3,
    MSG_PLAYER_JOIN = 4,
    MSG_PLAYER_LEAVE = 5,
    MSG_PLAYER_STATE = 6,
    MSG_CHAT = 7,
    MSG_PING = 8,       // unused (BUG-0144): nothing sends it, the host answers one; RTT comes from acks
    MSG_PONG = 9,       // unused, the answer to MSG_PING
    MSG_BYE = 10,
    MSG_ABILITY = 11,  // reserved, never sent or handled (BUG-0144)
    MSG_DAMAGE = 12,   // M2
    MSG_NPC_STATE = 13, // M2
    MSG_WORLD_OFFER = 14, // host -> guest: a save folder follows
    MSG_WORLD_FILE = 15,  // file header
    MSG_WORLD_CHUNK = 16, // file data
    MSG_WORLD_DONE = 17,  // end of transfer
    MSG_ANIM = 18,        // a player's creature played an animation (attack, social, ...)
    MSG_NPC_SPAWN = 19,   // host -> guest: start mirroring an NPC
    MSG_NPC_DESPAWN = 20, // host -> guest: stop mirroring an NPC
    MSG_DAMAGE_REQ = 21,  // guest -> host: my attack hit NPC net_id
    MSG_WORLD_TIME = 22,  // host -> guests: time of day (sky orientation quaternion)
    MSG_DNA = 23,         // host -> guests: shared DNA total (f32)
    MSG_DNA_ADD = 24,     // guest -> host: DNA the guest earned (f32 delta)
    MSG_RES = 25,         // host -> guests: shared resource total (u8 id, f64)
    MSG_RES_ADD = 26,     // guest -> host: local change of a shared resource (u8 id, f64 delta)
    MSG_PING_MARK = 27,   // any: "look here" marker (vec3), relayed by the host
    MSG_READY = 28,       // guest -> host: u8 ready
    MSG_LOBBY = 29,       // host -> guests: u8 count, {u8 id, u8 ready, u16 rtt_ms}
    MSG_NOTICE = 30,      // any: str8 text everybody sees (phase events, rescues), relayed by the host
    MSG_TRIBE_STATE = 31, // host -> guests: tribes + citizens snapshot (chunked)
    MSG_TRIBE_CMD = 32,   // guest -> host: order for own tribe members
    MSG_CELL_STATE = 33,  // host -> guests: NPC cells and food snapshot (chunked)
    MSG_CELL_HIT = 34,    // guest -> host: u32 host cell index, u8 damage, u8 type
    MSG_CELL_EAT = 35,    // guest -> host: u32 host cell index (eaten / destroyed by the guest)
    MSG_CELL_FOOD = 36,   // host -> guests: i32 food, i32 plant | guest -> host: i32 delta, u8 plant
    MSG_CIV_STATE = 37,   // host -> guests: cities (owners, political ids) + vehicles snapshot (chunked)
    MSG_CIV_CMD = 38,     // guest -> host: vehicle order / move / purchase / building
    MSG_CIV_BUILD = 39,   // host -> guests: a building appeared in a city (city index, noun, position)
    MSG_UFO_STATE = 40,   // host -> guests in its place: NPC ships snapshot (chunked)
    MSG_UFO_HIT = 41,     // guest -> host: u16 ship id, f32 damage, i32 type, vec3 dir
    MSG_UFO_DAMAGE = 42,  // host -> guest: f32 damage, i32 type, vec3 dir, u16 attacker ship id
    MSG_VOTE = 43,        // host -> guests: u8 active, u8 seconds left, str8 text (evolution vote)
    MSG_CREATION = 45,    // reliable chunks of a player's creation (owner, kind, id, total, offset, data)
    MSG_ROLE = 46,        // guest -> host: u8 role (civilization: 0 none, 1 military, 2 economic, 3 religious)
    MSG_CIV_ALARM = 47,   // host -> guests: u8 city index, u8 kind (0 military, 1 religious), u8 owner
    MSG_SPACE = 48,       // any -> (host relays) -> all: u8 kind, u8 origin, payload (spacecoop.cpp)
    MSG_CELL_BITTEN = 49, // guest -> host: u32 host cell index of the NPC whose mirror bit the guest's cell
    MSG_SPECIES = 50,     // host -> guests: species relationship table (u16 n, {key[3], i32 relation, u32 x,
                          // u32 impressed, u32 killed}) of the creature stage (social.cpp)
    MSG_SOCIAL = 51,      // u8 kind: 0 guest -> host act {u16 npc, u32 ability type}; 1 host -> guest progress
                          // {u16 npc, u8 done, u8 needed}; 2 host -> all result {u16 npc, u8 player, u8 ok,
                          // u32 reaction anim, f32 dna}; 3 guest -> host goal reward {u16 0, key[3],
                          // u8 outcome (6 ally, 5 extinct), f32 dna}, paid once per species and outcome
    MSG_CELL_KO = 44,     // any -> host -> all: u8 player, u8 state (0 up,1 out,2 revived,3 respawned),
                          // u8 tenths of a second left, u8 revive progress 0..255
    MSG_DIAG = 60,        // support diagnostics (net/diagmsg.h): u8 kind, payload; session clock, bug
                          // marks, sync self-check. Older SporeCoop ignores it (an unknown type).
};

// LAN discovery: plain datagrams outside any connection, answered by hosts on their game port
constexpr uint32_t kDiscoverQuery = 0x51435053; // 'SPCQ'
constexpr uint32_t kDiscoverReply = 0x52435053; // 'SPCR'
constexpr uint16_t kDiscoverPorts = 20;         // kDefaultPort .. +19: test lanes 1..10 host on +2(N-1)

struct DiscoverReply {
    uint16_t proto = kProtoVersion;
    uint16_t port = 0;
    std::string name; // host player
    uint8_t players = 0, max_players = 0;
    std::string info; // stage and world, e.g. "Creature - Scalinus"
    void write(Writer& w) const
    {
        w.u32(kDiscoverReply); w.u16(proto); w.u16(port); w.str8(name); w.u8(players); w.u8(max_players); w.str8(info);
    }
    bool read(Reader& r)
    {
        if (r.u32() != kDiscoverReply) return false;
        proto = r.u16(); port = r.u16(); name = r.str8(); players = r.u8(); max_players = r.u8(); info = r.str8();
        return r.ok();
    }
};

enum RejectReason : uint8_t {
    REJ_VERSION = 1,
    REJ_FULL = 2,
    REJ_BUILD = 3,
};

enum PlayerFlags : uint8_t {
    PF_IN_WORLD = 1,  // player has an avatar in the creature stage
    PF_MOVING = 2,
    PF_RUNNING = 4,
    PF_JUMPING = 8,
    PF_DEAD = 16,
    PF_SWIMMING = 32,
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
};
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

// HELLO / WELCOME extension (BUG-0219, 0.7.4): appended after the 0.7.2 fields, which ignore
// trailing bytes, so protocol 7 stays: `u32 caps`, then the payload of each set bit in ascending bit
// order. A peer without the extension (0.7.2.x, 0.7.3.0, fakepeer) reads as caps 0. A new field takes
// the next free bit and appends its payload after those of the lower bits; never insert a field
// before the extension. Unknown bits and trailing bytes are ignored.
enum Caps : uint32_t {
    CAP_SPLIT = 1, // joins split reliable messages (BUG-0034 receiver); no payload
    CAP_TOKEN = 2, // HELLO only: u64 rejoin token (BUG-0235); never in WELCOME or anything relayed
};
constexpr uint32_t kMyCaps = CAP_SPLIT; // what this build supports (the token bit is per HELLO)

struct Hello {
    uint16_t proto = kProtoVersion;
    uint32_t build = 0; // first 4 bytes of exe sha256
    std::string name;
    bool ext = true;           // write the extension (false: a 0.7.2-shaped HELLO, tests)
    uint32_t caps = kMyCaps;   // read: 0 = an older peer
    bool has_token = false;
    uint64_t token = 0;
    void write(Writer& w) const
    {
        w.u16(proto); w.u32(build); w.str8(name);
        if (!ext)
            return;
        uint32_t c = (caps & ~(uint32_t)CAP_TOKEN) | (has_token ? CAP_TOKEN : 0);
        w.u32(c);
        if (has_token)
            w.raw(&token, 8);
    }
    bool read(Reader& r)
    {
        proto = r.u16(); build = r.u32(); name = r.str8();
        if (!r.ok())
            return false;
        caps = 0;
        has_token = false;
        token = 0;
        ext = r.left() >= 4;
        if (ext)
            caps = r.u32();
        if ((caps & CAP_TOKEN) && r.left() >= 8) { // a short payload: no token, not a failed read
            r.raw(&token, 8);
            has_token = true;
        }
        return true;
    }
};

struct PlayerInfo {
    uint8_t id = 0;
    std::string name;
};

struct Welcome {
    uint8_t your_id = 0;
    uint32_t world = 0;
    std::vector<PlayerInfo> players;
    bool ext = true;         // write the extension (false: a 0.7.2-shaped WELCOME, tests)
    uint32_t caps = kMyCaps; // the host's (read: 0 = an older host); no payloads in WELCOME
    void write(Writer& w) const
    {
        w.u8(your_id);
        w.u32(world);
        w.u8((uint8_t)players.size());
        for (auto& p : players) { w.u8(p.id); w.str8(p.name); }
        if (ext)
            w.u32(caps & ~(uint32_t)CAP_TOKEN);
    }
    bool read(Reader& r)
    {
        your_id = r.u8();
        world = r.u32();
        uint8_t n = r.u8();
        players.clear();
        for (int i = 0; i < n && r.ok(); i++) {
            PlayerInfo p;
            p.id = r.u8();
            p.name = r.str8();
            players.push_back(p);
        }
        if (!r.ok())
            return false;
        ext = r.left() >= 4;
        caps = ext ? r.u32() & ~(uint32_t)CAP_TOKEN : 0;
        return true;
    }
};

struct Reject {
    uint8_t reason = 0;
    std::string text;
    void write(Writer& w) const { w.u8(reason); w.str8(text); }
    bool read(Reader& r) { reason = r.u8(); text = r.str8(); return r.ok(); }
};

struct PlayerJoin {
    PlayerInfo p;
    void write(Writer& w) const { w.u8(p.id); w.str8(p.name); }
    bool read(Reader& r) { p.id = r.u8(); p.name = r.str8(); return r.ok(); }
};

struct PlayerLeave {
    uint8_t id = 0, reason = 0;
    void write(Writer& w) const { w.u8(id); w.u8(reason); }
    bool read(Reader& r) { id = r.u8(); reason = r.u8(); return r.ok(); }
};

struct PlayerState {
    uint8_t id = 0;
    uint32_t time_ms = 0; // sender clock
    Vec3 pos;
    Quat rot;
    Vec3 vel;
    uint8_t flags = 0;
    uint8_t anim = 0;
    float health = 0, max_health = 0;
    uint32_t species = 0; // instance id of species key (appearance hint)
    uint32_t mode = 0;    // game mode id (Creature 0x1654C01, Space 0x1654C05, ...)
    uint32_t world = 0;   // crc32 of the current save name (same world <=> same value)

    void write(Writer& w) const
    {
        w.u8(id); w.u32(time_ms);
        w.f32(pos.x); w.f32(pos.y); w.f32(pos.z);
        w.f32(rot.x); w.f32(rot.y); w.f32(rot.z); w.f32(rot.w);
        w.f32(vel.x); w.f32(vel.y); w.f32(vel.z);
        w.u8(flags); w.u8(anim);
        w.f32(health); w.f32(max_health);
        w.u32(species);
        w.u32(mode); w.u32(world);
    }
    bool read(Reader& r)
    {
        id = r.u8(); time_ms = r.u32();
        pos.x = r.f32(); pos.y = r.f32(); pos.z = r.f32();
        rot.x = r.f32(); rot.y = r.f32(); rot.z = r.f32(); rot.w = r.f32();
        vel.x = r.f32(); vel.y = r.f32(); vel.z = r.f32();
        flags = r.u8(); anim = r.u8();
        health = r.f32(); max_health = r.f32();
        species = r.u32();
        mode = r.u32(); world = r.u32();
        return r.ok();
    }
};

struct Chat {
    uint8_t id = 0;
    std::string text;
    void write(Writer& w) const { w.u8(id); w.str16(text); }
    bool read(Reader& r) { id = r.u8(); text = r.str16(); return r.ok(); }
};

enum NpcFlags : uint8_t {
    NF_DEAD = 1,
    NF_FAMILY = 2, // member of the host avatar's herd
    NF_IMPRESSED = 4, // impressed by a player (cCreatureAnimal +0xB58 bit 0x80, set by a social success)
};

struct NpcSpawn {
    uint16_t net_id = 0;
    uint32_t key[3] = {0, 0, 0}; // species ResourceKey (instance, type, group)
    float scale = 1;
    uint8_t flags = 0;
    Vec3 pos;
    Quat rot;
    float health = 0, max_health = 0;
    void write(Writer& w) const
    {
        w.u16(net_id); w.u32(key[0]); w.u32(key[1]); w.u32(key[2]); w.f32(scale); w.u8(flags);
        w.f32(pos.x); w.f32(pos.y); w.f32(pos.z); w.f32(rot.x); w.f32(rot.y); w.f32(rot.z); w.f32(rot.w);
        w.f32(health); w.f32(max_health);
    }
    bool read(Reader& r)
    {
        net_id = r.u16(); key[0] = r.u32(); key[1] = r.u32(); key[2] = r.u32(); scale = r.f32(); flags = r.u8();
        pos.x = r.f32(); pos.y = r.f32(); pos.z = r.f32(); rot.x = r.f32(); rot.y = r.f32(); rot.z = r.f32(); rot.w = r.f32();
        health = r.f32(); max_health = r.f32();
        return r.ok();
    }
};

// one entry of an MSG_NPC_STATE batch: u32 time_ms, u8 count, entries...
struct NpcState {
    uint16_t net_id = 0;
    Vec3 pos;
    Quat rot;
    Vec3 vel;
    float health = 0;
    uint8_t flags = 0;
    static constexpr size_t kSize = 2 + 12 + 16 + 12 + 4 + 1;
    void write(Writer& w) const
    {
        w.u16(net_id); w.f32(pos.x); w.f32(pos.y); w.f32(pos.z);
        w.f32(rot.x); w.f32(rot.y); w.f32(rot.z); w.f32(rot.w);
        w.f32(vel.x); w.f32(vel.y); w.f32(vel.z); w.f32(health); w.u8(flags);
    }
    bool read(Reader& r)
    {
        net_id = r.u16(); pos.x = r.f32(); pos.y = r.f32(); pos.z = r.f32();
        rot.x = r.f32(); rot.y = r.f32(); rot.z = r.f32(); rot.w = r.f32();
        vel.x = r.f32(); vel.y = r.f32(); vel.z = r.f32(); health = r.f32(); flags = r.u8();
        return r.ok();
    }
};

struct DamageReq { // guest -> host
    uint16_t net_id = 0;
    float damage = 0;
    int32_t type = 0;
    void write(Writer& w) const { w.u16(net_id); w.f32(damage); w.u32((uint32_t)type); }
    bool read(Reader& r) { net_id = r.u16(); damage = r.f32(); type = (int32_t)r.u32(); return r.ok(); }
};

struct Damage { // host -> guest: you were hit
    uint8_t player = 0;
    float damage = 0;
    int32_t type = 0;
    uint16_t attacker = 0xFFFF; // NPC net id or 0xFFFF
    void write(Writer& w) const { w.u8(player); w.f32(damage); w.u32((uint32_t)type); w.u16(attacker); }
    bool read(Reader& r) { player = r.u8(); damage = r.f32(); type = (int32_t)r.u32(); attacker = r.u16(); return r.ok(); }
};

struct AnimEvent {
    uint8_t id = 0;
    uint32_t anim = 0;
    int32_t block = -1;
    uint8_t flags = 0; // 1 = disable blend-in, 2 = was PlayAnimationTo (had a target)
    void write(Writer& w) const { w.u8(id); w.u32(anim); w.u32((uint32_t)block); w.u8(flags); }
    bool read(Reader& r) { id = r.u8(); anim = r.u32(); block = (int32_t)r.u32(); flags = r.u8(); return r.ok(); }
};

struct WorldOffer {
    uint32_t world_id = 0; // crc32 of all files
    uint32_t total_size = 0;
    uint16_t file_count = 0;
    std::string planet; // display name
    void write(Writer& w) const { w.u32(world_id); w.u32(total_size); w.u16(file_count); w.str8(planet); }
    bool read(Reader& r) { world_id = r.u32(); total_size = r.u32(); file_count = r.u16(); planet = r.str8(); return r.ok(); }
};

struct WorldFile {
    uint16_t index = 0;
    uint32_t size = 0;
    std::string path; // relative, '/' separated
    void write(Writer& w) const { w.u16(index); w.u32(size); w.str8(path); }
    bool read(Reader& r) { index = r.u16(); size = r.u32(); path = r.str8(); return r.ok(); }
};

struct WorldChunk {
    uint16_t index = 0;
    uint32_t offset = 0;
    std::vector<uint8_t> data;
    void write(Writer& w) const { w.u16(index); w.u32(offset); w.u16((uint16_t)data.size()); w.raw(data.data(), data.size()); }
    bool read(Reader& r)
    {
        index = r.u16(); offset = r.u32();
        uint16_t n = r.u16();
        if (!r.ok() || r.left() < n) return false;
        data.assign(r.cur(), r.cur() + n);
        r.skip(n);
        return r.ok();
    }
};

uint32_t crc32_update(uint32_t crc, const uint8_t* p, size_t n);

} // namespace net
