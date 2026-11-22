// SporeCoop wire protocol: message ids and payload encoders/decoders (portable).
// See docs/04-netcode.md.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "buffer.h"

namespace net {

constexpr uint32_t kMagic = 0x4F435053; // 'SPCO'
constexpr uint16_t kProtoVersion = 7; // 3: shared resources, ping marks; 4: lobby, LAN discovery; 5: 128-packet acks;
                                      // 6: connection id in the header, unreliable BYE; 7: space features (space_ext.h)
constexpr uint16_t kDefaultPort = 47810;
// The ceiling of players in one session (ids 0..kMaxPlayers-1, the host 0). Owner 2026-11-18 (decision 6E B, alpha):
// "It needs to be scalable too. Setting it to 17 shouldn't cause any issues": every table sizes itself from this one
// number and the players a session takes are a setting (all.max_players, galaxy planets 16 by default). Ids from 0xFC
// up are markers (kNobody 0xFF, kNoOwner 0xFE, kOtherSeat 0xFC): the ceiling stays below them.
constexpr int kMaxPlayers = 64;
static_assert(kMaxPlayers >= 2 && kMaxPlayers <= 0xFB, "player ids are one byte, 0xFC and up are markers");
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
    MSG_DNA = 23,         // host -> guests: shared DNA total (f32), then (optional) u8 n x {u8 cause, u8 player, f32 amount} (dna_ledger.h)
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
    // decision 6F A (alpha): the compact MSG_CELL_STATE, to guests with CAP_COMPACT_CELL (cell_compact.h)
    MSG_CELL_STATE_C = 95,
    MSG_PARTY = 96,   // reliable (decision 6J): parties of 4 (parties_net.h)
    MSG_WHISPER = 97, // reliable (decision 6J): guest -> host u8 to, str16 text; host -> guest u8 from, str16 text
    MSG_TRIBE_VILLAGES = 98, // unreliable host -> guests (W4, BUG-9271): MSG_TRIBE_STATE's village table on its own when it
                             // does not fit beside the members: u16 cycle, u8 part, u8 parts, u8 total, u8 first, u8 count,
                             // u8 flags (1 species info, 2 native chiefs), count x {f32 x3, u8 flags, [u32 species, f32 food]},
                             // [count x {u32 x3 key, u8 archetype}], [count x u16 chief]; the state's chunks then say 0xFF
    MSG_CELL_HIT = 34,    // guest -> host: u32 host cell index, u8 damage, u8 type
    MSG_CELL_EAT = 35,    // guest -> host: u32 host cell index (eaten / destroyed by the guest)
    MSG_CELL_FOOD = 36,   // host -> guests: i32 food, i32 plant | guest -> host: i32 delta, u8 plant
    MSG_CIV_STATE = 37,   // host -> guests: cities (owners, political ids) + vehicles snapshot (chunked)
    MSG_CIV_CMD = 38,     // guest -> host: vehicle order / move / purchase / building
    MSG_CIV_BUILD = 39,   // host -> guests: a building appeared in a city (city index, noun, position)
    MSG_UFO_STATE = 40,   // host -> guests in its place: NPC ships snapshot (chunked)
    MSG_UFO_HIT = 41,     // guest -> host: u16 ship id, f32 damage, i32 type, vec3 dir
    MSG_UFO_DAMAGE = 42,  // host -> guest: f32 damage, i32 type, vec3 dir, u16 attacker ship id
    MSG_VOTE = 43,        // host -> guests: u8 active, u8 seconds left, str8 text (evolution vote), u32 editor, u8 flags (1: an
                          // evolve offer with active 0, BUG-0516; older senders stop before the flags)
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
    MSG_NEST = 52,        // u8 kind: 0 host -> guest eggs at the NPC nests near it (unreliable, 1 Hz) {u8 n, n x
                          // {u32 id, vec3 pos, u8 state, f32 scale}}; 1 guest -> host u32 id: the guest ate it (nest.cpp)
    MSG_PARTS = 53,       // the party's collected creature parts (parts.cpp, BUG-0401): u8 kind (0 host -> guests the
                          // host's set, 1 host -> guests news, 2 guest -> host found here), u8 origin, u32 world,
                          // u16 n, n x {u32 lo, u32 hi}. Older SporeCoop ignores it (an unknown type).
    MSG_FIELDS = 56,      // host -> guests: replicated state fields of mirrored objects (fields.h, BUG-0419 G2):
                          // u8 kind, u8 nf, nf x u8 field, u16 n, n x {u16 id, nf x f32}; unreliable, deltas + refresh.
                          // Older SporeCoop ignores it (an unknown type).
    MSG_NPC_RESYNC = 57,  // guest -> host: u8 n, n x u16 NPC id the guest gets states for and has no spawn of
                          // (BUG-0487 / G5): the host sends that spawn again. Older SporeCoop ignores it.
    MSG_NPC_ANIM = 59,    // host -> guest: animations the host's NPCs' behaviour trees started (BUG-0464, npc.cpp):
                          // u8 kind 0 (started, at most once a frame): u8 n, n x {u16 NPC id, u32 anim, u8 how (0 CreaturePlayAnim,
                          // 1 PlayAnimation, 2 PlayAnimationTo), u8 arg, u32 arg2};
                          // kind 1 (1 Hz: what they play now, a BT's animation or a body's): u8 n, n x {u16 id, u32 anim}.
                          // Unreliable.
                          // Older SporeCoop ignores it (an unknown type).
    MSG_WOBJ = 54,        // world objects owned by the host (worldobj.cpp, BUG-0472 / BUG-0459): u8 kind (0 host -> guest
                          // snapshot {u8 obj kind, vec3 centre, f32 r, u8 n, n x {u32 id, vec3 pos, u32 state}}, unreliable 1 Hz;
                          // 1 guest -> host request {u8 obj kind, u32 id, vec3 pos, u32 state now}). Older SporeCoop ignores it.
    MSG_CELL_KO = 44,     // any -> host -> all: u8 player, u8 state (0 up,1 out,2 revived,3 respawned),
                          // u8 tenths of a second left, u8 revive progress 0..255
    MSG_CREATURE_RESCUE = 73, // u32 world, u32 knockout number, u8 event; only the host sends results
    MSG_TRIBE_BUILDINGS = 55, // host -> guest: u32 world/sequence, u8 count, {id/noun/faction, pos, hp/maxhp}
    MSG_TRIBE_EQUIPMENT = 74, // host -> guest: u32 world/sequence, u8 count, {u16 citizen, u8 tool/item}
    MSG_TRIBE_TOOLS = 75, // host -> guest: complete per-village rack inventory, transforms and health
    MSG_TRIBE_RELATIONSHIPS = 58, // host -> guest: world/sequence, ordered faction pair, flags, native event values
    MSG_TRIBE_FOOD = 76, // host -> guest: NPC stores, bounded by world/faction/species/position
    MSG_TRIBE_PERFORMANCE = 77, // host -> guest: world/sequence, up to64 {member, native token, phase/request/ready, progress, round/total}
    MSG_TRIBE_OUTFIT = 78, // reliable: u8 begin/result/accepted, u32 world/token/previous/model; voted founders only
    MSG_CIV_TURRETS = 90,  // host -> guest: one city wall layout, occupied slots and turret HP
    MSG_CELL_COMBAT = 89, // guest contact hit; host replies with any whole bonus for the same body
    MSG_CAMPAIGN_RULES = 88, // reliable host snapshot scoped to campaign name hash and stage
    MSG_CARDS = 110, // reliable (BUG-9306, 7Q A): u8 kind; 0 guest -> host u8 stage, u32 its own new card; 1 host -> guest u8 mask, 5 x u32 its record (cards.h)
    MSG_IDENTITY = 83, // reliable guest -> host (BUG-9135): u64 the player's persistent identity (identity.h)
    MSG_TRIBE_LATE = 84, // reliable host -> guest (BUG-9159): u8 kind (1: make your creature in the editor now), u32 world
    MSG_DNA_OWN = 85, // reliable host -> guest (BUG-9162): f32 DNA of that guest's own (its kill paid on the host's game)
    MSG_START_PARTS = 87, // reliable host -> guests (3Z B): u16 n, n x {u32 key lo, u32 key hi, u8 state}: the part states of
                          // the parts list of the host's new-world start editor (its diet's mouth unlocked)
    MSG_MIGRATE = 86, // reliable (BUG-9163): u8 kind 1 (a migration started), u8 the player whose evolution started it, u32 world
    MSG_DRAMA = 82, // reliable host -> guests (BUG-9131): u8 Drama Manager event kind the host started (meteor, rain)
    MSG_PROGRESS_DEBT = 81, // reliable host -> guests (BUG-9119): u32 stage mode, u32 world, f32 the party's progress debt
    MSG_TRIBE_COMBAT = 80, // reliable host snapshot: world, count, {member id, scaled native max HP}
    MSG_SPACE_LAUNCH = 79, // reliable host offer and founder reply for a missed Civilization launch
    MSG_CIV_ECONOMY = 91, // host city routes and economic meter; civ_economy.h (BUG-9085: 64 belongs to Tribe)
    MSG_CIV_INFLUENCE = 92, // host native religious followers and meter; civ_influence.h
    MSG_HOST_RESTART = 94, // reliable host -> guests (BUG-9239, decision 4S): u16 seconds; the host restarts Spore for
                           // its memory and hosts the same world again: the guests rejoin by themselves meanwhile
    MSG_NPC_STATE_C = 93, // unreliable host -> guest (BUG-9235): MSG_NPC_STATE's batch in the compact form: u32 time_ms,
                          // u8 n, vec3 anchor, n x write_npc_c (23 B), then n x i16 height trailer
    MSG_ADVENTURE = 70, // reliable mission/run-scoped objective proposals and ordered host credits
    MSG_LIBRARY = 71, // optional CAP_LIBRARY persistent creation PNG exchange
    MSG_ADVENTURE_ACTORS = 72, // unreliable host -> guest: scenario creatures' states (adventure_sync.h ActorBatch, BUG-9114)
    MSG_DIAG = 60,        // support diagnostics (net/diagmsg.h): u8 kind, payload; session clock, bug
                          // marks, sync self-check. Older SporeCoop ignores it (an unknown type).
    MSG_EFFECT = 61,      // a player's own creature started / stopped a visual effect (BUG-0451,
                          // effects.h): u8 player, u8 stop, u8 pool (1/2), u32 effect id, u32 key.
                          // The host relays a guest's message to the other guests; the receiver
                          // plays it on that player's puppet with the game's own effect pools.
                          // Older SporeCoop ignores it (an unknown type).
    MSG_PLAYER_RULE = 63, // the game's rules for a player that one game decides on the other player's creature
                          // (player_rules.cpp, BUG-0418 / P1 of 0.7.7): u8 kind; kind 0 host -> guest "stunned"
                          // {u8 player, f32 seconds} (an NPC's charge stunned its puppet: CreatureStun vt 0x94),
                          // kind 1 guest -> host {u16 NPC id, f32 seconds} (the guest's charge stunned a proxy).
                          // Older SporeCoop ignores it (an unknown type).
    MSG_MIRROR_FX = 64,   // host -> guests: what the host's game did to a creature a guest mirrors (mirror_fx.h, BUG-0600):
                          // u8 kind; 0 effect {u8 space (0 NPC, 1 tribe member), u16 id, u8 stop, u8 pool, u32 effect,
                          // u32 key}, 1 combat voice {space, id, u8 enter}, 2 sprint {space, id, u8 on}.
                          // Older SporeCoop ignores it (an unknown type).
    MSG_NPC_MEAT = 65,    // the meat left on bodies of killed NPCs (BUG-0557 / BUG-0383, npc.cpp): u8 kind; 0 host -> guest
                          // {u8 n, n x {u16 NPC id, f32 meat}} (unreliable, on a change and every 2 s), 1 guest -> host
                          // {u8 n, n x {u16 NPC id, f32 eaten}} (reliable: what the guest's creature ate of its proxy).
                          // Older SporeCoop ignores it (an unknown type).
    MSG_WSTATE = 62,      // host -> guests: host-owned world state rows (wsync.h, BUG-0419 / BUG-0565 / BUG-0577 /
                          // BUG-0579 / BUG-0564): u8 kind 0, u32 host mode, u8 rows, {u8 id, u8 nv, u8 n, n x {u32 key,
                          // nv x u32}}. Older SporeCoop ignores it (an unknown type).
    MSG_SETTINGS = 66,    // host -> guests (reliable): the session settings (settings_core.h, F3 of 0.7.12): u8 kind 0,
                          // u8 flags (1: the host plays its world), u8 n, n x {str8 key, u32 value}; on a join and on
                          // every change. Older SporeCoop ignores it (an unknown type).
    MSG_COLORS = 67,      // the player colours (find_core.h, F2 of 0.7.12): u8 kind; 0 host -> guests (reliable) the table
                          // u8 n, n x {u8 id, u8 colour}; 1 guest -> host u8 colour (the guest asks for it). Older
                          // SporeCoop ignores it (an unknown type).
    MSG_REPORT = 68,      // the session report (net/sreport.h, F4 of 0.7.12, BUG-0767): u8 kind; host -> guest the
                          // question / a stop, guest -> host the answer and its report zip in pieces (reliable).
                          // Older SporeCoop ignores it (an unknown type).
    MSG_HOST_WORLD = 69,  // host -> guests (reliable): u32 world, the world id of WELCOME again (host_world.h welcome_world:
                          // the CRC of the host's save name), sent when the host's save changes during the session and
                          // after a join (BUG-0933). Older SporeCoop ignores it (an unknown type).
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
    // BUG-0210: an appended id of the hosted game (random per host start), so the answers of one
    // host from its two addresses (cable and Wi-Fi) are one game. Older readers ignore the bytes
    // (read() does not ask for the end); an older host sends none (has_id false).
    bool has_id = false;
    uint32_t host_id = 0;
    int scaling = -1; // older replies do not advertise rules
    uint8_t settings = 0; // bit 0: paced transfer, bit 1: password required
    void write(Writer& w) const
    {
        w.u32(kDiscoverReply); w.u16(proto); w.u16(port); w.str8(name); w.u8(players); w.u8(max_players); w.str8(info);
        if (has_id) w.u32(host_id);
        if (has_id && scaling >= 0) { w.u16((uint16_t)scaling); w.u8(settings); }
    }
    bool read(Reader& r)
    {
        if (r.u32() != kDiscoverReply) return false;
        proto = r.u16(); port = r.u16(); name = r.str8(); players = r.u8(); max_players = r.u8(); info = r.str8();
        has_id = r.ok() && r.left() >= 4;
        host_id = has_id ? r.u32() : 0;
        scaling = -1;
        settings = 0;
        if (has_id && r.left() >= 3) { scaling = r.u16(); settings = r.u8(); }
        return r.ok();
    }
};

