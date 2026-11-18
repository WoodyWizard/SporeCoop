// BUG-9135 (roadmap phase 5): the Civilization founder nations of up to founders::kPlaySeats players, one per seat.
//
// Pure rules (no game state; tests/civ_seats_tests.cpp): which seat a guest plays, who owns a seat's cities and
// vehicles, which seat gets the next capital, when a new capital may be founded and which nation is the new one;
// roadmap phase 4: who gets a late capital, when the site manager may found one, which city is the new one and what
// the catch-up builds (docs/re/civ.md#civ-late-capital).
// civ.cpp (switch civ.multi_founders) feeds them the founder roster (founders::Roster, Stage::Civilization), the
// Tribe roster, the outfit histories and the live cities. Seat 0 is the host's nation, seats 1.. the partners'.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "founders.h"

namespace civ_seats {

constexpr uint8_t kNobody = 0xFF;    // party::kNobody: no connected player in a seat
constexpr uint8_t kNoOwner = 0xFE;   // civ.cpp: a founder nation nobody may command now (its player is away)
constexpr uint8_t kOtherSeat = 0xFC; // guest: another partner's nation (only the host knows that player's id)

// the seats the gameplay uses: the founded seats, at most founders::kPlaySeats (owner decision 10)
inline size_t play_seats(const founders::Roster& r) { return std::min(r.seats(), founders::kPlaySeats); }

// The seat's species: the nation's recorded species or any look of the seat's tribal founder (the outfit history).
// Outfits::contains is the same test; it lives in founders.cpp, this header stays free of it.
inline bool seat_species(const founders::Roster& r, const founders::Outfits& outfits, int seat, uint32_t species)
{
    if (!species || seat < 0 || seat >= (int)founders::kMaxSeats)
        return false;
    if (r.entries[seat].species == species)
        return true;
    const auto& history = outfits.species[seat];
    return std::find(history.begin(), history.end(), species) != history.end();
}

// Before BUG-9135 every partner commanded the second nation. That rule stays for a two-founder roster whose partner
// seat has no identity yet, while exactly one partner is in the session.
inline bool sole_partner_seat(const founders::Roster& r, int seat, size_t partners_present)
{
    return seat == 1 && r.seats() == 2 && !r.entries[1].player && partners_present == 1;
}

// The seat a guest plays: its identity, else its species on a seat without identity, else the two-founder rule.
// -1: none (not a founder of this campaign, or a seat the guest cannot recognise before the host shares the world).
inline int own_seat(const founders::Roster& r, const founders::Outfits& outfits, uint64_t identity, uint32_t species,
                    size_t partners_present)
{
    const int n = (int)play_seats(r);
    if (identity)
        for (int s = 1; s < n; ++s)
            if (r.entries[s].player == identity)
                return s;
    for (int s = 1; s < n; ++s)
        if (!r.entries[s].player && seat_species(r, outfits, s, species))
            return s;
    return sole_partner_seat(r, 1, partners_present) ? 1 : -1;
}

// Who owns a founder seat's cities and vehicles (the IsPlayerOwned hooks and the host's owner tables).
//   host: the connected player in the seat (seat_player, kNobody = away); an away seat is the host's while offline
//         control is on, else nobody's (kNoOwner)
//   guest: its own seat is its own (me), every other partner seat kOtherSeat
inline uint8_t owner_of_seat(int seat, bool host, uint8_t seat_player, int local_seat, uint8_t me, bool offline_control)
{
    if (seat == 0)
        return 0;
    if (seat < 0)
        return kNobody;
    if (!host)
        return seat == local_seat ? me : kOtherSeat;
    if (seat_player != kNobody)
        return seat_player;
    return offline_control ? 0 : kNoOwner;
}

// The partner seat whose capital comes next: seats are founded in order (a roster has no gaps), each from the Tribe
// roster's seat of the same index (its nation's species descends from that tribal founder, founders::can_record),
// at most founders::kPlaySeats. civ_seats = the Civilization roster's seats (0 = none yet). -1 = nothing to found.
inline int next_capital_seat(size_t civ_seats, size_t tribal_seats)
{
    const size_t wanted = std::min(tribal_seats, founders::kPlaySeats);
    const size_t next = civ_seats ? civ_seats : 1;
    return next < wanted ? (int)next : -1;
}

struct City {
    uint32_t faction = 0; // the city's political id
    bool home = false;    // the city's nation is the native player country (the host's)
    bool blank = false;   // neither the city nor its nation has a species key yet (a fresh native city)
    bool nation = true;   // the city has a nation (one without is no new capital; it still blocks a founding)
    uint64_t id = 0;      // the city object (civ.cpp: its address), to tell a new city from those before (late_*)
};

// A new partner capital is founded only while every city belongs to the host's nation or a founder nation: an
// established NPC nation is never repurposed, and the new nation is told apart from the native ones by being the only
// one left (the rule of the two-founder campaign: "only from the native graduation's single capital").
inline bool only_founder_cities(const std::vector<City>& cities, const founders::Roster& r)
{
    if (cities.empty())
        return false;
    bool home = false;
    for (const auto& c : cities) {
        if (c.home)
            home = true;
        else if (!r.contains_faction(c.faction))
            return false;
    }
    return home;
}

// After the founding: the new nation's political id once its capital appeared. 0 = not yet (the appearance is
// asynchronous); kAmbiguous = another nation or a species key is there too (the identity is retained, nothing bound).
constexpr uint32_t kAmbiguous = 0xFFFFFFFFu;
inline uint32_t new_capital(const std::vector<City>& cities, const founders::Roster& r)
{
    uint32_t found = 0;
    for (const auto& c : cities) {
        if (!c.nation || c.home || r.contains_faction(c.faction))
            continue;
        if (found || !c.blank || !c.faction)
            return kAmbiguous;
        found = c.faction;
    }
    return found;
}

// Identities to learn: a connected partner (its identity, its creature species) whose identity is not seated yet
// belongs to the seat without identity whose species it plays, when that partner matches exactly one such seat and
// no other unseated partner matches that seat too. Returns {seat, index into partners}, each seat at most once.
struct Partner {
    uint64_t identity = 0;
    uint32_t species = 0;
};
inline std::vector<std::pair<int, size_t>> identities_to_bind(const founders::Roster& r, const founders::Outfits& outfits,
                                                              const std::vector<Partner>& partners)
{
    const int n = (int)r.seats();
    auto unseated = [&](const Partner& who) { return who.identity && r.seat_of_player(who.identity) < 0; };
    auto matches = [&](const Partner& who, int s) {
        return unseated(who) && !r.entries[s].player && seat_species(r, outfits, s, who.species);
    };
    std::vector<std::pair<int, size_t>> out;
    for (int s = 1; s < n; ++s) {
        size_t found = 0, count = 0;
        for (size_t p = 0; p < partners.size(); ++p)
            if (matches(partners[p], s)) {
                found = p;
                ++count;
            }
        if (count != 1)
            continue;
        int seats_matched = 0;
        for (int t = 1; t < n; ++t)
            seats_matched += matches(partners[found], t);
        bool twice = false; // the same identity twice in the session (a stale entry): bind nothing
        for (size_t p = 0; p < partners.size(); ++p)
            twice |= p != found && partners[p].identity == partners[found].identity;
        if (seats_matched == 1 && !twice)
            out.push_back({s, found});
    }
    return out;
}

// ---- Roadmap phase 4 (owner decisions 4 and 5): the late capital (civ.cpp, switch civ.late_capital) ----
// A partner who has no founder nation gets one capital at a free native site, also once NPC nations exist, caught up
// to the host's capital. One at a time; seats stay contiguous: the Tribe roster's seats come first (their species is
// the tribal founder's current look), then late joiners in party seat order.

// A connected partner as the host sees it.
struct Joiner {
    int party_seat = -1;        // party::seat_of_player (-1: no seat yet: identity not arrived, or the party is full)
    uint64_t identity = 0;      // identity::of
    uint32_t species = 0;       // its creature (creations::player_key)
    bool own_creature = false;  // a creature of its own (not one of the host's creations, not the host's species)
    bool species_ready = false; // the creature's model is loaded and known here (archivable into the planet)
    int nation_seat = -1;       // its founder seat (party::founder_seat_of with the stage's legacy match), -1 = none
};

enum class LateWhy : uint8_t {
    Capital,        // gets the next capital (the first one of these in party seat order)
    HasNation,      // already a founder: nothing to do
    NoPartySeat,    // no party seat (identity not arrived, or the party is full): wait
    NoOwnCreature,  // plays the host's species: no capital until it creates a creature (decision 4)
    SpeciesLoading, // its creature is not loaded here yet: wait
    SharedSpecies,  // its species is a founder's (a nation's, or a look of a tribal founder): no second nation of it
    SeatsFull,      // every play seat (founders::kPlaySeats) has a nation
    TribalFirst,    // a Tribe roster seat's capital comes first (seat order)
    TribalSeat,     // a tribal founder whose own seat's capital is still to come (founded from the Tribe roster)
};

inline const char* late_why_text(LateWhy why)
{
    switch (why) {
    case LateWhy::Capital: return "gets a capital";
    case LateWhy::HasNation: return "has a nation";
    case LateWhy::NoPartySeat: return "has no party seat yet";
    case LateWhy::NoOwnCreature: return "plays the host's species: no capital until they create a creature of their own";
    case LateWhy::SpeciesLoading: return "waits for their creature to load";
    case LateWhy::SharedSpecies: return "plays a species another nation already has";
    case LateWhy::SeatsFull: return "finds every founder seat taken";
    case LateWhy::TribalFirst: return "waits for a tribal founder's capital first";
    case LateWhy::TribalSeat: return "gets their tribal seat's capital";
    }
    return "?";
}

// The Civilization seat a late joiner takes: the next one after the founded seats (seat 0 is always the host's).
inline size_t late_seat(const founders::Roster& nations) { return std::max<size_t>(nations.seats(), 1); }

inline LateWhy late_status(const founders::Roster& nations, const founders::Roster& tribe, const founders::Outfits& looks,
                           uint32_t host_species, const Joiner& j)
{
    if (j.nation_seat >= 0)
        return LateWhy::HasNation;
    if (j.party_seat < 1 || !j.identity)
        return LateWhy::NoPartySeat;
    // a tribal founder (by identity, or by a look of a seat without one) whose seat has no nation yet: that seat's
    // capital is theirs, founded in seat order from the Tribe roster
    const int tribal_seat = tribe.seat_of_player(j.identity);
    const size_t tribal_end = std::min(tribe.seats(), founders::kPlaySeats);
    if (tribal_seat >= (int)nations.seats() && tribal_seat < (int)tribal_end)
        return LateWhy::TribalSeat;
    for (size_t s = nations.seats(); s < tribal_end; ++s)
        if (!tribe.entries[s].player && seat_species(tribe, looks, (int)s, j.species))
            return LateWhy::TribalSeat;
    if (!j.species || !j.own_creature || j.species == host_species)
        return LateWhy::NoOwnCreature;
    if (!j.species_ready)
        return LateWhy::SpeciesLoading;
    for (size_t s = 0; s < founders::kMaxSeats; ++s)
        if ((s < nations.seats() && nations.entries[s].species == j.species) ||
            (s < tribe.seats() && seat_species(tribe, looks, (int)s, j.species)))
            return LateWhy::SharedSpecies;
    if (late_seat(nations) >= founders::kPlaySeats)
        return LateWhy::SeatsFull;
    if (next_capital_seat(nations.seats(), tribe.seats()) >= 0)
        return LateWhy::TribalFirst;
    return LateWhy::Capital;
}

// What comes next: a Tribe roster seat's capital (joiner -1), or the first late joiner (party seat order) who gets
// one; seat -1 = nothing now. `why` (when given) receives every joiner's status, in the joiners' order.
struct LatePlan {
    int seat = -1;
    int joiner = -1;
};
inline LatePlan late_capital(const founders::Roster& nations, const founders::Roster& tribe, const founders::Outfits& looks,
                             uint32_t host_species, const std::vector<Joiner>& joiners, std::vector<LateWhy>* why = nullptr)
{
    LatePlan plan;
    const int tribal = next_capital_seat(nations.seats(), tribe.seats());
    if (tribal >= 0)
        plan.seat = tribal;
    if (why)
        why->clear();
    int best_party_seat = 0;
    for (size_t i = 0; i < joiners.size(); ++i) {
        const LateWhy w = late_status(nations, tribe, looks, host_species, joiners[i]);
        if (why)
            why->push_back(w);
        if (tribal < 0 && w == LateWhy::Capital && (plan.joiner < 0 || joiners[i].party_seat < best_party_seat)) {
            plan.joiner = (int)i;
            best_party_seat = joiners[i].party_seat;
        }
    }
    if (plan.joiner >= 0)
        plan.seat = (int)late_seat(nations);
    return plan;
}

// May the native site manager found a city now? cCivExpansion +0xAC all sites, +0xB0 sites not founded yet
// (docs/re/civ.md#civ-city-burst): all - left = the cities founded, appeared or still on their way; more than the live
// cities = one is still on its way (then a new city could not be told apart).
enum class SiteState : uint8_t { Ready, NoSite, Pending, Unknown };
inline SiteState site_state(int all, int left, size_t live_cities)
{
    if (all <= 0 || all > 64 || left < 0 || left > all)
        return SiteState::Unknown;
    if (!left)
        return SiteState::NoSite;
    return size_t(all - left) > live_cities ? SiteState::Pending : SiteState::Ready;
}

// After the late founding: the new capital's political id. The new city is the one city whose object was not there
// before; it must be a fresh nation (blank, with a nation of its own whose id no earlier city had, not seated, not the
// host's). 0 = not appeared yet; kAmbiguous = more than one new city, or the new one is not fresh (nothing is bound).
inline uint32_t new_city_since(const std::vector<City>& before, const std::vector<City>& cities, const founders::Roster& r)
{
    auto was = [&](const City& c) {
        for (const auto& b : before)
            if (b.id == c.id)
                return true;
        return false;
    };
    uint32_t found = 0;
    for (const auto& c : cities) {
        if (was(c))
            continue;
        if (found || !c.nation || c.home || !c.blank || !c.faction || r.contains_faction(c.faction))
            return kAmbiguous;
        for (const auto& b : before)
            if (b.faction == c.faction)
                return kAmbiguous;
        found = c.faction;
    }
    return found;
}

// The catch-up (decision 5: the host's capital's buildings, not its empire). A building of one of the catch-up types
// (house, factory, entertainment; civ.cpp type_for_noun) and its layout slot (-1: in none).
struct Building {
    uint32_t noun = 0;
    int slot = -1;
    bool operator==(const Building&) const = default;
};
// For every type, the host capital's buildings beyond the count the new capital has, each with the host building's
// slot as the preferred one (a free slot there keeps the host's layout). Types are taken in turn (one of each type the
// capital lacks per round, in the order the host's slots first show them) so a capital with fewer free slots than
// needed still gets the host's mix; at most `free_slots` buildings.
inline std::vector<Building> catch_up_buildings(std::vector<Building> host, const std::vector<Building>& own, size_t free_slots)
{
    std::stable_sort(host.begin(), host.end(), [](const Building& a, const Building& b) {
        return (a.slot < 0 ? INT32_MAX : a.slot) < (b.slot < 0 ? INT32_MAX : b.slot);
    });
    std::vector<uint32_t> types;
    std::vector<std::vector<Building>> wanted; // per type: the host's buildings the capital lacks, free slots first
    for (const auto& b : host)
        if (std::find(types.begin(), types.end(), b.noun) == types.end())
            types.push_back(b.noun);
    for (uint32_t t : types) {
        size_t have = 0;
        for (const auto& b : own)
            have += b.noun == t;
        std::vector<Building> of_type, taken;
        for (const auto& b : host) {
            if (b.noun != t)
                continue;
            bool busy = b.slot < 0;
            for (const auto& o : own)
                busy |= o.slot == b.slot;
            (busy ? taken : of_type).push_back(b);
        }
        of_type.insert(of_type.end(), taken.begin(), taken.end());
        const size_t need = of_type.size() > have ? of_type.size() - have : 0;
        of_type.resize(need);
        wanted.push_back(std::move(of_type));
    }
    std::vector<Building> out;
    for (size_t round = 0; out.size() < free_slots; ++round) {
        bool any = false;
        for (const auto& list : wanted)
            if (round < list.size() && out.size() < free_slots) {
                out.push_back(list[round]);
                any = true;
            }
        if (!any)
            break;
    }
    return out;
}
// Turrets (City_AddTurret takes the next free wall slot): as many as the host's capital has.
inline size_t catch_up_turrets(size_t host, size_t own) { return host > own ? host - own : 0; }

} // namespace civ_seats
