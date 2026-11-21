// Every player's own start in a new game at ANY stage (BUG-9293, the owner's rule of 2026-11-20: "whatever stage the
// host starts a new game in, the partner gets their own choice too"): the portable part (no game calls; `make test`
// checks it in tests/validate_tests.cpp test_start_any). The module is start_any.{h,cpp}; the Cell start stays
// start_choice (BUG-9273). docs/01-architecture.md#architecture-start-any.
//
//   * the stages a new game can start in from the galaxy menu after Cell: Creature (1), Tribe (2), Civ (3), Space (4);
//     a player starting there has the consequence card of every stage before it (cPlayer +0x10FC.., the History): the
//     choice names one card per earlier stage, Cell .. Civ (the Space archetype comes from the four, as the game does);
//   * the player's own creature: one of its My Creations creatures (or the one it plays already);
//   * MSG_START_ANY (110, session family 1.7): u8 kind and its payload (below); the host keeps every choice by the
//     player's identity next to the planet: <planet>.spo.duostartany (SAN1).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/buffer.h"

namespace start_any {

constexpr int kCardStages = 4; // Cell, Creature, Tribe, Civ (the Space archetype is computed from these)
constexpr int kOptions = 3;
// the consequence cards a player can pick per stage (cards.cpp valid_card's table): Cell carnivore, herbivore,
// omnivore; Creature aggressive, social, mixed; Tribe aggressive, social, mixed; Civ military, economic, religious
constexpr uint32_t kCard[kCardStages][kOptions] = {
    {0xCFB01B93, 0xA8EC6F99, 0x5ECE4770},
    {0x17E5EF84, 0xFD159D91, 0xB9DE15F2},
    {0x0CAC124D, 0x60A78928, 0xC2CA9495},
    {0x0A35D0F5, 0x3360727F, 0x6AEB96A1},
};
inline const char* option_name(int stage, int i)
{
    static const char* n[kCardStages][kOptions] = {{"carnivore", "herbivore", "omnivore"},
                                                   {"aggressive", "social", "mixed"},
                                                   {"aggressive", "social", "mixed"},
                                                   {"military", "economic", "religious"}};
    return stage >= 0 && stage < kCardStages && i >= 0 && i < kOptions ? n[stage][i] : "-";
}
// the start stage index (1 Creature .. 4 Space); the card stages a start there asks for: 0 .. stage - 1 (at most 4)
inline int cards_asked(int start) { return start < 1 ? 0 : start > kCardStages ? kCardStages : start; }

struct Choice {
    uint8_t stage = 0;          // 1 Creature .. 4 Space: the stage of the game it was chosen for
    uint32_t creature = 0;      // the creature's instance (its own creation, as this player's game has it), 0 = keep
    int8_t card[kCardStages] = {-1, -1, -1, -1}; // option index per card stage, -1 = not asked (a later stage)
    bool operator==(const Choice&) const = default;
};
inline bool valid(const Choice& c)
{
    if (c.stage < 1 || c.stage > 4)
        return false;
    for (int i = 0; i < kCardStages; i++) {
        const bool asked = i < cards_asked(c.stage);
        if (asked ? (c.card[i] < 0 || c.card[i] >= kOptions) : c.card[i] != -1)
            return false;
    }
    return true;
}
inline uint32_t card_id(const Choice& c, int stage)
{
    return stage >= 0 && stage < kCardStages && c.card[stage] >= 0 && c.card[stage] < kOptions ? kCard[stage][c.card[stage]] : 0;
}
inline void write_choice(net::Writer& w, const Choice& c)
{
    w.u8(c.stage);
    w.u32(c.creature);
    for (int8_t v : c.card)
        w.u8((uint8_t)v);
}
inline bool read_choice(net::Reader& r, Choice& c)
{
    Choice t;
    t.stage = r.u8();
    t.creature = r.u32();
    for (auto& v : t.card)
        v = (int8_t)r.u8();
    if (!r.ok() || !valid(t))
        return false;
    c = t;
    return true;
}

// ---- MSG_START_ANY (110): u8 kind, payload
enum Kind : uint8_t {
    kAsk = 1,    // host -> a guest: u8 start stage. Choose your own creature and your cards of the earlier stages
    kAnswer = 2, // guest -> host: the Choice
    kTaken = 3,  // host -> that guest: u8 1 taken (its seat has the cards now) / 0 refused
};

// ---- the planet's records
constexpr uint32_t kFileMagic = 0x314E4153u; // SAN1
constexpr size_t kMaxRecords = 32;
struct Record {
    std::string key;
    Choice c;
    bool operator==(const Record&) const = default;
};
inline std::vector<uint8_t> encode_file(const std::vector<Record>& recs)
{
    net::Writer w;
    w.u32(kFileMagic);
    size_t n = recs.size() < kMaxRecords ? recs.size() : kMaxRecords;
    w.u8((uint8_t)n);
    for (size_t i = 0; i < n; i++) {
        w.str8(recs[i].key);
        write_choice(w, recs[i].c);
    }
    return w.data;
}
inline bool decode_file(const std::vector<uint8_t>& bytes, std::vector<Record>& out)
{
    net::Reader r(bytes.data(), bytes.size());
    if (r.u32() != kFileMagic)
        return false;
    unsigned n = r.u8();
    if (!r.ok() || n > kMaxRecords)
        return false;
    std::vector<Record> recs;
    for (unsigned i = 0; i < n; i++) {
        Record rec;
        rec.key = r.str8();
        if (!r.ok() || rec.key.empty() || !read_choice(r, rec.c))
            return false;
        recs.push_back(std::move(rec));
    }
    if (r.left())
        return false;
    out = std::move(recs);
    return true;
}
inline const Record* find_record(const std::vector<Record>& recs, const std::string& key)
{
    for (const auto& r : recs)
        if (r.key == key)
            return &r;
    return nullptr;
}

// ---- who is asked: a guest seated in the host's world of a stage after Cell, on a planet whose party began in this
// session (a new game, or the planet's first co-op play), with no recorded choice, not asked in this join yet
struct AskState {
    bool setting = true;
    int stage = 0;          // the host's stage index now (0 Cell .. 4 Space), -1 none
    bool new_party = false; // the planet's party was made in this session (no <planet>.spo.duoparty before)
    bool seated = false;
    bool in_world = false;  // the guest plays the host's world now, settled
    bool recorded = false;
    bool asked = false;
};
inline bool should_ask(const AskState& s)
{
    return s.setting && s.stage >= 1 && s.stage <= 4 && s.new_party && s.seated && s.in_world && !s.recorded && !s.asked;
}

} // namespace start_any