enum RejectReason : uint8_t {
    REJ_VERSION = 1,
    REJ_FULL = 2,
    REJ_BUILD = 3,
    REJ_STARTED = 4, // F3 all.late_join off: the host plays its world already (an older guest shows the text)
};

enum PlayerFlags : uint16_t { // Lineages + upstream 0.7.14: 9 flags (u16 on the wire)
    PF_IN_WORLD = 1,  // player has an avatar in the creature stage
    PF_MOVING = 2,
    PF_RUNNING = 4,
    PF_JUMPING = 8,
    PF_DEAD = 16,
    PF_SWIMMING = 32,
    PF_SNEAKING = 64, // BUG-0463: the creature sneaks (+0xBB1); older SporeCoop ignores the bit
    PF_DOWNED = 128,  // Lineages: knocked down (creature_rescue), 64 before the 0.7.8 merge
    PF_RESYNC = 256,  // BUG-0332 (upstream 128): the host loads the shared world once for everybody (tribe.host_reload)
    PF_PAUSED = 512,  // BUG-9227 (Lineages): the host's game time stands (its pause, a caption) for a second or more
};

// BUG-0425: a height over the sender's terrain (m) that is not known (an older sender, no terrain)
constexpr float kNoAgl = -1e9f;
inline bool agl_known(float agl) { return agl > -1e6f; }

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
    // the versions of the game and the mod (support logs, 0.7.4): HELLO: str8 "coop X, SporeKit Y, <build>";
    // WELCOME: str8 of the host, u8 n, n x (u8 player id, str8 version) of the players the host knows
    CAP_VERSION = 4,
    CAP_PERSONAL_LAUNCH = 16, // HELLO: this guest understands the optional Civilization launch offer
    CAP_CAMPAIGN_PROFILE = 8, // str8: fork rules and required content
    CAP_RESUME_WORLD = 64, // HELLO only: u64 receipt for a still-loaded world
    CAP_JOIN_PROOF = 128, // HELLO only: connection-bound password proof
    CAP_LIBRARY = 32, // persistent, paced creation-library exchange (MSG_LIBRARY)
    CAP_COMPACT_CELL = 256, // 6F A: this guest reads MSG_CELL_STATE_C; no payload
    CAP_FAN_IN = 512, // 6F B: this guest takes the host's messages copied by the galaxy server (net/galaxy.h kFan)
};
constexpr uint32_t kMyCaps = CAP_SPLIT | CAP_VERSION | CAP_LIBRARY | CAP_COMPACT_CELL | CAP_FAN_IN; // token bit is per HELLO

