// Players' old cells as NPC species (BUG-1477, idea and RE of PR #211 by IltonBJSilva; docs/re/cell.md
// #cell-lineage-cells-0717): the portable rules, no game calls (tests/cell_tests.cpp lineage()). The module is
// lineage_cells.{h,cpp}; docs/01-architecture.md#arch-lineage-cells.
//
//   * which cells: each player's own My Creations cells (newest first, at most kMaxPerPlayer), never the cell the
//     player plays now and never a premade (BUG-1476's game cells); a guest's only while its co-op window's box
//     "my old cells as wild species" is ticked (its consent, never by default), the host's own once it switched the
//     session setting cell.lineage_cells on (default off: settings::kLineageCellsDefault)
//   * the wire: MSG_CREATION kind 12 (creations 1.2): owner = the cell's player, id = the cell's shared instance
//     (0x7B + its content hash, species_line_core.h shared_instance: the same key on every game), the payload the
//     bundle of creations::line_pack; total 0 = withdrawn (id 0: every cell of that owner). A guest sends to the host,
//     the host relays to every peer of creations 1.2 or newer (an older peer drops kind 12 as an unknown kind and is
//     never sent one) and to players joining later
//   * where: a native spawn of the population (PopulateMarker -> CreateCellObject -> GetModelKeyForCellResource, the
//     kin filter's chain) of a creature species (cell type 2 with an AI) at the host's level or below (a species of
//     the levels above is drawn large: no giants, BUG-9285 of #211), not the player species (that is the kin's,
//     BUG-1257), takes a pool cell in kSharePercent of the spawns; the cell's diet must fit the native species'
//     (carnivore / herbivore mouths, GetCellModelData +0x1250 / +0x1251), except under the prey-and-threat AI 0x1006,
//     which picks its prey by the model's own mouths (#211's RE)
//   * only while every peer reads creations 1.2 and has had the cell's records for kLead seconds (a guest's mirror
//     of an unknown key is not made: BUG-0455), so a 0.7.16.1 peer plays as with 0.7.16.1
//   * the pool is not saved: the population's cells are not in a Cell save (#211's RE; coop_lineage_cells.py parses
//     a fresh save of the host to check)
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace lineage {

constexpr unsigned kSharePercent = 17;   // of a native creature species' spawns (#211: about one in six)
constexpr size_t kMaxPerPlayer = 16;     // own cells a player shares (newest first)
constexpr size_t kMaxPool = 64;          // cells in the host's pool
constexpr double kLead = 5.0;            // s: a cell's records reach every peer before its first use
constexpr uint16_t kCreationsMinor = 2;  // creations 1.2 (0.7.17) reads kind 12
constexpr uint32_t kSharedPrefix = 0x7B000000u;
constexpr int kPreyAi = 0x1006;          // the prey-and-threat AI: its prey follows the model's mouths

// a player offers its old cells: the session setting is on and (a guest) its own box is ticked, and the host reads
// kind 12 (a guest asks host_families, the host sends to each peer that reads it)
inline bool offers(bool setting, bool is_host, bool consent) { return setting && (is_host || consent); }

// the host swaps spawns only while every peer reads kind 12
inline bool host_swaps(bool setting, const std::vector<bool>& peers_read)
{
    if (!setting)
        return false;
    for (bool r : peers_read)
        if (!r)
            return false;
    return true;
}

// the cells a player offers: its own cells (newest first) without the one it plays and without premades
inline std::vector<uint32_t> own_offer(const std::vector<uint32_t>& own_newest_first, uint32_t playing,
                                       const std::vector<uint32_t>& premades)
{
    std::vector<uint32_t> out;
    for (uint32_t k : own_newest_first) {
        if (!k || k == playing || out.size() >= kMaxPerPlayer)
            continue;
        bool pre = false;
        for (uint32_t p : premades)
            pre |= p == k;
        if (!pre)
            out.push_back(k);
    }
    return out;
}

// a candidate's diet fits the native species' (any: the prey-and-threat AI, which follows the model's own mouths)
inline bool diet_fits(bool cand_carn, bool cand_herb, bool native_carn, bool native_herb, bool any)
{
    if (!cand_carn && !cand_herb)
        return false;
    return any || (cand_carn && native_carn) || (cand_herb && native_herb);
}

// a spawn may take a player's cell: a creature with an AI, not the player species, at the host's level or below
inline bool spawn_fits(bool creature_with_ai, bool player_species, int spawn_level, int own_level)
{
    return creature_with_ai && !player_species && spawn_level >= 0 && own_level > 0 && spawn_level <= own_level;
}

// the share roll: r is uniform in 0..99
inline bool roll(unsigned r, unsigned percent) { return r < percent; }

inline bool is_shared(uint32_t inst) { return (inst & 0xFF000000u) == kSharedPrefix && (inst & 0x00FFFFFFu) != 0; }

// one cell of the host's pool
struct Cand {
    uint32_t key = 0;   // the shared instance (cll type, the cell group)
    uint8_t owner = 0;  // whose old cell
    int8_t diet = -1;   // -1 not known yet, else bit 0 carnivore, bit 1 herbivore
    std::map<uint8_t, double> got; // peer -> when its records went to that peer
};

// every peer has had the cell's records for `lead` seconds (the owner has its own)
inline bool ready(const Cand& c, const std::vector<uint8_t>& peers, double now, double lead)
{
    for (uint8_t p : peers) {
        if (p == c.owner)
            continue;
        auto it = c.got.find(p);
        if (it == c.got.end() || now - it->second < lead)
            return false;
    }
    return true;
}

// a withdrawal (kind 12, total 0): which of the pool's cells go
inline bool withdrawn(const Cand& c, uint8_t owner, uint32_t id) { return c.owner == owner && (id == 0 || c.key == id); }

} // namespace lineage
