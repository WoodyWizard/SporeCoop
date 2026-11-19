// Which other SporeCoop versions may play together (B6 of 0.7.15, owner decision 7, BUG-1137;
// docs/04-netcode.md#netcode-compat). Portable: tests/net_tests.cpp and tests/validate_tests.cpp.
//
// Every 0.7.x of the same protocol (kProtoVersion 7) and the same exe build joins, with a warning on both sides
// ("<player> plays SporeCoop 0.7.13; you have 0.7.15: some things may not match"), unless there is an explicit
// conflict: each message family of net/messages.def has a revision (`rev`, bumped when older versions cannot read the
// family any more); HELLO and WELCOME carry the sender's (CAP_FAMILIES). A different rev of a family the current stage
// uses refuses the join with a text that names both versions (`coop.fix net.family_check off`: never refused, as in
// 0.7.14). A peer that sends no revisions (every version before 0.7.15) counts as rev 1 minor 0 of the families
// 0.7.15 started with (kBaseFamilies). A family one side does not know is no conflict: its messages are new types the
// other side ignores, as every addition since 0.7.0.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "manifest.h"
#include "protocol.h"

namespace net::compat {

// the families 0.7.15 started with (ids 1..10 of messages.def): what a peer without CAP_FAMILIES has, at rev 1 minor 0
constexpr uint8_t kBaseFamilies = 10;

inline std::vector<FamilyRev> my_families()
{
    std::vector<FamilyRev> v;
    for (auto& f : kFamilies)
        v.push_back({(uint8_t)f.id, f.rev, f.minor});
    return v;
}
inline std::vector<FamilyRev> base_families()
{
    std::vector<FamilyRev> v;
    for (uint8_t i = 1; i <= kBaseFamilies; i++)
        v.push_back({i, 1, 0});
    return v;
}

// the stage bit of a game mode (modes.h ids; an adventure counts as Creature; menus, editors, loads: 0)
inline uint32_t stage_bit(uint32_t mode)
{
    switch (mode) {
    case 0x1654C00: return STAGE_CELL;
    case 0x1654C01: case 0x1654C10: return STAGE_CREATURE;
    case 0x1654C02: return STAGE_TRIBE;
    case 0x1654C04: return STAGE_CIV;
    case 0x1654C05: return STAGE_SPACE;
    default: return 0;
    }
}
// is family `id` in use in `mode`? A family of every stage always; an unknown family never
inline bool used(uint8_t id, uint32_t mode)
{
    const FamilyInfo* f = family_info(id);
    return f && ((f->stages & STAGE_ALWAYS) || (f->stages & stage_bit(mode)));
}

struct Conflict {
    bool any = false;
    uint8_t family = 0;
    uint16_t mine = 0, theirs = 0;
};
inline const FamilyRev* find(const std::vector<FamilyRev>& v, uint8_t id)
{
    for (auto& x : v)
        if (x.family == id)
            return &x;
    return nullptr;
}
// the first family both sides know, used in `mode` (all families: `all`), whose rev differs. `has`: the peer sent its
// revisions (false: an older peer, kBaseFamilies at rev 1)
inline Conflict conflict(const std::vector<FamilyRev>& mine, const std::vector<FamilyRev>& theirs_sent, bool has,
                         uint32_t mode, bool all = false)
{
    const std::vector<FamilyRev> base = has ? std::vector<FamilyRev>{} : base_families();
    const std::vector<FamilyRev>& theirs = has ? theirs_sent : base;
    for (auto& m : mine) {
        const FamilyRev* t = find(theirs, m.family);
        if (!t || t->rev == m.rev || !(all || used(m.family, mode)))
            continue;
        return {true, m.family, m.rev, t->rev};
    }
    return {};
}

// "coop 0.7.15, SporeKit 0.7.15, GOG 2024 dc04aee5" -> "0.7.15"; "" (a version before 0.7.4) -> ""
inline std::string short_version(const std::string& full)
{
    if (full.rfind("coop ", 0) != 0)
        return full;
    size_t e = full.find(',');
    return full.substr(5, e == std::string::npos ? std::string::npos : e - 5);
}
inline std::string version_words(const std::string& full)
{
    std::string v = short_version(full);
    return v.empty() ? "an older SporeCoop (before 0.7.4)" : "SporeCoop " + v;
}
inline std::string family_title(uint8_t id)
{
    const FamilyInfo* f = family_info(id);
    return f ? f->title : "family " + std::to_string(id);
}

// the warning both players see at a join of another version (owner decision 7)
inline std::string warn_text(const std::string& player, const std::string& their_version, const std::string& my_version)
{
    return player + " plays " + version_words(their_version) + "; you have " + version_words(my_version) +
           ": some things may not match";
}
// the refusal, for the player who reads it (`other`: the other player's name and version, `mine`: the reader's)
inline std::string refuse_text(const std::string& other, const std::string& other_version, const std::string& my_version,
                               uint8_t family, uint16_t other_rev, uint16_t my_rev)
{
    return other + " plays " + version_words(other_version) + "; you have " + version_words(my_version) + ": the " +
           family_title(family) + " messages of these versions do not fit together (revision " +
           std::to_string(other_rev) + " there, " + std::to_string(my_rev) +
           " here). Update both games to the same version to play together";
}
// the host's line about a guest it refused
inline std::string not_let_in_text(const std::string& player, const std::string& their_version, const std::string& my_version,
                                   uint8_t family, uint16_t their_rev, uint16_t my_rev)
{
    return player + " (" + version_words(their_version) + ") was not let in: the " + family_title(family) +
           " messages of your versions do not fit together (revision " + std::to_string(their_rev) + " there, " +
           std::to_string(my_rev) + " here; you have " + version_words(my_version) + ")";
}
// a later stage change into a family that differs: a warning, the session goes on
inline std::string stage_warn_text(const std::string& player, const std::string& their_version, const Conflict& c)
{
    return player + " plays " + version_words(their_version) + ": the " + family_title(c.family) +
           " messages of your versions do not fit together (revision " + std::to_string(c.theirs) + " there, " +
           std::to_string(c.mine) + " here): playing " + family_title(c.family) + " together may not work";
}

// BUG-1281: a peer running the SporeCoop Lineages fork (PR #196, based on 0.7.14.1). It keeps protocol 7 and sends no
// family revisions, so the gate above let it in as an older 0.7.x, and its messages (ids 55, 58, 70-94, a u16
// PlayerState flags field) decode as ours. Two signs, either is enough: its HELLO / WELCOME sets caps bit 8, which it
// uses for a campaign profile (a str8) and ours for the family list, so bit 8 without a readable family list (ours
// always writes one with the bit); or its version text names the fork ("coop 0.7.14.1-lineages64-dev, ...")
inline bool is_fork(const std::string& version, uint32_t caps, bool has_families)
{
    if ((caps & CAP_FAMILIES) && !has_families)
        return true;
    std::string v = version;
    for (auto& ch : v)
        ch = (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
    return v.find("lineages") != std::string::npos;
}
// the refusal, for the player who reads it (`other`: the other player's name and version, `mine`: the reader's).
// `fork_is_other`: the other player runs the fork (our guest refusing a fork host); false: the reader does (the REJECT
// our host sends to a fork guest, which shows it as every 0.7.x does)
inline std::string fork_refuse_text(const std::string& other, const std::string& other_version, const std::string& my_version,
                                    bool fork_is_other)
{
    if (fork_is_other)
        return other + " plays the Lineages fork of SporeCoop (" + version_words(other_version) + "); you have " +
               version_words(my_version) +
               ": the two use different messages and cannot play together. Play with the same SporeCoop as the host";
    return other + " plays " + version_words(other_version) + "; you have the Lineages fork of SporeCoop (" +
           version_words(my_version) +
           "): the two use different messages and cannot play together. Play with the same SporeCoop as the host";
}
// the host's line about a fork guest it refused
inline std::string fork_not_let_in_text(const std::string& player, const std::string& their_version)
{
    return player + " (the Lineages fork of SporeCoop, " + version_words(their_version) +
           ") was not let in: the fork's messages do not fit this SporeCoop";
}

// "session 1.0, world 1.0, ..." (status lines, reports); has false: "none sent (before 0.7.15: rev 1 of each)"
inline std::string families_text(const std::vector<FamilyRev>& v, bool has = true)
{
    if (!has)
        return "none sent (before 0.7.15: rev 1 of each)";
    std::string s;
    for (auto& x : v) {
        const FamilyInfo* f = family_info(x.family);
        if (!s.empty())
            s += ", ";
        s += (f ? std::string(f->name) : "#" + std::to_string(x.family)) + " " + std::to_string(x.rev) + "." +
             std::to_string(x.minor);
    }
    return s;
}

} // namespace net::compat
