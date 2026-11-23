// The per-player captain (WP-K1 of docs/plan-0.7.17.md): the portable rules, no game code (tests/core_tests.cpp).
// docs/re/creations.md#creations-captain-0717.
//  - BUG-1279: the host's captain property list for a guest (MSG_CREATION kind 10): which lists go, how a guest takes one
//  - BUG-1262: a partner's adventure captain (MSG_CREATION kind 11): its key here, when its puppet may show it
//  - BUG-1237: the own creature's bake asked before SetSpecies, how long the swap waits for it
//  - BUG-1475 (#217 a, idea of PR #217 by IltonBJSilva): a guest's own captain on its own game in Space
// The .prop walker follows App::PropertyList::Read 0x6A2F60 and the property reader 0x694410 (Ghidra 2026-11-23,
// feat/w74-captain): the idea of the walker is from the outside agent's branch fix/agy-open-bugs (4e7490e6).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace captain {

// a creation's property list: {creation instance, 0x00B1B104, (creation group & 0xFFFFE5FF) | 0xE500}
// (SporeCoop::CaptainProps_Get 0x5BF1E0, CaptainProps_Save 0x5BF220)
constexpr uint32_t kPropType = 0x00B1B104u;
constexpr uint32_t kPropItems = 0xC339D5C7u;    // the captain's item config (SporeCoop::CaptainItems_Load 0x5BF2E0)
constexpr uint32_t kPropNewer = 0x0D8A4BC9u;    // the key of a newer version (cEmpire_UpdateCaptainKey 0xC32AE0)
constexpr uint32_t kCreatureType = 0x2B978C46u; // a creature creation (crt) and its group
constexpr uint32_t kCreatureGroup = 0x40626200u;
// BUG-1262: the instance line of a partner's adventure captain copy here (0x7B the species line, 0x7C a player's
// creation, 0x7D a host world creation: creations.cpp)
constexpr uint32_t kCopyLine = 0x7A000000u;
// creations family minor of 0.7.17 (net/messages.def): a peer from it reads MSG_CREATION kinds 10 and 11; an older peer
// (creations 1.1, 0.7.16.1) drops them, so nothing is sent to it (BUG-1282's rule)
constexpr uint16_t kMinor = 2;

inline uint32_t prop_group(uint32_t group) { return (group & 0xFFFFE5FFu) | 0xE500u; }
inline bool is_prop_key(uint32_t type, uint32_t group) { return type == kPropType && (group & 0xFFFFu) == 0xE500u; }
inline bool is_creature(uint32_t type, uint32_t group) { return type == kCreatureType && group == kCreatureGroup; }

// the copy's instance of player `owner`'s captain with the bundle id `content` (the same on every game: the bundle is
// the same bytes everywhere): 0x7A000001..0x7AFFFFFF
inline uint32_t copy_instance(uint8_t owner, uint32_t content)
{
    const uint8_t seed[6] = {owner, 11u, (uint8_t)content, (uint8_t)(content >> 8), (uint8_t)(content >> 16),
                             (uint8_t)(content >> 24)};
    uint32_t h = 0x811C9DC5u;
    for (uint8_t b : seed)
        h = (h ^ b) * 0x1000193u;
    uint32_t low = h & 0x00FFFFFFu;
    return kCopyLine | (low ? low : 1u);
}
inline bool is_copy(uint32_t instance) { return (instance & 0xFF000000u) == kCopyLine && (instance & 0x00FFFFFFu); }

// ---- the .prop walker ----
// A binary property list: a big-endian count (bit 31: a parent list's key of 12 bytes follows), then per property
// {u32 id, u16 type, u16 flags} and its value. A scalar of type 1, 2, 5, 6 is 1 byte, 3, 7, 8 two, 9, 10, 13 four,
// 11, 12, 14 eight; 0x12 / 0x13 a u32 length and that many 1 / 2 byte characters; 0x11 is refused; any other 16
// bytes. With flags & 0x30 an array: u32 count, u32 item size, the items (a primitive type its own size per item, a
// string as above, any other type the item size); flags & 0x40 is refused. A list with more than 4096 properties or an
// array of more than 65536 items is not one this code takes.
struct PropScan {
    uint32_t count = 0;      // properties in the list
    bool items = false;      // it has kPropItems (a captain's list)
    bool newer = false;      // it has kPropNewer
    bool dropped = false;    // strip_prop: the dropped id was there
};

