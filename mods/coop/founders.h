// The campaign's founder seats, kept next to the planet's save (Lineages port, from PR #196's founders.h).
//
// Phase 2 of the port (BUG-9260) brings only the party: one seat per player who ever played this planet. The founder
// rosters of the stages (Tribe villages, Civilization nations), the difficulty rules and the outfit histories of
// Lineages come with the phases that use them; they index the same seats.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace founders {

// seats a campaign file can hold (the host's and up to seven partners'); the game plays kPlaySeats of them for now
constexpr size_t kMaxSeats = 8;
constexpr size_t kPlaySeats = 4;

// The campaign's party, one seat per player who ever played this planet, persistent next to the save
// (`<planet>.spo.duoparty`, PTY1: u32 'PTY1', u8 seats, per seat u64 player identity, u32 own creature species
// instance, str8 name). Seat 0 is the host's. The Creature nests, Tribe villages and Civilization nations of the
// later phases are indexed by these seats.
struct PartySeat {
    uint64_t player = 0;   // identity.h; 0 = not known yet
    uint32_t species = 0;  // the seat's own creature (its key instance); 0 = plays the host's species
    std::string name;      // the player's name when last seen (for an offline seat)
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

} // namespace founders
