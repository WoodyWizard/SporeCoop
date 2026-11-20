// Every player's own start in a new Cell game (BUG-9273): the portable part (no game calls; `make test` checks it in
// tests/validate_tests.cpp test_start_choice). The module is start_choice.{h,cpp}; docs/01-architecture.md#architecture-start-choice.
//
//   * a diet picks the player's own cell: the game's premade player cells of the Cell wizard's diet cards (herbivore
//     E219E5D2, carnivore 252A132F) and, since the wizard has no omnivore card, the game's own cell 0C176D89 whose
//     model has both mouths (GetCellModelData +0x1250 and +0x1251 both 1: docs/re/cell.md#cell-start-choice);
//   * the diet also gives the Cell consequence card the stage gives that diet (cards.cpp: carnivore CFB01B93,
//     herbivore A8EC6F99, omnivore 5ECE4770), used when the player's own game reported none;
//   * the host keeps every player's choice by the player's identity key (identity.h key()) next to the planet:
//     <planet>.spo.duostart (SCH1, founders::write_side), so a rejoin and a reload do not ask again;
//   * MSG_START_CHOICE (79, session family): u8 kind and its payload (below).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/buffer.h"

namespace start_choice {

enum Diet : uint8_t { kHerbivore = 0, kCarnivore = 1, kOmnivore = 2, kDiets = 3 };

constexpr uint32_t kCellModelType = 0x3D97A8E4, kCellModelGroup = 0x40616200; // cll, the cell models' group
constexpr uint32_t kDietCell[kDiets] = {0xE219E5D2, 0x252A132F, 0x0C176D89};
constexpr uint32_t kDietCard[kDiets] = {0xA8EC6F99, 0xCFB01B93, 0x5ECE4770};
inline const char* diet_name(int d)
{
    static const char* n[kDiets] = {"herbivore", "carnivore", "omnivore"};
    return d >= 0 && d < kDiets ? n[d] : "?";
}
inline int diet_find(const std::string& s)
{
    for (int i = 0; i < kDiets; i++)
        if (s == diet_name(i) || s == std::string(diet_name(i)).substr(0, 4) || s == std::to_string(i))
            return i;
    return -1;
}
// the diet a cell model is (one of ours), -1 = none
inline int diet_of_cell(uint32_t instance)
{
    for (int i = 0; i < kDiets; i++)
        if (kDietCell[i] == instance)
            return i;
    return -1;
}

// ---- the creature's name: what the player typed, trimmed, printable, at most kNameMax characters (UTF-8 kept whole)
constexpr size_t kNameMax = 24;
inline std::string clean_name(const std::string& in)
{
    std::string out;
    size_t chars = 0;
    for (size_t i = 0; i < in.size() && chars < kNameMax;) {
        unsigned char c = (unsigned char)in[i];
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > in.size()) { // a broken byte: dropped
            i++;
            continue;
        }
        bool ok = true;
        for (size_t k = 1; k < n; k++)
            ok &= ((unsigned char)in[i + k] >> 6) == 2;
        if (ok && (n > 1 || (c >= 0x20 && c != 0x7F)))
            out.append(in, i, n), chars++;
        i += ok ? n : 1;
    }
    size_t a = out.find_first_not_of(' '), b = out.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : out.substr(a, b - a + 1);
}

// ---- one player's choice
constexpr int kNoColor = -1;
struct Choice {
    uint8_t diet = kHerbivore;
    int8_t color = kNoColor; // findx::kPalette index, -1 = keep the one the host gave
    std::string name;
    bool operator==(const Choice&) const = default;
};
inline bool valid(const Choice& c, int colors)
{
    return c.diet < kDiets && c.color >= kNoColor && c.color < colors && !c.name.empty() && c.name == clean_name(c.name);
}
inline void write_choice(net::Writer& w, const Choice& c)
{
    w.u8(c.diet);
    w.u8((uint8_t)c.color);
    w.str8(c.name);
}
inline bool read_choice(net::Reader& r, Choice& c, int colors)
{
    Choice t;
    t.diet = r.u8();
    t.color = (int8_t)r.u8();
    t.name = r.str8();
    if (!r.ok() || !valid(t, colors))
        return false;
    c = t;
    return true;
}

// ---- MSG_START_CHOICE (79, session family 1.5): u8 kind, payload
enum Kind : uint8_t {
    kAsk = 1,    // host -> a guest: nothing. Choose your own start (diet, name, colour) for this planet
    kAnswer = 2, // guest -> host: Choice
    kTable = 3,  // host -> every guest: u8 n, n x {u8 player, Choice}: the choices of the players here (a guest
                 //   finds its own in it after a rejoin: it plays it again without being asked)
};
struct Row {
    uint8_t player = 0;
    Choice c;
    bool operator==(const Row&) const = default;
};
inline void write_table(net::Writer& w, const std::vector<Row>& rows)
{
    w.u8(kTable);
    w.u8((uint8_t)rows.size());
    for (const auto& r : rows) {
        w.u8(r.player);
        write_choice(w, r.c);
    }
}
inline bool read_table(net::Reader& r, std::vector<Row>& out, int colors) // (after the kind byte)
{
    unsigned n = r.u8();
    if (!r.ok() || n > 16)
        return false;
    std::vector<Row> rows;
    for (unsigned i = 0; i < n; i++) {
        Row row;
        row.player = r.u8();
        if (!r.ok() || !read_choice(r, row.c, colors))
            return false;
        rows.push_back(std::move(row));
    }
    out = std::move(rows);
    return true;
}

// ---- the side file next to the planet: <planet>.spo.duostart (founders::write_side). SCH1: u32 'SCH1', u8 n,
// n x {str8 key (identity.h key()), Choice}
constexpr uint32_t kFileMagic = 0x31484353u; // SCH1
constexpr size_t kMaxRecords = 16;
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
inline bool decode_file(const std::vector<uint8_t>& bytes, std::vector<Record>& out, int colors)
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
        if (!r.ok() || rec.key.empty() || !read_choice(r, rec.c, colors))
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
// a player's choice is recorded once: a second answer for the same key keeps the first (false = kept)
inline bool record_once(std::vector<Record>& recs, const std::string& key, const Choice& c)
{
    if (key.empty() || find_record(recs, key) || recs.size() >= kMaxRecords)
        return false;
    recs.push_back({key, c});
    return true;
}

// ---- who is asked: a guest seated in the host's Cell world, with no recorded choice, not asked in this join yet
struct AskState {
    bool setting = true;    // the session setting cell.start_choice
    bool cell = false;      // the host plays the Cell stage now
    bool seated = false;    // the guest holds a party seat (its identity is known)
    bool in_world = false;  // the guest plays the host's Cell world now (not loading, not elsewhere)
    bool recorded = false;  // the planet's file has its choice
    bool asked = false;     // asked already since it joined
};
inline bool should_ask(const AskState& s)
{
    return s.setting && s.cell && s.seated && s.in_world && !s.recorded && !s.asked;
}

} // namespace start_choice