inline size_t prim_size(uint16_t t)
{
    switch (t) {
    case 0x01: case 0x02: case 0x05: case 0x06: return 1;
    case 0x03: case 0x07: case 0x08: return 2;
    case 0x09: case 0x0A: case 0x0D: return 4;
    case 0x0B: case 0x0C: case 0x0E: return 8;
    default: return 0;
    }
}

// copies `in` without the property `drop` (0: none) into `out` (the count is lowered, the parent bit and key kept);
// false (out empty) when the bytes are no property list this code reads
inline bool strip_prop(const std::vector<uint8_t>& in, uint32_t drop, std::vector<uint8_t>& out, PropScan* scan = nullptr)
{
    out.clear();
    PropScan s;
    auto be32 = [](const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; };
    auto be16 = [](const uint8_t* p) { return (uint16_t)((uint16_t)p[0] << 8 | p[1]); };
    const uint8_t* d = in.data();
    const size_t n = in.size();
    if (n < 4)
        return false;
    const uint32_t raw = be32(d);
    const bool parent = (raw & 0x80000000u) != 0;
    const uint32_t cnt = raw & 0x7FFFFFFFu;
    if (cnt > 4096u)
        return false;
    size_t p = 4;
    if (parent) {
        if (n - p < 12)
            return false;
        p += 12;
    }
    std::vector<uint8_t> o(d, d + p);
    uint32_t kept = 0;
    auto string_at = [&](uint16_t t) {
        const size_t ch = t == 0x13 ? 2u : 1u;
        if (n - p < 4)
            return false;
        const uint64_t len = be32(d + p);
        p += 4;
        if (len > (n - p) / ch)
            return false;
        p += (size_t)(len * ch);
        return true;
    };
    for (uint32_t i = 0; i < cnt; i++) {
        const size_t at = p;
        if (n - p < 8)
            return false;
        const uint32_t id = be32(d + p);
        const uint16_t t = be16(d + p + 4), fl = be16(d + p + 6);
        p += 8;
        if (!t || t == 0x11 || (fl & 0x40))
            return false;
        if (fl & 0x30) {
            if (n - p < 8)
                return false;
            const uint64_t c = be32(d + p), isz = be32(d + p + 4);
            p += 8;
            if (c > 65536u)
                return false;
            if (t == 0x12 || t == 0x13) {
                for (uint64_t k = 0; k < c; k++)
                    if (!string_at(t))
                        return false;
            } else {
                const size_t step = prim_size(t) ? prim_size(t) : (size_t)isz;
                if (step && c > (n - p) / step)
                    return false;
                p += (size_t)(c * step);
            }
        } else if (t == 0x12 || t == 0x13) {
            if (!string_at(t))
                return false;
        } else {
            const size_t sz = prim_size(t) ? prim_size(t) : 16u;
            if (sz > n - p)
                return false;
            p += sz;
        }
        if (drop && id == drop) {
            s.dropped = true;
            continue;
        }
        s.items |= id == kPropItems;
        s.newer |= id == kPropNewer;
        o.insert(o.end(), d + at, d + p);
        kept++;
    }
    if (p != n)
        return false;
    const uint32_t nraw = (parent ? 0x80000000u : 0u) | kept;
    o[0] = (uint8_t)(nraw >> 24), o[1] = (uint8_t)(nraw >> 16), o[2] = (uint8_t)(nraw >> 8), o[3] = (uint8_t)nraw;
    s.count = kept;
    if (scan)
        *scan = s;
    out.swap(o);
    return true;
}

// ---- BUG-1279: the host captain's property list on a guest ----
// The host sends a list only when it is a captain's (it has the item config): a creation's ordinary list (name and
// editor flags) is not sent. The guest takes it under the HOST's key, so it never registers it over anything of its
// own: not when this game has a list under that key (its own creation's), not when it has its own creation under the
// captain's creature key (ids are per profile: a guest's own creation of the same id, BUG-0278's class; the list would
// give that creation the host captain's name and items).
enum class HostProp : uint8_t { kRegister, kSame, kSkipOwn, kInvalid };
inline HostProp host_prop(bool items, uint32_t hash, bool registered, uint32_t registered_hash, bool own_list,
                          bool own_creation)
{
    if (!items || !hash)
        return HostProp::kInvalid;
    if (registered)
        return registered_hash == hash ? HostProp::kSame : HostProp::kRegister;
    if (own_list || own_creation)
        return HostProp::kSkipOwn;
    return HostProp::kRegister;
}

