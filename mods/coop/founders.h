// Campaign-scoped founding species. Political IDs here belong to the host's save.
#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace founders {

// A new campaign reserves its capacity before the first founder starts.
// The persistent roster binds individual species separately.
struct CampaignRules {
    uint8_t seats = 2;
    uint16_t difficulty_percent = 100;
    uint16_t extra_health_percent = 15, extra_damage_percent = 5;
    uint16_t extra_hunger_basis_points = 250; // 100 basis points = 1%
    float hunger_factor() const { return 1.f + (seats - 1u) * extra_hunger_basis_points / 10000.f; }
    uint32_t enemy_health_percent() const { return 100u + (seats - 1u) * extra_health_percent; }
    uint32_t enemy_damage_percent() const { return 100u + (seats - 1u) * extra_damage_percent; }
    bool operator==(const CampaignRules&) const = default;
    uint32_t workload_percent() const { return 100u + (seats - 1u) * difficulty_percent; }
    uint32_t rival_target(uint16_t baseline) const {
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
    int award(int raw, bool plant, const CampaignRules& rules) {
        if (raw <= 0) return raw;
        const auto divisor = rules.workload_percent();
        auto& carry = remainder[plant ? 1 : 0];
        const uint64_t earned = uint64_t(raw) * 100u + carry;
        carry = uint32_t(earned % divisor);
        return int(earned / divisor);
    }
};
bool save_cell_credit(const std::string& planet, const CampaignRules& rules, const CellCredit& credit);
bool read_cell_credit(const std::string& planet, const CampaignRules& rules, CellCredit& credit);
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
bool reserve(const std::string& planet, const CampaignRules& requested, std::string& err);

struct Founder {
    uint32_t species = 0, faction = 0;
    bool operator==(const Founder&) const = default;
};

struct Roster {
    // Persistent campaign slots, NOT reusable network player IDs. Slots 0/1
    // are the existing duo founders. The additional slots are reserved until
    // four-species gameplay and its versioned save format are qualified.
    std::array<Founder, 4> entries{};
    Roster() = default;
    Roster(uint32_t home_species, uint32_t guest_species, uint32_t home_faction, uint32_t guest_faction)
        : entries{{{home_species, home_faction}, {guest_species, guest_faction}}} {}
    const Founder& home() const { return entries[0]; }
    const Founder& guest() const { return entries[1]; }
    const Founder* by_faction(uint32_t faction) const
    {
        if (faction) for (const auto& entry : entries)
            if (entry.species && entry.faction == faction) return &entry;
        return nullptr;
    }
    bool contains_faction(uint32_t faction) const { return by_faction(faction) != nullptr; }
    bool operator==(const Roster&) const = default;
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
    std::array<std::vector<uint32_t>, 2> species;
    uint32_t current(size_t seat) const { return seat < species.size() && !species[seat].empty() ? species[seat].back() : 0; }
    bool contains(size_t seat, uint32_t key) const;
};
bool read_outfits(const std::string& planet, Outfits& out, std::string& err);
// Expected-current check prevents a delayed editor result overwriting a newer one.
bool record_outfit(const std::string& planet, size_t seat, uint32_t expected, uint32_t model, std::string& err);
// Built-in creatures need no model archive. Each explicitly archived model has an immutable key.
bool model_ids(const std::string& planet_file, std::vector<uint32_t>& ids, std::string& err);
bool load_guest_model(const std::string& planet_file, uint32_t id, std::vector<uint8_t>& data, std::string& err);
bool store_guest_model(const std::string& planet_file, uint32_t id, const std::vector<uint8_t>& data, std::string& err);

} // namespace founders
