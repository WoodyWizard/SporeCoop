// Campaign-scoped founding species. Political IDs here belong to the host's save.
#pragma once

#include <algorithm>
#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace founders {

// The highest Difficulty Scaling a new campaign accepts (player decision, 41-dev).
// Saved campaigns keep whatever they were created with.
constexpr unsigned kMaxDifficultyPercent = 400;

// A new campaign reserves its capacity before the first founder starts.
// The persistent roster binds individual species separately.
struct CampaignRules {
    uint8_t seats = 2;
    uint16_t difficulty_percent = 100;
    uint16_t extra_health_percent = 15, extra_damage_percent = 5;
    uint16_t extra_hunger_basis_points = 250; // 100 basis points = 1%
    uint16_t extra_takeover_percent = 5;
    // 0 (campaigns before 41-dev): difficulty_percent is the progress added per extra seat and the
    // other extras are fixed. 1 (BUG-9100): difficulty_percent scales every per-seat extra, and
    // progress adds kProgressPerSeat percent per extra seat at 100%.
    uint8_t model = 0;
    static constexpr uint32_t kProgressPerSeat = 60;
    // The whole bonus of the extra seats, rounded to the nearest unit.
    uint32_t extra_total(uint32_t per_seat) const {
        const uint64_t seats_over = seats > 1 ? seats - 1u : 0u;
        if (!model) return uint32_t(seats_over * per_seat);
        return uint32_t((seats_over * per_seat * difficulty_percent + 50u) / 100u);
    }
    uint32_t takeover_percent() const { return 100u + extra_total(extra_takeover_percent); }
    float hunger_factor() const { return 1.f + extra_total(extra_hunger_basis_points) / 10000.f; }
    uint32_t enemy_health_percent() const { return 100u + extra_total(extra_health_percent); }
    uint32_t enemy_damage_percent() const { return 100u + extra_total(extra_damage_percent); }
    bool operator==(const CampaignRules&) const = default;
    uint32_t workload_percent() const {
        return 100u + (model ? extra_total(kProgressPerSeat) : (seats - 1u) * difficulty_percent);
    }
    // 43-dev (player decision): a Cell predator group gets one extra member from three reserved
    // seats on (before: one per extra seat, three at four seats). Seats still raise difficulty.
    unsigned cell_extra_predators() const { return seats >= 3 ? 1u : 0u; }
    // The extra predators' bodies add meat; plant food is worth this much more while they spawn, so
    // an omnivore stays reachable (BUG-9110).
    static constexpr uint32_t kCellPlantBonusPercent = 25;
    uint32_t cell_plant_percent() const { return cell_extra_predators() ? 100u + kCellPlantBonusPercent : 100u; }
    static constexpr unsigned native_fleet_limit = 36;
    static constexpr unsigned native_military_interval_ms = 40000;
    static constexpr unsigned max_fleet_overflow = 11; // ceil(36 * 30%) at four seats
    unsigned civ_military_limit(unsigned base) const {
        const unsigned extra = (base * (workload_percent() - 100u) + 999u) / 1000u;
        return std::min<unsigned>(base + extra, native_fleet_limit + max_fleet_overflow);
    }
    unsigned civ_overflow_slots(unsigned base, unsigned total, unsigned military) const {
        if (base > native_fleet_limit || total < native_fleet_limit) return 0;
        const unsigned limit = civ_military_limit(base);
        const unsigned fleet_limit = native_fleet_limit + std::min<unsigned>(limit - base, max_fleet_overflow);
        if (total >= fleet_limit || military >= limit) return 0;
        return std::min<unsigned>(fleet_limit - total, limit - military);
    }
    unsigned civ_military_interval_ms() const {
        // The native AI compares elapsed milliseconds to 40,000. Keep a
        // positive interval even with exceptionally high custom difficulty.
        const unsigned reduction = workload_percent() - 100u;
        return reduction < 1000u ? native_military_interval_ms - 40u * reduction : 1u;
    }
    uint32_t rival_target(uint16_t baseline) const {
        if (model) {
            const uint64_t seats_over = seats > 1 ? seats - 1u : 0u;
            return baseline + uint32_t((baseline * seats_over * kProgressPerSeat * difficulty_percent + 9999u) / 10000u);
        }
        const uint32_t added = (uint32_t(baseline) * difficulty_percent + 99u) / 100u;
        return baseline + (seats - 1u) * added;
    }
};
struct CellCredit {
    std::array<uint32_t,2> remainder{};
    uint8_t health_carry = 0, damage_carry = 0;
    static uint32_t bonus(unsigned base, uint32_t percent, uint8_t& carry) {
        const uint64_t total = uint64_t(base) * (percent - 100u) + carry;
        carry = uint8_t(total % 100);
        return uint32_t(total / 100);
    }
    // plant_percent: plant food's worth (BUG-9110), 100 = as the native game gives it
    int award(int raw, bool plant, const CampaignRules& rules, uint32_t plant_percent = 100) {
        if (raw <= 0) return raw;
        const auto divisor = rules.workload_percent();
        auto& carry = remainder[plant ? 1 : 0];
        const uint64_t earned = uint64_t(raw) * (plant ? plant_percent : 100u) + carry;
        carry = uint32_t(earned % divisor);
        return int(earned / divisor);
    }
};
bool save_cell_credit(const std::string& planet, const CampaignRules& rules, const CellCredit& credit);
bool read_cell_credit(const std::string& planet, const CampaignRules& rules, CellCredit& credit);
struct ConversionCredit {
    uint32_t city = 0, owner = 0, attacker = 0, remainder = 0;
    int apply(int raw, uint32_t percent) {
        if (raw <= 0 || percent < 100) return raw;
        const uint64_t total = uint64_t(raw)*100u + remainder;
        remainder = uint32_t(total % percent);
        return int(total / percent);
    }
    bool operator==(const ConversionCredit&) const = default;
};
uint64_t checkpoint_stamp(const std::string& planet);
bool save_conversion_credit(const std::string& planet, uint32_t percent, const std::vector<ConversionCredit>& credit);
bool read_conversion_credit(const std::string& planet, uint32_t percent, std::vector<ConversionCredit>& credit);
enum class RulesFile { Missing, Saved, Unreadable };
// Stages are ordered Cell (1) through Space (5). Zero means no queued change.
struct SeatPlan {
    uint8_t protected_seats = 1;
    uint8_t next_seats = 0, queued_stage = 0;
    // A native crash can return to Civ while the Space tutorial is unsaved.
    // Nonzero entries preserve the capacity of completed stages.
    std::array<uint8_t,5> previous_stage_seats{};
    uint8_t seats_in(uint8_t stage, uint8_t current) const {
        if (!stage || stage > previous_stage_seats.size()) return current;
        const auto saved = previous_stage_seats[stage-1];
        return saved ? (saved < protected_seats ? protected_seats : saved) : current;
    }
    bool due(uint8_t stage, bool space_reload) const {
        return next_seats && (stage > queued_stage || (stage == 5 && queued_stage == 5 && space_reload));
    }
    bool operator==(const SeatPlan&) const = default;
};
RulesFile read_rules(const std::string& planet, CampaignRules& value, SeatPlan* plan = nullptr);
bool queue_seats(const std::string& planet, uint8_t seats, uint8_t stage, std::string& error);
bool update_seats(const std::string& planet, uint8_t occupied, uint8_t stage, bool space_reloaded,
                  CampaignRules& rules, SeatPlan& plan, std::string& error);