struct Hello {
    uint16_t proto = kProtoVersion;
    uint32_t build = 0; // first 4 bytes of exe sha256
    std::string name;
    bool ext = true;           // write the extension (false: a 0.7.2-shaped HELLO, tests)
    uint32_t caps = kMyCaps;   // read: 0 = an older peer
    bool has_token = false;
    uint64_t token = 0;
    uint64_t resume_world = 0;
    std::string version; // CAP_VERSION ("" read: an older peer)
    std::string campaign_profile;
    std::string join_proof;
    void write(Writer& w) const
    {
        w.u16(proto); w.u32(build); w.str8(name);
        if (!ext)
            return;
        uint32_t c = (caps & ~(uint32_t)CAP_TOKEN) | (has_token ? CAP_TOKEN : 0);
        if (!campaign_profile.empty())
            c |= CAP_CAMPAIGN_PROFILE;
        c &= ~(uint32_t)CAP_RESUME_WORLD;
        if (resume_world) c |= CAP_RESUME_WORLD;
        if (!join_proof.empty()) c |= CAP_JOIN_PROOF;
        w.u32(c);
        if (has_token)
            w.raw(&token, 8);
        if (c & CAP_VERSION)
            w.str8(version);
        if (c & CAP_CAMPAIGN_PROFILE)
            w.str8(campaign_profile);
        if (resume_world) w.raw(&resume_world, sizeof resume_world);
        if (!join_proof.empty()) w.str8(join_proof);
    }
    bool read(Reader& r)
    {
        proto = r.u16(); build = r.u32(); name = r.str8();
        if (!r.ok())
            return false;
        caps = 0;
        has_token = false;
        token = 0;
        resume_world = 0;
        ext = r.left() >= 4;
        if (ext)
            caps = r.u32();
        if ((caps & CAP_TOKEN) && r.left() >= 8) { // a short payload: no token, not a failed read
            r.raw(&token, 8);
            has_token = true;
        }
        version.clear();
        campaign_profile.clear();
        if ((caps & CAP_VERSION) && r.left() >= 1) {
            Reader v = r; // a bad payload leaves the HELLO readable
            std::string s = v.str8();
            if (v.ok()) {
                version = s;
                r = v;
            }
        }
        if (caps & CAP_CAMPAIGN_PROFILE) {
            campaign_profile = r.str8();
            if (!r.ok() || campaign_profile.empty()) return false;
        }
        if ((caps & CAP_RESUME_WORLD) && r.left() >= sizeof resume_world)
            r.raw(&resume_world, sizeof resume_world);
        join_proof.clear();
        if (caps & CAP_JOIN_PROOF) {
            join_proof = r.str8();
            if (!r.ok() || join_proof.size() != 64) return false;
        }
        return true;
    }
};