// CaptainProps_Save 0x5BF220 (the adventure's end 0xF16D60, the outfitter's exit 0x5804A4 / 0x5804F5, the name panel
// 0xEEBEA9 / 0xEEDA9B; every caller drops the result but CaptainProps_Copy 0x5BF522) writes the list into the creation's
// database, or into Pollination.package when that one is not writable: a list of ours (the host captain's on a guest,
// a copy's, a creation of a package of ours) would stay in the player's profile. Refused (false) for those keys while
// connected; a creation of this game's own under the key (made since the list was registered) is saved as the game does.
inline bool refuse_props_save(bool fix, bool connected_guest, bool host_list_here, bool creation_ours, bool own_creation)
{
    if (!fix || own_creation)
        return false;
    return (connected_guest && host_list_here) || creation_ours;
}

// ---- BUG-1262: a partner's adventure captain ----
// The puppet shows the copy only when this game has baked its model (BUG-1321: a creature made from an unbaked key is
// drawn white and stays so); never after a time-out. -1 (no bake manager) keeps the stand-in too.
inline bool puppet_shows_copy(bool fix, bool registered, int bake) { return fix && registered && bake == 1; }
// how long the sender waits before it reads its captain again when the records are not in a file package yet (a new
// captain: EditorSaves.package is written a moment later): 2, 4, 8, 16, then every 30 s
inline double captain_retry(unsigned tries)
{
    double s = 2.0;
    for (unsigned i = 0; i < tries && s < 30.0; i++)
        s *= 2.0;
    return s < 30.0 ? s : 30.0;
}

// ---- BUG-1237: the own creature baked before SetSpecies ----
// The swap asks the game's bake as soon as the own key is known (every kAskEvery while it is not baked), and waits for
// the bake at most kWait after the key was first wanted (the load's 3 s settle wait runs at the same time; a change of
// the own creature in a running world has none of its own). bake: 1 baked, 0 not yet, -1 no bake manager, -2 not
// asked yet.
constexpr double kAskEvery = 0.5, kWait = 5.0;
inline bool swap_waits(bool fix, int bake, double wanted_for) { return fix && (bake == 0 || bake == -2) && wanted_for < kWait; }

// ---- BUG-1475 (#217 a): a guest's own captain on its own game in Space ----
// The game's captain key (cEmpire +0xC4) on a guest's game is the host's (its save's). A guest of creature.mode own
// that plays a creation of its own gets that creation as its captain (cEmpire_SetCaptainKey), once the Space world has
// run kSettle seconds, when this game can load it and has its species (BUG-1111's portrait and BUG-1278's stats read
// both). The key the game sets itself later (the captain outfitter's result 0xFE2C7B, "Name Your Captain") is the
// player's: taken as its own captain. Before every save the world's key is written back for the save (savestrip hooks)
// and ours after it: the save holds the host's captain only.
constexpr double kSettle = 5.0;
enum class Own : uint8_t { kNone, kSet, kAdopt, kHold };
// cur: the empire's captain now; mine: the one we set (0: none yet); orig: the world's (read once the world has run
// kSettle seconds); want: the own creation. A world without a captain (orig 0) is left to the game's "Name Your Captain"
// (BUG-1111's path): the captain the player names there is its own.
inline Own own_step(bool fix, bool eligible, double in_world, uint32_t cur, uint32_t mine, uint32_t orig, uint32_t want)
{
    if (!fix || !eligible || in_world < kSettle)
        return Own::kNone;
    if (!mine) {
        if (want && cur == want)
            return Own::kHold;
        if (cur && cur != orig)
            return Own::kAdopt; // the game made a captain for this player since (named, outfitted)
        if (!orig || !want)
            return Own::kNone;
        return cur == want ? Own::kHold : Own::kSet;
    }
    if (!want && cur == mine)
        return Own::kHold;
    if (cur == mine)
        return Own::kHold;
    if (cur == orig || !cur)
        return Own::kSet; // (put back: the game's own path set the world's again)
    return Own::kAdopt;   // the game made a new captain for this player (an outfitter's result): it is its own
}

} // namespace captain