// Owner decision 2026-11-12 (docs/ROADMAP.md, joining late): the host raises the reserved seats of the running stage at
// once, so a late joiner past the reserved founders fits without waiting for the next stage; the difficulty of the
// extra seats applies at once too. Only up (a shrink stays queued for the next stage: a founder's village or nation of
// this stage may own the seat). A queued change to this many seats or fewer is dropped; one to more seats stays.
// Space keeps its own rule (from the next Space load). False: not a raise, or not a regular stage.
inline bool raise_now(CampaignRules& rules, SeatPlan& plan, uint8_t stage, uint8_t seats)
{
    if (!stage || stage > plan.previous_stage_seats.size() || seats > 4 || seats <= plan.seats_in(stage, rules.seats))
        return false;
    // a stage the game returned to after its next one began (a crash before the next stage's first save): that
    // stage's own entry is the count it uses
    if (plan.previous_stage_seats[stage - 1]) plan.previous_stage_seats[stage - 1] = seats;
    else rules.seats = seats;
    if (plan.next_seats && plan.next_seats <= seats) plan.next_seats = plan.queued_stage = 0;
    return true;
}
bool raise_seats(const std::string& planet, uint8_t seats, uint8_t stage, std::string& error);
bool reserve(const std::string& planet, const CampaignRules& requested, std::string& err);

