// Own stage history per player (WP-H1 of docs/plan-0.7.17.md, BUG-1470 / BUG-1471; ideas and RE from PR #207 and
// PR #217 by IltonBJSilva): the portable rules. The game glue is history.cpp; docs/re/creations.md#creations-own-history.
//
// Every game has one cPlayer, loaded from the host's save: the consequence card of each finished stage (+0x10FC Cell,
// +0x1100 Creature, +0x1104 Tribe, +0x1108 Civ, +0x110C the Space archetype) and the Creature trait progress behind the
// Creature card (+0x1278 social, +0x127C combat). Before 0.7.17 every guest's game showed the host's (the wsync row
// "stages" wrote them every 0.5 s) and a guest's kill or social win, landed on the host's game, raised the host's
// progress (BUG-1471). In creature.mode own (every player its own creature) now:
//   * the host keeps a Record per guest (stable key of the per-player store, record `history` v1);
//   * a guest's kill / social win on the host's game, inside an actor::Scope naming the guest (npc.cpp, social.cpp),
//     adds its trait progress to the guest's Record, not to the host's cPlayer (the hook on cPlayer::AddTraitProgress);
//   * update(): a stage the host's world has done gets the guest's own card: Cell = what the guest's own game decides
//     from its own diet (reported over MSG_PLAYER_HISTORY K_CELL), Creature = the game's own rule run with the guest's
//     progress, Tribe and Civ = the host's (one shared tribe and nation, owner question 3 of the plan), Space = the
//     game's archetype of the guest's four cards; a stage the host's world has not done is nobody's (a reload of an
//     earlier save, a new game started after Cell: #218's RE note);
//   * the host sends each guest its Record (K_RECORD); the guest's game writes it into its own cPlayer while it plays
//     the host's world, and in Space gives its empire its own archetype (SpaceGame_SetArchetype, once per world).
// First sight of a guest in a world: its history so far was the party's one, so its Record starts as a copy of the
// host's cards and progress (seed()).
// Portable (no Windows, no game): tests/validate_tests.cpp test_history.
#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "player_store.h"

