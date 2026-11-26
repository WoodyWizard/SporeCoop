// The species line (WP-0 of docs/plan-0.7.16.md, BUG-1270): the portable part. Rules, wire and store format of one
// player's line; species_line.h is the game API every package reads. docs/01-architecture.md#arch-species-line.
//
// A player's line is what its own species is in each stage:
//   * Cell / Creature: its creations (creations.cpp; not in the Row: creations keeps and sends them)
//   * Tribe: its tribe outfit if it made one for its current creature, else its creature
//   * Civ: its Civ outfit if it made one for its current tribe line, else its tribe line
//   * Civ designs: one key per game design id (the culture set's id, cCivilization +0x6C: City Hall 0x99E92F05,
//     house 0x4E3F7777, ..., a vehicle's VehicleDesignId(purpose, locomotion)); none at first (the host's culture key)
//
// A Row holds the owner's side: the creature's instance on the owner's game (`creature`, 0 = it plays the world's
// own), and per outfit / design a Ref: the creation's key on the owner's game (`own`), the shared instance every game
// registers its records under (`shared`, 0x7B......, from the content: the same on every game) and for an outfit the
// owner-side instance it was made for (`made_for`). Rules compare owner-side instances only, so every game decides the
// same from the same Row.
//
// Portable (no Windows, no game): tests/species_tests.cpp runs it natively.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "player_store.h"