// BUG-9135: founder seats a campaign can persist: the host (seat 0) and up to seven partners. The gameplay of the
// stages still binds seats 0 and 1 (tribe.cpp / civ.cpp); the save formats and the player identity carry more.
constexpr size_t kMaxSeats = 8;

struct Founder {
    uint32_t species = 0, faction = 0;
    // the founder's persistent player identity (identity.h), 0 = not known (a DUO1 / CIV1 roster, an older partner)
    uint64_t player = 0;
    bool operator==(const Founder&) const = default;
};

struct Roster {
    // Persistent campaign slots, NOT reusable network player IDs. Slot 0 is the host's founder, slot 1 the first
    // partner's; slots 2.. the further partners' (DUO2 / CIV2 only).
    std::array<Founder, kMaxSeats> entries{};
    Roster() = default;
    Roster(uint32_t home_species, uint32_t guest_species, uint32_t home_faction, uint32_t guest_faction)
        : entries{{{home_species, home_faction}, {guest_species, guest_faction}}} {}
    const Founder& home() const { return entries[0]; }
    const Founder& guest() const { return entries[1]; }
    // founded seats (they are contiguous from seat 0)
    size_t seats() const
    {
        size_t n = 0;
        while (n < entries.size() && entries[n].species && entries[n].faction) n++;
        return n;
    }
    const Founder* by_faction(uint32_t faction) const
    {
        if (faction) for (const auto& entry : entries)
            if (entry.species && entry.faction == faction) return &entry;
        return nullptr;
    }
    int seat_of_faction(uint32_t faction) const
    {
        const Founder* f = by_faction(faction);
        return f ? int(f - entries.data()) : -1;
    }
    int seat_of_player(uint64_t player) const
    {
        if (player) for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].species && entries[i].player == player) return int(i);
        return -1;
    }
    bool contains_faction(uint32_t faction) const { return by_faction(faction) != nullptr; }
    bool operator==(const Roster&) const = default;
    // the same founded seats (species and nation of each), whoever plays them: a roster sent over the network carries
    // no player identities (BUG-9187)
    bool same_seats(const Roster& o) const
    {
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].species != o.entries[i].species || entries[i].faction != o.entries[i].faction) return false;
        return true;
    }
};

// planet_file is world::current_game_key(), or spaceext::game_key() in Space.
enum class Stage { Tribe, Civilization };
bool read(const std::string& planet_file, Roster& out, Stage stage = Stage::Tribe);
bool can_record(const std::string& planet_file, const Roster& value, std::string& err, Stage stage = Stage::Tribe);
bool record(const std::string& planet_file, const Roster& value, std::string& err, Stage stage = Stage::Tribe);
std::string describe(const Roster& value);
bool offline_control(const std::string& planet, const Roster& nations);
bool set_offline_control(const std::string& planet, const Roster& nations, bool enabled);