namespace history {

constexpr int kStages = 5; // in cPlayer's field order
constexpr int kCell = 0, kCreature = 1, kTribe = 2, kCiv = 3, kSpace = 4;
// the stage ids cPlayer::GetConsequenceTrait 0xC75880 / SetConsequenceTrait 0xC75E60 take (fnv1 of "CellGame" ...)
constexpr uint32_t kStageId[kStages] = {0xA426730B, 0xAD56080C, 0xF71FA311, 0xBEB528CB, 0x2DB6DAD3};
constexpr uint32_t kCardsOff = 0x10FC, kSocialOff = 0x1278, kCombatOff = 0x127C; // cPlayer
constexpr uint32_t kArchetypeOff = 0x58;                                       // cEmpire: the archetype index

// session 1.3 (0.7.17) reads MSG_PLAYER_HISTORY: a peer below plays the shared history as 0.7.16.1
constexpr uint16_t kSessionMinor = 3;

constexpr const char* kStoreFeature = pstore::kRecHistory;
constexpr uint16_t kStoreVersion = 1;
constexpr float kMaxProgress = 1.0e7f; // a progress beyond this is no read
constexpr float kMaxCredit = 1.0e6f;   // one credit beyond this is no game amount

// the trait ids of each stage (the release cheat setConsequenceTrait's table .data 0x156ACD0,
// docs/re/creations.md#creations-own-history)
inline bool valid_card(int stage, uint32_t v)
{
    static const uint32_t ids[kStages][10] = {
        {0xCFB01B93, 0xA8EC6F99, 0x5ECE4770}, // Cell: carnivore, herbivore, omnivore
        {0x17E5EF84, 0xFD159D91, 0xB9DE15F2}, // Creature: aggressive, social, mixed
        {0x0CAC124D, 0x60A78928, 0xC2CA9495}, // Tribe: aggressive, social, mixed
        {0x0A35D0F5, 0x3360727F, 0x6AEB96A1}, // Civ: military, economic, religious
        {0x50FEE6BF, 0xB40D2187, 0x2B35F523, 0xB436C0BC, 0x9CE4639C, 0xB7CB9F7A, 0xCF6C2AB4, 0xD9C806E4, 0xA5656E84,
         0xF966D429}, // Space: bard, ecologist, zealot, diplomat, scientist, trader, shaman, warrior, wanderer, knight
    };
    if (stage < 0 || stage >= kStages || !v)
        return false;
    for (uint32_t id : ids[stage])
        if (id && id == v)
            return true;
    return false;
}
// 0 (no card) or a card of the stage
inline bool card_ok(int stage, uint32_t v) { return !v || valid_card(stage, v); }

// the archetype index the game writes to cEmpire +0x58 for a Space card (SpaceGame_SetArchetype 0x1005C92..0x1005DAC,
// objdump of GOG 2024); -1: not a Space card
inline int archetype_index(uint32_t card)
{
    switch (card) {
    case 0xD9C806E4: return 9;  // warrior
    case 0xB7CB9F7A: return 10; // trader
    case 0x9CE4639C: return 11; // scientist
    case 0xCF6C2AB4: return 12; // shaman
    case 0x50FEE6BF: return 13; // bard
    case 0x2B35F523: return 14; // zealot
    case 0xB436C0BC: return 15; // diplomat
    case 0xB40D2187: return 16; // ecologist
    case 0xA5656E84: return 17; // wanderer
    case 0xF966D429: return 18; // knight
    default: return -1;
    }
}

inline bool sane(float v) { return std::isfinite(v) && std::fabs(v) <= kMaxProgress; }

struct Record {
    uint32_t cards[kStages] = {};
    float social = 0, combat = 0;
    uint32_t cell_own = 0; // the Cell card the player's own game decides from its own diet (its last report), 0: none
    bool operator==(const Record& o) const
    {
        for (int i = 0; i < kStages; i++)
            if (cards[i] != o.cards[i])
                return false;
        return social == o.social && combat == o.combat && cell_own == o.cell_own;
    }
    bool operator!=(const Record& o) const { return !(*this == o); }
};

// first sight of a guest in a world: the history so far was the party's one (every game showed the host's)
inline Record seed(const uint32_t host_cards[kStages], float host_social, float host_combat)
{
    Record r;
    for (int i = 0; i < kStages; i++)
        r.cards[i] = card_ok(i, host_cards[i]) ? host_cards[i] : 0;
    r.social = sane(host_social) ? host_social : 0;
    r.combat = sane(host_combat) ? host_combat : 0;
    return r;
}

// a guest's kill (social false) or social win (true) of `amount`, as cPlayer::AddTraitProgress 0xC75840 adds it
inline bool credit(Record& r, bool social, float amount)
{
    if (!std::isfinite(amount) || amount == 0 || std::fabs(amount) > kMaxCredit)
        return false;
    float& v = social ? r.social : r.combat;
    float n = v + amount;
    if (!sane(n))
        return false;
    v = n;
    return true;
}

// the host's cards decide which stages are done; a done stage the guest has no card of gets its own:
// decide(stage, record) is the guest's own candidate (Cell: its game's report, Creature: the game's rule on its
// progress, Space: the game's archetype of its four cards; the record holds the stages before it already), a
// candidate that is no card of the stage is the host's card. Tribe and Civ are always the host's. Returns the bits
// of the stages whose card changed.
template <class Decide>
inline unsigned update(Record& r, const uint32_t host[kStages], Decide&& decide)
{
    unsigned changed = 0;
    for (int i = 0; i < kStages; i++) {
        uint32_t v = r.cards[i];
        if (!valid_card(i, host[i]))
            v = 0; // not done in this world (an earlier save, a game started later): nobody's
        else if (i == kTribe || i == kCiv)
            v = host[i]; // one shared tribe and nation: the host's card
        else if (!valid_card(i, v)) {
            uint32_t d = decide(i, (const Record&)r);
            v = valid_card(i, d) ? d : host[i];
        }
        if (v != r.cards[i]) {
            r.cards[i] = v;
            changed |= 1u << i;
        }
    }
    return changed;
}

// guest: the bits of the stages whose card in this game's cPlayer differs from the record
inline unsigned card_diff(const uint32_t mine[kStages], const Record& r)
{
    unsigned d = 0;
    for (int i = 0; i < kStages; i++)
        if (mine[i] != r.cards[i])
            d |= 1u << i;
    return d;
}
inline bool progress_differs(float social, float combat, const Record& r)
{
    return !(std::fabs(social - r.social) <= 0.01f) || !(std::fabs(combat - r.combat) <= 0.01f);
}

// guest, Space: give the empire its own archetype (SpaceGame_SetArchetype(game, false, false): the archetype of the
// cPlayer's Space card, its index and its tools)? Only once the card is in the cPlayer, never twice for one card in a
// world (twice duplicates tools, PR #217's note) and not when the empire has that archetype already (the host's is
// the same, or this world's save has it)
inline bool archetype_due(uint32_t own, uint32_t cplayer_space, int empire_index, uint32_t applied)
{
    int want = archetype_index(own);
    return want >= 0 && cplayer_space == own && empire_index != want && applied != own;
}

// own history is in play: the switch on and every player its own creature (creature.mode own); host mode: one history
inline bool active(bool fix, bool host_creature_mode) { return fix && !host_creature_mode; }

// a peer's mod reads MSG_PLAYER_HISTORY: it sent its families, session at the same rev and minor 3 or newer
inline bool peer_reads(bool has_families, bool found, uint16_t rev, uint16_t minor, uint16_t my_rev)
{
    return has_families && found && rev == my_rev && minor >= kSessionMinor;
}

// ---- the store record `history` v1 (per player, under its stable key): 5 x u32 cards, f32 social, f32 combat,
// u32 the own Cell card (28 + 4 bytes)
inline std::vector<uint8_t> encode(const Record& r)
{
    pstore::Out o;
    for (uint32_t c : r.cards)
        o.u32(c);
    o.f32(r.social);
    o.f32(r.combat);
    o.u32(r.cell_own);
    return o.d;
}
inline bool decode(const std::vector<uint8_t>& d, Record& out)
{
    pstore::In in(d);
    Record r;
    for (int i = 0; i < kStages; i++)
        r.cards[i] = in.u32();
    r.social = in.f32();
    r.combat = in.f32();
    r.cell_own = in.u32();
    if (!in.end() || !sane(r.social) || !sane(r.combat) || !card_ok(kCell, r.cell_own))
        return false;
    for (int i = 0; i < kStages; i++)
        if (!card_ok(i, r.cards[i]))
            return false;
    out = r;
    return true;
}

// the host's records of one world (stable key, or "id:N" for a player without one: memory only)
class Book {
public:
    std::map<std::string, Record> recs;
    std::string world; // the store's world they belong to ("" none yet)

    // the store of world `w` opened: its records replace ours; ours it has none of stay only when it is the same world
    // (a guest seen since the last save); another world starts empty. Returns the records read.
    int load(const pstore::Store& s, const std::string& w)
    {
        std::map<std::string, Record> keep;
        if (w == world)
            for (auto& [k, x] : recs)
                if (!s.get(k, kStoreFeature))
                    keep[k] = x;
        recs = std::move(keep);
        world = w;
        int n = 0;
        for (auto& [k, p] : s.players) {
            const pstore::Record* r = s.get(k, kStoreFeature);
            Record x;
            if (r && r->ver == kStoreVersion && decode(r->data, x)) {
                recs[k] = x;
                n++;
            }
        }
        return n;
    }
    // the host's word (coop.store adopt): `from`'s history is `to`'s
    bool adopt(const std::string& from, const std::string& to)
    {
        auto it = recs.find(from);
        if (from == to || it == recs.end())
            return false;
        recs[to] = it->second;
        recs.erase(from);
        return true;
    }
};

} // namespace history