namespace species {

enum class Stage : uint8_t { Cell = 0, Creature = 1, Tribe = 2, Civ = 3, Count = 4 };
inline const char* stage_name(Stage s)
{
    switch (s) {
    case Stage::Cell: return "cell";
    case Stage::Creature: return "creature";
    case Stage::Tribe: return "tribe";
    case Stage::Civ: return "civ";
    default: return "?";
    }
}

// the shared instances of line creations: 0x7B + 24 bits of the content hash (creations' own copies are 0x7C player
// creations and 0x7D the host's species and designs)
constexpr uint32_t kSharedPrefix = 0x7B000000u;
inline uint32_t shared_instance(uint32_t content_hash) { return kSharedPrefix | (content_hash & 0x00FFFFFFu); }
inline bool is_shared(uint32_t inst) { return (inst & 0xFF000000u) == kSharedPrefix && (inst & 0x00FFFFFFu) != 0; }

// Cell and Creature come from creations (creations::player_key), not from the Row. What creations has of a player here
// is that player's own (the stage's key) when this game knows the key and it is not the world's own:
//   * Creature: only creations' shared copy (0x7C......, built from the player's records); a key sent without records
//     is the world's own species (a guest that plays the host's creature, BUG-0278) or one of the host's creations
//   * Cell: the shared copy too, and a key every game has without records (a premade cell, a Sporepedia download the
//     player chose), unless it is the host's cell (`hosts`: the world's own; the host's kin are the game's own answer)
constexpr uint32_t kCopyPrefix = 0x7C000000u;
inline bool is_copy(uint32_t inst) { return (inst & 0xFF000000u) == kCopyPrefix && (inst & 0x00FFFFFFu) != 0; }
inline bool creation_counts(Stage s, uint32_t inst, bool known, bool hosts)
{
    if (!inst || !known || hosts)
        return false;
    return is_copy(inst) || s == Stage::Cell;
}

constexpr uint8_t kRowVersion = 1;
constexpr size_t kMaxDesigns = 32;     // design ids per player (the game has 5 building ids and 12 vehicle ids)
constexpr uint32_t kMaxBundle = 4u << 20; // one line creation's bundle on the wire
constexpr const char* kStoreFeature = "species.line"; // the per-player store's record (player_store.h), version 1
constexpr uint16_t kStoreVersion = 1;

struct Ref {
    uint32_t own = 0, type = 0, group = 0; // the creation's key on the owner's game
    uint32_t shared = 0;                   // the instance every game registers it under (0: none / not sent yet)
    uint32_t made_for = 0;                 // outfits: the owner-side instance of the species it was made for
    bool set() const { return own != 0; }
    bool operator==(const Ref& o) const
    {
        return own == o.own && type == o.type && group == o.group && shared == o.shared && made_for == o.made_for;
    }
    bool operator!=(const Ref& o) const { return !(*this == o); }
};

struct Row {
    uint32_t creature = 0; // the owner's creature on its own game (instance), 0 = the world's own
    Ref tribe, civ;
    std::map<uint32_t, Ref> designs; // game design id -> design
    bool empty() const { return !creature && !tribe.set() && !civ.set() && designs.empty(); }
    bool operator==(const Row& o) const
    {
        return creature == o.creature && tribe == o.tribe && civ == o.civ && designs == o.designs;
    }
    bool operator!=(const Row& o) const { return !(*this == o); }
};

// ---- the rules ----
// the tribe outfit counts when it was made for the player's current creature (and every game has it: shared)
inline bool tribe_counts(const Row& r) { return r.tribe.set() && r.tribe.shared && r.tribe.made_for == r.creature; }
// the tribe line on the owner's side: the outfit's instance, else the creature's (0 = the world's own)
inline uint32_t tribe_line_own(const Row& r) { return tribe_counts(r) ? r.tribe.own : r.creature; }
// the Civ outfit counts when it was made for the player's current tribe line
inline bool civ_counts(const Row& r) { return r.civ.set() && r.civ.shared && r.civ.made_for == tribe_line_own(r); }

// What a stage's species is, as a choice of the Row: the creature (creations' key), or one of the Row's outfits.
enum class Source : uint8_t { None, Creature, Tribe, Civ };
// `have(src)`: this game can use that source's key (its creations' key is known, the outfit is registered). A source
// this game cannot use falls back to the one before it (Civ -> Tribe -> Creature -> None), so a game never answers a
// key it cannot build.
template <class Have>
inline Source pick(const Row& r, Stage s, Have have)
{
    if (s == Stage::Civ && civ_counts(r) && have(Source::Civ))
        return Source::Civ;
    if ((s == Stage::Civ || s == Stage::Tribe) && tribe_counts(r) && have(Source::Tribe))
        return Source::Tribe;
    if (s != Stage::Cell && r.creature && have(Source::Creature))
        return Source::Creature;
    return Source::None;
}

// an outfit made now for the row's current line: tribe for the creature, Civ for the tribe line
inline Ref tribe_ref(const Row& r, uint32_t own, uint32_t type, uint32_t group, uint32_t shared)
{
    return {own, type, group, shared, r.creature};
}
inline Ref civ_ref(const Row& r, uint32_t own, uint32_t type, uint32_t group, uint32_t shared)
{
    return {own, type, group, shared, tribe_line_own(r)};
}

// what the host sends of a row: a Ref whose shared key the host's game cannot resolve yet goes out without it (it
// counts nowhere until its records are there), so a key goes out only when every game can have it
template <class Known>
inline Row resolved(const Row& r, Known known)
{
    Row o = r;
    auto fix = [&](Ref& f) {
        if (f.shared && !known(f.shared))
            f.shared = 0;
    };
    fix(o.tribe);
    fix(o.civ);
    for (auto& [id, f] : o.designs)
        fix(f);
    return o;
}

// ---- the format (wire and store: the same bytes, little endian) ----
// u8 version (1), u32 creature, Ref tribe, Ref civ, u8 n, n x (u32 design id, Ref); Ref = u32 own, type, group, shared,
// made_for. A reader takes version 1 and ignores trailing bytes of a newer version.
inline void put_ref(pstore::Out& o, const Ref& f)
{
    o.u32(f.own);
    o.u32(f.type);
    o.u32(f.group);
    o.u32(f.shared);
    o.u32(f.made_for);
}
inline Ref get_ref(pstore::In& in)
{
    Ref f;
    f.own = in.u32();
    f.type = in.u32();
    f.group = in.u32();
    f.shared = in.u32();
    f.made_for = in.u32();
    return f;
}
// a Ref from outside is used only when it is whole: a shared instance is ours (0x7B......) or 0, a key has a type
inline bool ref_ok(const Ref& f)
{
    if (!f.set())
        return f.shared == 0 && f.made_for == 0;
    return f.type != 0 && (f.shared == 0 || is_shared(f.shared));
}
inline std::vector<uint8_t> encode(const Row& r)
{
    pstore::Out o;
    o.u8(kRowVersion);
    o.u32(r.creature);
    put_ref(o, r.tribe);
    put_ref(o, r.civ);
    size_t n = r.designs.size() < kMaxDesigns ? r.designs.size() : kMaxDesigns;
    o.u8((uint8_t)n);
    size_t i = 0;
    for (auto& [id, f] : r.designs) {
        if (i++ >= n)
            break;
        o.u32(id);
        put_ref(o, f);
    }
    return o.d;
}
// false: not a row (cut, another version, a bad Ref); `out` is then empty
inline bool decode(const std::vector<uint8_t>& d, Row& out)
{
    out = Row{};
    pstore::In in(d);
    if (in.u8() != kRowVersion || !in.ok)
        return false;
    Row r;
    r.creature = in.u32();
    r.tribe = get_ref(in);
    r.civ = get_ref(in);
    unsigned n = in.u8();
    if (!in.ok || n > kMaxDesigns || !ref_ok(r.tribe) || !ref_ok(r.civ))
        return false;
    for (unsigned i = 0; i < n; i++) {
        uint32_t id = in.u32();
        Ref f = get_ref(in);
        if (!in.ok || !id || !f.set() || !ref_ok(f) || f.made_for || r.designs.count(id))
            return false;
        r.designs[id] = f;
    }
    if (!in.ok)
        return false;
    out = std::move(r);
    return true;
}

// ---- the message (MSG_SPECIES_LINE, net/messages.def; family creations 1.1) ----
// u8 kind, then:
//   kRow    u8 player, u16 n, n bytes of encode(): the host's row of a player (to everybody), a guest's own row (to the
//           host, player = its id; the host takes it only from that player)
//   kStored u8 player, u16 n, n bytes: host -> that guest: its row kept in the host's per-player store (a rejoin, a new
//           evening): a guest that set nothing yet in this session takes it as its own and sends its creations again
//   kBundle u8 owner, u32 shared instance, u32 total, u32 offset, u16 n, n bytes: a piece of a line creation's bundle
//           (creations' bundle format); a guest sends its own to the host, the host relays it to everybody
// A peer before 0.7.16 does not know the type: its dispatch drops it (nothing changes there).
enum Kind : uint8_t { kRow = 1, kStored = 2, kBundle = 3 };
constexpr size_t kChunk = 900;

// ---- Creature-stage starter filtering (BUG-1583) ----
// Prevents a guest without explicit Creature selection from auto-spawning as a Tribe/Civ-stage outfitted creature
// (e.g. armored 20-26 HP, Strike lvl 5) against a starting 10 HP Creature-stage host.
constexpr float kMaxStarterBonusHp = 6.0f;       // clean starting Creature profile bonus HP (+0x56C) ceiling
constexpr float kMaxStarterTotalHp = 16.0f;      // clean starting Creature brain-0 total HP ceiling
constexpr float kMaxStarterCombatLevel = 2.0f;   // Strike/Bite/Charge/Spit level > 2 indicates mid/late or Civ-armored tier

struct CreatureStageStats {
    uint32_t instance = 0;
    float bonus_hp = 0.0f;         // cSpeciesProfile +0x56C
    float total_hp = 10.0f;        // measured or estimated brain-0 max HP
    float max_combat_level = 0.0f; // highest combat ability level (+0x3C for Bite/Charge/Strike/Spit)
    bool is_known_outfit = false;  // matches Row::tribe.own or Row::civ.own
    bool has_outfitter_parts = false;
};

inline bool is_row_outfit_instance(const Row& r, uint32_t inst)
{
    if (inst == 0)
        return false;
    return (r.tribe.set() && r.tribe.own == inst) || (r.civ.set() && r.civ.own == inst);
}

// True if this creature creation exceeds starting Creature-stage tier (outfitted Tribe/Civ creature or high-tier combat/armor).
inline bool is_overpowered_or_outfitted_starter(const CreatureStageStats& s, float host_max_hp = 10.0f)
{
    if (s.is_known_outfit || s.has_outfitter_parts)
        return true;
    if (s.bonus_hp > kMaxStarterBonusHp)
        return true;
    float hp_cap = kMaxStarterTotalHp;
    if (host_max_hp >= 10.0f) {
        float rel_cap = host_max_hp * 1.4f + 2.0f;
        if (rel_cap > hp_cap)
            hp_cap = rel_cap;
    }
    if (s.total_hp > hp_cap)
        return true;
    if (s.max_combat_level > kMaxStarterCombatLevel)
        return true;
    return false;
}

// Pick the newest clean Creature-stage candidate from `candidates`, or 0 to fall back to the world's starting species.
inline uint32_t select_clean_creature_starter(const std::vector<CreatureStageStats>& candidates, float host_max_hp = 10.0f)
{
    for (const auto& c : candidates) {
        if (c.instance == 0)
            continue;
        if (!is_overpowered_or_outfitted_starter(c, host_max_hp))
            return c.instance;
    }
    return 0;
}

// ---- Shared creation metadata & fallback name helpers (BUG-1583) ----
// Parses Spore pollen_metadata (0x030BDEE3) for BOTH offline (-2 sentinel) AND online/Maxis author IDs.
inline bool extract_metadata_name(const uint8_t* data, size_t len, std::string& out_author, std::string& out_name)
{
    out_author.clear();
    out_name.clear();
    if (!data || len < 0x40)
        return false;
    auto be32 = [](const uint8_t* p) -> uint32_t {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    };
    auto utf16le_to_utf8 = [](const uint8_t* p, size_t chars, bool strict_printable) -> std::string {
        std::string s;
        s.reserve(chars);
        for (size_t i = 0; i < chars; i++) {
            uint32_t c = (uint32_t)p[2 * i] | ((uint32_t)p[2 * i + 1] << 8);
            if (strict_printable && (c < 0x20u || c == 0x7Fu || c == 0xFFFEu || c == 0xFFFFu))
                return {};
            if (c < 0x80u) {
                s += (char)c;
            } else if (c < 0x800u) {
                s += (char)(0xC0u | (c >> 6));
                s += (char)(0x80u | (c & 0x3Fu));
            } else {
                s += (char)(0xE0u | (c >> 12));
                s += (char)(0x80u | ((c >> 6) & 0x3Fu));
                s += (char)(0x80u | (c & 0x3Fu));
            }
        }
        return s;
    };
    auto try_at = [&](size_t off, bool strict) -> bool {
        size_t o = off;
        std::string parts[2];
        for (int k = 0; k < 2; k++) {
            if (o + 4 > len)
                return false;
            uint32_t slen = be32(data + o);
            o += 4;
            if (slen > 256 || o + (size_t)slen * 2 > len)
                return false;
            if (slen > 0) {
                parts[k] = utf16le_to_utf8(data + o, slen, strict);
                if (strict && parts[k].empty())
                    return false;
            }
            o += (size_t)slen * 2;
        }
        if (parts[1].empty())
            return false;
        out_author = std::move(parts[0]);
        out_name = std::move(parts[1]);
        return true;
    };

    // 1. Offline author ID (-2 = FF FF FF FF FF FF FF FE) sentinel scan (matches dbpf::metadata_names)
    static constexpr uint8_t kOfflineMark[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE};
    for (size_t i = 0; i + 8 <= len; i++) {
        if (std::memcmp(data + i, kOfflineMark, 8) == 0) {
            if (try_at(i + 8, false))
                return true;
        }
    }

    // 2. Online EA / Maxis / Sporepedia metadata (version 10..20, author & name after 8-byte author ID at +0x38)
    uint32_t ver = be32(data);
    if (ver >= 10 && ver <= 20) {
        constexpr size_t kOffsets[3] = {0x38, 0x34, 0x3C};
        for (size_t off : kOffsets) {
            if (try_at(off, true))
                return true;
        }
    }
    return false;
}

inline const char* creation_group_label(uint32_t group)
{
    switch (group) {
    case 0x40616200u: return "Cell";
    case 0x40626200u: return "Creature";
    case 0x40636200u: return "Building";
    case 0x40646200u: return "Vehicle";
    case 0x40656200u: return "UFO";
    case 0x40666200u: return "Flora";
    default: return "Creation";
    }
}

inline std::string fallback_creation_name(uint32_t group, uint64_t id_or_hash)
{
    char buf[64];
    uint32_t tag = (uint32_t)((id_or_hash ^ (id_or_hash >> 32)) & 0xFFFFu);
    if (tag == 0)
        tag = 1;
    snprintf(buf, sizeof buf, "%s #%u", creation_group_label(group), (unsigned)tag);
    return buf;
}

inline std::string sanitize_creation_name(const std::string& raw, uint32_t group, uint64_t id_or_hash)
{
    std::string cleaned;
    cleaned.reserve(raw.size());
    for (unsigned char c : raw) {
        if (cleaned.size() >= 64)
            break;
        if (c >= 0x20 && c != 0x7F)
            cleaned.push_back((char)c);
    }
    while (!cleaned.empty() && cleaned.front() == ' ')
        cleaned.erase(cleaned.begin());
    while (!cleaned.empty() && cleaned.back() == ' ')
        cleaned.pop_back();
    if (cleaned.empty())
        return fallback_creation_name(group, id_or_hash);
    return cleaned;
}

} // namespace species