// Opt-in six-rival qualification on a copy of a three-victory Tribal save.
struct SixRivals { uint32_t extra = 0, deferred = 0, outcome = 0; };
bool read_six_rivals(const std::string& planet, const Roster& founders, SixRivals& trial);
bool save_six_rivals(const std::string& planet, const Roster& founders, const SixRivals& trial, bool create);
// Outfit keys are versions of a founder, not new founders. The first key in
// each history is the immutable founding species; the last is its current look.
struct Outfits {
    std::array<std::vector<uint32_t>, kMaxSeats> species;
    uint32_t current(size_t seat) const { return seat < species.size() && !species[seat].empty() ? species[seat].back() : 0; }
    bool contains(size_t seat, uint32_t key) const;
};
// BUG-9135 (roadmap phase 4): a Civilization roster descends from the Tribe roster. A seat with a tribal founder
// (seat < tribe.seats()) plays one of that founder's looks (`looks`: the outfit histories, Outfits::contains). A seat
// past the Tribe roster is a late joiner's (decision 5: a capital without a former village): it needs its player's
// identity and a species that is no tribal founder's look. Before phase 4 every seat needed a tribal founder.
// Pure (tests/civ_seats_tests.cpp); can_record uses it for Stage::Civilization when a Tribe roster exists.
inline bool nations_descend(const Roster& tribe, const Outfits& looks, const Roster& nations)
{
    auto look_of = [&](size_t seat, uint32_t key) {
        const auto& history = looks.species[seat];
        return key && std::find(history.begin(), history.end(), key) != history.end();
    };
    const size_t tribal = std::min(tribe.seats(), kMaxSeats);
    for (size_t i = 0; i < nations.seats(); ++i) {
        const Founder& f = nations.entries[i];
        if (i < tribal) {
            if (!look_of(i, f.species)) return false;
            continue;
        }
        if (i == 0 || !f.player) return false; // the host's seat always has a tribal founder; a late seat a player
        for (size_t t = 0; t < tribal; ++t)
            if (look_of(t, f.species)) return false;
    }
    return true;
}
bool read_outfits(const std::string& planet, Outfits& out, std::string& err);
// Expected-current check prevents a delayed editor result overwriting a newer one.
bool record_outfit(const std::string& planet, size_t seat, uint32_t expected, uint32_t model, std::string& err);
// Built-in creatures need no model archive. Each explicitly archived model has an immutable key.
bool model_ids(const std::string& planet_file, std::vector<uint32_t>& ids, std::string& err);
bool load_guest_model(const std::string& planet_file, uint32_t id, std::vector<uint8_t>& data, std::string& err);
bool store_guest_model(const std::string& planet_file, uint32_t id, const std::vector<uint8_t>& data, std::string& err);
// BUG-9150 (owner decision 14, "1B"): founder seats founded with a stand-in (a copy of the host's creature under a key of
// the late joiner's own, archived like a partner's creature) until their player has a creature of their own. The
// stand-in keys of a planet, <planet>.duostandins (up to kMaxSeats u32, little endian; a missing file: none).
bool read_stand_ins(const std::string& planet_file, std::vector<uint32_t>& keys);
bool add_stand_in(const std::string& planet_file, uint32_t key, std::string& err);

// BUG-9135 / roadmap 0b: the campaign's party, one seat per player who ever played this planet, persistent next to
// the save (`.duoparty`, PTY1: u8 seats, per seat u64 player identity, u32 own creature species instance, str8 name).
// Seat 0 is the host's. The seats of the founder rosters (Tribe villages, Civilization nations) and the Creature
// nests are these seats. Data structures hold kMaxSeats; the game allows kPlaySeats players for now (owner decision).
constexpr size_t kPlaySeats = 4;
struct PartySeat {
    uint64_t player = 0;   // identity.h; 0 = not known yet (a seat taken over from a two-founder save)
    uint32_t species = 0;  // the seat's own creature (its key instance); 0 = plays the host's species
    std::string name;      // the player's name when last seen (for an offline seat)
    bool operator==(const PartySeat&) const = default;
};
struct Party {
    std::vector<PartySeat> seats;
    int seat_of(uint64_t player) const
    {
        if (player) for (size_t i = 0; i < seats.size(); ++i)
            if (seats[i].player == player) return int(i);
        return -1;
    }
    bool operator==(const Party&) const = default;
};
bool read_party(const std::string& planet_file, Party& out);
// BUG-9189: a mod side file next to the planet's save (planet_file + suffix, never inside the game's save), written
// whole through a temporary file. read: false = none, unreadable or larger than max_bytes.
bool read_side(const std::string& planet_file, const wchar_t* suffix, std::vector<uint8_t>& out, size_t max_bytes);
bool write_side(const std::string& planet_file, const wchar_t* suffix, const std::vector<uint8_t>& data);
// a party may only grow, and a seat's identity is learnt once: the same rule as the founder rosters
bool record_party(const std::string& planet_file, const Party& value, std::string& err);

} // namespace founders
