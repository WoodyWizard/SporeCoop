// Every player's own nation in Civilization (BUG-9274; the Lineages port, from PR #196's civ_seats.h, BUG-9135 there).
//
// Pure rules (no game state; tests/civ_seats_tests.cpp): which party seat gets the next capital, when the native site
// manager may found one, which city is the new capital, who commands a seat's cities and vehicles, the owner byte the
// guests get, the team's tech level, progress and victory. civ_nations.cpp feeds them the nations' roster
// (founders::Roster, Stage::Civilization, `<planet>.spo.duonations`: one entry per PARTY seat as the Tribe roster of
// BUG-9268, seat 0 the host's nation = the native player civilization), the Tribe roster and the live cities.
// docs/re/civ.md#civ-own-nations.
//
// One change from Lineages: a seat is the player's party seat (party.h), not the founding order, so a seat that plays
// the host's species has no nation (it shares the host's, the 0.7.15 split) and the roster can have gaps.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "founders.h"

namespace civ_seats {

constexpr uint8_t kNobody = 0xFF;   // party::kNobody: no connected player in a seat
constexpr uint8_t kNoOwner = 0xFE;  // a nation nobody commands now (its player is away): the host's tables
constexpr uint8_t kAwaySeat = 0xFC; // MSG_CIV_STATE city owner byte: a player's nation whose player is away (a guest
                                    // counts it as the team's, nobody commands it)

// the seats the gameplay uses (the host's and up to three partners')
inline bool play_seat(int seat) { return seat >= 0 && seat < (int)founders::kPlaySeats; }

// ---- who gets a capital ----
// A party seat as the host sees it when it decides the next capital.
struct Want {
    int seat = -1;          // the party seat (1..)
    uint32_t species = 0;   // its own species: the Tribe roster's (its village's), else the seat's own creature
    bool known = false;     // that species' model is loaded in the host's game (archived with the planet or present)
};
// The next seat to found a capital for: the lowest seat with its own species (not the host nation's, not another
// nation's or another wanting seat's lower in order) that has no nation in the roster yet; -1 = none now.
inline int next_capital(const founders::Roster& nations, const std::vector<Want>& wants, uint32_t host_species)
{
    int best = -1;
    for (const auto& w : wants) {
        if (w.seat < 1 || !play_seat(w.seat) || !w.species || !w.known || w.species == host_species)
            continue;
        if (nations.entries[w.seat].founded())
            continue; // has its nation (a lost one is not founded again: the game said the nation is gone)
        bool taken = false;
        for (size_t s = 0; s < founders::kMaxSeats; ++s)
            taken |= (int)s != w.seat && nations.entries[s].founded() && nations.entries[s].species == w.species;
        for (const auto& o : wants)
            taken |= o.seat >= 1 && o.seat < w.seat && o.species == w.species;
        if (!taken && (best < 0 || w.seat < best))
            best = w.seat;
    }
    return best;
}

// ---- the founding ----
struct City {
    uint32_t faction = 0; // the city's political id
    bool home = false;    // the city's nation is the native player country (the host's)
    bool blank = false;   // neither the city nor its nation has a species key yet (a fresh native city)
    bool nation = true;   // the city has a nation
    uint64_t id = 0;      // the city object (its address), to tell a new city from those before
};

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
inline const char* site_name(SiteState s)
{
    switch (s) {
    case SiteState::Ready: return "ready";
    case SiteState::NoSite: return "no free site";
    case SiteState::Pending: return "a city on its way";
    case SiteState::Unknown: return "unknown";
    }
    return "?";
}

// After the founding: the new capital's political id. The new city is the one city whose object was not there before;
// it must be a fresh nation (blank, a nation of its own whose id no earlier city had, not in the roster, not the
// host's). 0 = not appeared yet; kAmbiguous = more than one new city, or the new one is not fresh (nothing is bound).
constexpr uint32_t kAmbiguous = 0xFFFFFFFFu;
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

// ---- who commands ----
// The host's owner of a seat's cities and vehicles: seat 0 the host, a partner seat its connected player (kNobody:
// away -> kNoOwner, nobody commands it; its cities and vehicles stay its nation's), -1 (no seat): kNobody.
inline uint8_t seat_owner(int seat, uint8_t seat_player)
{
    if (seat == 0)
        return 0;
    if (seat < 0)
        return kNobody;
    return seat_player != kNobody && seat_player < kAwaySeat ? seat_player : kNoOwner;
}
// The city owner byte MSG_CIV_STATE carries for a nation's city: the player, or kAwaySeat for a team nation nobody
// commands (a guest must still count it as the team's); 0xFE / 0xFF stay "not the players'"
inline uint8_t wire_owner(uint8_t owner, bool team)
{
    return team && owner == kNoOwner ? kAwaySeat : owner;
}
// A guest's view of a city owner byte: the team's (a player's city, the shared capital 0xFD, an away seat's)
inline bool team_owner(uint8_t owner) { return owner < kNoOwner; }

// ---- the team ----
// The native level (+0x298) the cities earn (cCivilization_AddCity: 4 at the planet's city total, 3 over three, 2 with
// one): the team's cities count for every player's nation; never lower than what it has
inline int team_tech(size_t team_cities, int final_count, int have)
{
    int earned = final_count > 0 && team_cities >= (size_t)final_count ? 4 : team_cities > 3 ? 3 : team_cities ? 2 : 1;
    return std::max(have, earned);
}
// The HUD progress (CivMode_Progress: the player nation's cities / the planet's total) with every other player
// nation's cities added
inline float team_progress(float own_fraction, size_t other_team_cities, int final_count)
{
    if (final_count <= 0 || !(own_fraction >= 0))
        return own_fraction;
    return std::min(1.f, own_fraction + (float)other_team_cities / (float)final_count);
}
// The team won the planet: only the players' nations are left (every live city is a team city) and the team holds
// the planet's city total (no free site left to found an NPC nation's city on)
inline bool team_won(size_t team_cities, size_t live_cities, int final_count)
{
    return team_cities > 0 && team_cities == live_cities && final_count > 0 && team_cities >= (size_t)final_count;
}

// Two team nations hurt or take nothing of each other (the players' nations are allied for good)
inline bool friendly_fire(bool target_team, bool attacker_team, uint32_t target, uint32_t attacker)
{
    return target_team && attacker_team && target && attacker && target != attacker;
}

} // namespace civ_seats