struct PlayerInfo {
    uint8_t id = 0;
    std::string name;
    std::string version; // CAP_VERSION text, "" = unknown (an older SporeCoop)
};

struct Welcome {
    uint8_t your_id = 0;
    uint32_t world = 0;
    std::vector<PlayerInfo> players;
    bool ext = true;         // write the extension (false: a 0.7.2-shaped WELCOME, tests)
    uint32_t caps = kMyCaps; // the host's (read: 0 = an older host)
    std::string host_version; // CAP_VERSION (read: "" = an older host); the players' in `players`
    std::string campaign_profile;
    void write(Writer& w) const
    {
        w.u8(your_id);
        w.u32(world);
        w.u8((uint8_t)players.size());
        for (auto& p : players) { w.u8(p.id); w.str8(p.name); }
        if (!ext)
            return;
        uint32_t c = caps & ~(uint32_t)CAP_TOKEN;
        if (!campaign_profile.empty())
            c |= CAP_CAMPAIGN_PROFILE;
        w.u32(c);
        if (caps & CAP_VERSION) {
            w.str8(host_version);
            uint8_t n = 0;
            for (auto& p : players)
                n += !p.version.empty() && p.id != 0;
            w.u8(n);
            for (auto& p : players)
                if (!p.version.empty() && p.id != 0) {
                    w.u8(p.id);
                    w.str8(p.version);
                }
        }
        if (c & CAP_CAMPAIGN_PROFILE)
            w.str8(campaign_profile);
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
        host_version.clear();
        campaign_profile.clear();
        if ((caps & CAP_VERSION) && r.left() >= 1) {
            Reader v = r; // a bad payload leaves the WELCOME readable
            std::string hv = v.str8();
            int n = v.u8();
            std::vector<std::pair<uint8_t, std::string>> got;
            for (int i = 0; i < n && v.ok(); i++) {
                uint8_t id = v.u8();
                std::string s = v.str8();
                got.push_back({id, s});
            }
            if (v.ok()) {
                r = v;
                host_version = hv;
                for (auto& p : players) {
                    if (p.id == 0)
                        p.version = hv;
                    for (auto& [id, s] : got)
                        if (id == p.id && id != 0)
                            p.version = s;
                }
            }
        }
        if (caps & CAP_CAMPAIGN_PROFILE) {
            campaign_profile = r.str8();
            return r.ok() && !campaign_profile.empty();
        }
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
    // the version (0.7.4) after the 0.7.2 fields: older readers ignore it, "" from an older host
    void write(Writer& w) const { w.u8(p.id); w.str8(p.name); w.str8(p.version); }
    bool read(Reader& r)
    {
        p.id = r.u8(); p.name = r.str8();
        if (!r.ok())
            return false;
        p.version.clear();
        if (r.left() >= 1) {
            Reader v = r;
            std::string s = v.str8();
            if (v.ok()) {
                p.version = s;
                r = v;
            }
        }
        return true;
    }
};

struct PlayerLeave {
    uint8_t id = 0, reason = 0;
    void write(Writer& w) const { w.u8(id); w.u8(reason); }
    bool read(Reader& r) { id = r.u8(); reason = r.u8(); return r.ok(); }
};

struct AdventureGate {
    uint32_t visit = 0, act = 0, target = 0, credit = 0;
};

struct PlayerState {
    uint8_t id = 0;
    uint32_t time_ms = 0; // sender clock
    Vec3 pos;
    Quat rot;
    Vec3 vel;
    uint16_t flags = 0;
    uint8_t anim = 0;
    float health = 0, max_health = 0;
    uint32_t species = 0; // instance id of species key (appearance hint)
    uint32_t mode = 0;    // game mode id (Creature 0x1654C01, Space 0x1654C05, ...)
    uint32_t world = 0;   // crc32 of the current save name (same world <=> same value)
    AdventureGate gate;   // optional Duo extension for a single move-to objective (Lineages)
    float captain_scale = 0; // Lineages (Adventure, BUG-9113): the captain's own scale; 0 = not sent
    uint32_t editor = 0;     // Lineages (the Party panel, 2026-11-13): the editor open now (EditorRequest+0xC); 0 = none
    // Lineages (decision 3G, the Party cards' second bar): Creature the hunger meter, Space the ship's energy, as 0..1;
    // below 0 = none. Written after captain_scale (every Lineages peer has the same build)
    float energy = -1;
    // BUG-0519: the sender's Cell world scale (G+0x514C) the pos / vel / size are in; 0 = not in Cell
    // or an older sender (the field is appended: older readers stop before it)
    float scale = 0;
    // BUG-0425: the sender's height over its own terrain (cPlanetModel::GetHeightAt) when its body is off the
    // ground (PF_JUMPING); kNoAgl otherwise or from an older sender (appended after scale: older readers stop
    // before it). NPC states carry it in the MSG_NPC_STATE batch's trailer (npc.cpp), not here
    float agl = kNoAgl;
    // BUG-0733: the sender's own creature's size (cSpatialObject GetScale) in the Creature stage; 0 = not sent (another
    // stage, or an older sender: appended after agl, older readers stop before it)
    float body = 0;

