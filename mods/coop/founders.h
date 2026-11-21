// The campaign's founder seats, kept next to the planet's save (Lineages port, from PR #196's founders.h).
//
// Phase 2 of the port (BUG-9260) brings only the party: one seat per player who ever played this planet. The founder
// rosters of the stages (Tribe villages, Civilization nations), the difficulty rules and the outfit histories of
// Lineages come with the phases that use them; they index the same seats.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace founders {

// seats a campaign file can hold (the host's and up to seven partners'); the game plays kPlaySeats of them for now
constexpr size_t kMaxSeats = 8;
constexpr size_t kPlaySeats = 4;

// The campaign's party, one seat per player who ever played this planet, persistent next to the save
// (`<planet>.spo.duoparty`). PTY1: u32 'PTY1', u8 seats, per seat u64 player identity, u32 own creature species
// instance, str8 name. PTY2 (BUG-9263, the own stage history) = PTY1's seat followed by u8 flags (bit 0: the seat has
// a history of its own), 5 x u32 its consequence cards (Cell, Creature, Tribe, Civ, Space: cPlayer +0x10FC..+0x110C,
// 0 = the stage not done) and f32 social, f32 combat (its trait progress, cPlayer +0x1278 / +0x127C). PTY1 files are
// read (no own history: the seat shared the host's), every write is PTY2. Seat 0 is the host's. The Creature nests,
// Tribe villages and Civilization nations of the later phases are indexed by these seats.
constexpr size_t kCards = 5;
struct PartySeat {
    uint64_t player = 0;   // identity.h; 0 = not known yet
    uint32_t species = 0;  // the seat's own creature (its key instance); 0 = plays the host's species
    std::string name;      // the player's name when last seen (for an offline seat)
    bool own_cards = false;          // BUG-9263: cards / social / combat below are this player's own
    uint32_t cards[kCards] = {};     // the consequence card of each finished stage (docs/re/creations.md#creations-own-cards)
    float social = 0, combat = 0;    // the Creature stage's trait progress
    bool operator==(const PartySeat&) const = default;
};
struct Party {
    std::vector<PartySeat> seats;
    int seat_of(uint64_t player) const
    {
        if (player)
            for (size_t i = 0; i < seats.size(); ++i)
                if (seats[i].player == player)
                    return int(i);
        return -1;
    }
    bool operator==(const Party&) const = default;
};

// planet_file is world::current_game_key() (the full path of the planet's .spo)
// BUG-9281: false while the planet has no file yet (a new game before the game's first save of it)
bool planet_saved(const std::string& planet_file);
bool read_party(const std::string& planet_file, Party& out);
// a party may only grow, and a seat's identity is never replaced by another one
bool record_party(const std::string& planet_file, const Party& value, std::string& err);

// a mod side file next to the planet's save (planet_file + suffix, never inside the game's save), written whole
// through a temporary file. read: false = none, unreadable or larger than max_bytes.
bool read_side(const std::string& planet_file, const wchar_t* suffix, std::vector<uint8_t>& out, size_t max_bytes);
bool write_side(const std::string& planet_file, const wchar_t* suffix, const std::vector<uint8_t>& data);

// BUG-9261 (Lineages port phase 3): a partner's own creature archived with the host's planet, so its nest (its village
// and nation later) loads while that partner is offline. One file per creature next to the save,
// `<planet>.spo.duoguest-<KEY>` (KEY: the creature's 0x7C...... key in hex; the content is creations.cpp's archive:
// u32 'DGA1', u32 key, u8 owner, the creation's bundle). 16 creatures and 8 MB each at most; an archive is never
// replaced by a different one under the same key. creations::restore_guest_model registers them before the planet loads.
bool model_ids(const std::string& planet_file, std::vector<uint32_t>& ids, std::string& err);
bool load_guest_model(const std::string& planet_file, uint32_t id, std::vector<uint8_t>& data, std::string& err);
bool store_guest_model(const std::string& planet_file, uint32_t id, const std::vector<uint8_t>& data, std::string& err);

// BUG-9268 (Lineages port phase 5, from PR #196's founders::Roster): the founders of a stage, kept next to the planet's
// save. Tribe: `<planet>.spo.duofounders`, one village per party seat that plays a species of its own. One change from
// Lineages: an entry's index is the player's PARTY seat (party.h; seat 0 the host's village, the native player tribe),
// not the order the villages were founded in, so a seat playing the host's species (no village of its own) leaves its
// entry empty. Format DUO3 (Lineages' DUO1 / DUO2 are founder-ordered and not read): u32 'DUO3', u8 n (1..kMaxSeats),
// n x {u32 species (the creature key instance the village was founded with), u32 faction (the village's political id,
// cTribe +0x30), u64 player identity (0: not known)}. docs/re/tribe.md#tribe-own-villages.
struct Founder {
    uint32_t species = 0, faction = 0;
    uint64_t player = 0; // identity.h
    bool founded() const { return species && faction; }
    bool operator==(const Founder&) const = default;
};
struct Roster {
    std::array<Founder, kMaxSeats> entries{};
    // one past the last founded entry
    size_t seats() const
    {
        size_t n = 0;
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].founded())
                n = i + 1;
        return n;
    }
    int seat_of_faction(uint32_t faction) const
    {
        if (faction)
            for (size_t i = 0; i < entries.size(); ++i)
                if (entries[i].founded() && entries[i].faction == faction)
                    return int(i);
        return -1;
    }
    bool contains_faction(uint32_t faction) const { return seat_of_faction(faction) >= 0; }
    bool operator==(const Roster&) const = default;
};
// every founded entry has its own species and faction, an identity is in one entry only; the host's entry (0) is founded
// whenever another one is
inline bool roster_valid(const Roster& v)
{
    if (v.seats() > 1 && !v.entries[0].founded())
        return false;
    for (size_t i = 0; i < v.entries.size(); ++i) {
        const Founder& a = v.entries[i];
        if (!a.founded() && (a.species || a.faction))
            return false;
        for (size_t j = i + 1; a.founded() && j < v.entries.size(); ++j) {
            const Founder& b = v.entries[j];
            if (b.founded() && (a.species == b.species || a.faction == b.faction || (a.player && a.player == b.player)))
                return false;
        }
    }
    return true;
}
enum class Stage { Tribe, Civilization };
// false: none, unreadable or invalid
bool read(const std::string& planet_file, Roster& out, Stage stage = Stage::Tribe);
// written whole (a seat's village may be founded again after it was lost: its faction changes)
bool record(const std::string& planet_file, const Roster& value, std::string& err, Stage stage = Stage::Tribe);
std::string describe(const Roster& value);

} // namespace founders