    void write(Writer& w) const
    {
        w.u8(id); w.u32(time_ms);
        w.f32(pos.x); w.f32(pos.y); w.f32(pos.z);
        w.f32(rot.x); w.f32(rot.y); w.f32(rot.z); w.f32(rot.w);
        w.f32(vel.x); w.f32(vel.y); w.f32(vel.z);
        w.u16(flags); w.u8(anim);
        w.f32(health); w.f32(max_health);
        w.u32(species);
        w.u32(mode); w.u32(world);
        w.f32(scale);
        w.f32(agl);
        w.f32(body);          // upstream 0.7.14
        w.f32(captain_scale); // Lineages: after the 0.7.8 fields (every Lineages peer has the same build)
        w.f32(energy);        // Lineages (3G)
        if (editor) w.u32(editor); // Lineages (BUG-9191): only while in an editor (no gate then): 4 bytes after energy
        if (gate.visit || gate.credit) {
            w.u32(gate.visit); w.u32(gate.act); w.u32(gate.target); w.u32(gate.credit);
        }
    }
    bool read(Reader& r)
    {
        id = r.u8(); time_ms = r.u32();
        pos.x = r.f32(); pos.y = r.f32(); pos.z = r.f32();
        rot.x = r.f32(); rot.y = r.f32(); rot.z = r.f32(); rot.w = r.f32();
        vel.x = r.f32(); vel.y = r.f32(); vel.z = r.f32();
        flags = r.u16(); anim = r.u8();
        health = r.f32(); max_health = r.f32();
        species = r.u32();
        mode = r.u32(); world = r.u32();
        scale = r.ok() && r.left() >= 4 ? r.f32() : 0.f;
        agl = r.ok() && r.left() >= 4 ? r.f32() : kNoAgl;
        body = r.ok() && r.left() >= 4 ? r.f32() : 0.f;
        captain_scale = 0;
        if (r.ok() && r.left() >= 4) {
            float s = r.f32();
            captain_scale = s > 0.01f && s < 100.f ? s : 0.f; // a creature's scale; anything else is not one
        }
        energy = -1;
        if (r.ok() && r.left() >= 4) {
            const float e = r.f32();
            energy = e >= 0 && e <= 1 ? e : -1.f;
        }
        editor = r.ok() && (r.left() == 4 || r.left() == 20) ? r.u32() : 0;
        gate = {};
        if (r.ok() && r.left() == 16)
            gate = {r.u32(), r.u32(), r.u32(), r.u32()};
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
    NF_SCALED_HP = 64, // Lineages: host campaign combat multiplier applies to this NPC (8 before the 0.7.8 merge)
    NF_DEAD = 1,
    NF_FAMILY = 2, // member of the host avatar's herd
    NF_IMPRESSED = 4, // impressed by a player (cCreatureAnimal +0xB58 bit 0x80, set by a social success)

    NF_PACK = 8,      // in the host's pack (+0xB58 bit 0x100: recruited, a Summon Flock member; BUG-0556). Older guests ignore it
    NF_SUMMONED = 16, // its herd is a summoned flock (herd +0x192, cSummonCreatureEvent: the hearts effect 0x6A17B05A; BUG-0556)
    NF_AIR = 32,      // BUG-0550 / BUG-0425: off its ground in the host's game (+0x137 = 0 and off GetHeightAt: a jump, a fall,
                      // thrown, flying); its height over the host's terrain is in the batch's agl trailer. Older guests ignore it
    NF_ALPHA = 128,   // Lineages: the nest's alpha (cCreatureAnimal +0xB58 bit 0x1: the "Alpha" card, one more social round; BUG-9115)
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

// BUG-9235: the compact NPC state. Positions are offsets from the batch's anchor in 1/64 m (i16: +-500 m), the rotation
// is the smallest-three quaternion in 32 bits (2 bits: the largest component's index, 3 x 10 bits of the others in
// [-1/sqrt2, 1/sqrt2], its sign made positive), the velocity in cm/s (i16), health f32, flags u8: 23 bytes for 47.
constexpr size_t kNpcStateCSize = 2 + 6 + 4 + 6 + 4 + 1;
constexpr float kNpcPosQ = 64.f;
inline bool npc_pos_fits(const Vec3& p, const Vec3& anchor)
{
    const float lim = 32000.f / kNpcPosQ;
    return std::fabs(p.x - anchor.x) < lim && std::fabs(p.y - anchor.y) < lim && std::fabs(p.z - anchor.z) < lim;
}
inline uint32_t pack_quat(Quat q)
{
    float c[4] = {q.x, q.y, q.z, q.w};
    int big = 0;
    for (int i = 1; i < 4; i++)
        if (std::fabs(c[i]) > std::fabs(c[big])) big = i;
    const float sign = c[big] < 0 ? -1.f : 1.f;
    uint32_t out = (uint32_t)big << 30;
    int k = 0;
    for (int i = 0; i < 4; i++) {
        if (i == big) continue;
        float v = std::max(-0.70710678f, std::min(0.70710678f, c[i] * sign));
        uint32_t qv = (uint32_t)std::lround((v + 0.70710678f) / 1.41421356f * 1023.f);
        out |= (qv & 1023u) << (20 - 10 * k);
        k++;
    }
    return out;
}
inline Quat unpack_quat(uint32_t v)
{
    const int big = (int)(v >> 30);
    float c[4] = {};
    float sum = 0;
    int k = 0;
    for (int i = 0; i < 4; i++) {
        if (i == big) continue;
        uint32_t qv = (v >> (20 - 10 * k)) & 1023u;
        c[i] = qv / 1023.f * 1.41421356f - 0.70710678f;
        sum += c[i] * c[i];
        k++;
    }
    c[big] = std::sqrt(std::max(0.f, 1.f - sum));
    Quat q;
    q.x = c[0]; q.y = c[1]; q.z = c[2]; q.w = c[3];
    return q;
}
inline int16_t q16(float v, float scale)
{
    float s = std::round(v * scale);
    return (int16_t)std::max(-32767.f, std::min(32767.f, std::isfinite(s) ? s : 0.f));
}
inline void write_npc_c(Writer& w, const NpcState& st, const Vec3& anchor)
{
    w.u16(st.net_id);
    w.u16((uint16_t)q16(st.pos.x - anchor.x, kNpcPosQ));
    w.u16((uint16_t)q16(st.pos.y - anchor.y, kNpcPosQ));
    w.u16((uint16_t)q16(st.pos.z - anchor.z, kNpcPosQ));
    w.u32(pack_quat(st.rot));
    w.u16((uint16_t)q16(st.vel.x, 100.f));
    w.u16((uint16_t)q16(st.vel.y, 100.f));
    w.u16((uint16_t)q16(st.vel.z, 100.f));
    w.f32(st.health);
    w.u8(st.flags);
}
inline bool read_npc_c(Reader& r, NpcState& st, const Vec3& anchor)
{
    st.net_id = r.u16();
    st.pos.x = anchor.x + (int16_t)r.u16() / kNpcPosQ;
    st.pos.y = anchor.y + (int16_t)r.u16() / kNpcPosQ;
    st.pos.z = anchor.z + (int16_t)r.u16() / kNpcPosQ;
    st.rot = unpack_quat(r.u32());
    st.vel.x = (int16_t)r.u16() / 100.f;
    st.vel.y = (int16_t)r.u16() / 100.f;
    st.vel.z = (int16_t)r.u16() / 100.f;
    st.health = r.f32();
    st.flags = r.u8();
    return r.ok();
}

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
    // BUG-0555 (appended, older readers ignore it): the type of the attacker's ability whose hit it is (+8 of the
    // ability, 0x1E bite, 0x1F charge, 0x20 spit; 0 = not an ability hit / unknown, also from an older host)
    uint32_t ability = 0;
    void write(Writer& w) const { w.u8(player); w.f32(damage); w.u32((uint32_t)type); w.u16(attacker); w.u32(ability); }
    bool read(Reader& r)
    {
        player = r.u8(); damage = r.f32(); type = (int32_t)r.u32(); attacker = r.u16();
        ability = r.ok() && r.left() >= 4 ? r.u32() : 0;
        return r.ok();
    }
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
