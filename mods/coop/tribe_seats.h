// BUG-9268 (Lineages port phase 5, from PR #196's tribe_seats.h): every player's own tribe. The pure parts of
// tribe_villages.cpp: who owns a member, who shares the host's village, when a partner's village is founded, how many
// founder villages the native counts leave out, the alliance and the food line. Portable (no Windows, no game):
// tests/tribe_seats_tests.cpp. docs/re/tribe.md#tribe-own-villages.
//
// Seats are the party's (party.h, founders::Roster by party seat): 0 the host's village (the native player tribe), 1..
// the partners'. A partner who plays a species of their own has a village of it (an NPC tribe of the host's game, its
// members theirs); a partner who plays the host's species shares the host's village as before (0.7.15: the player
// tribe's members dealt between the players).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace tribe_seats {

constexpr uint8_t kNobody = 0xFE; // tribe_logic::kNobody

// ---- members' owners (host) ----
// a member of a partner's village: the seat's player while connected, else nobody (the village runs by itself)
inline uint8_t village_owner(uint8_t seat_player) { return seat_player >= 0xFE ? kNobody : seat_player; }

// the players who share the host's village: every player here without a village of their own (sorted as `players`)
inline std::vector<uint8_t> shared_players(const std::vector<uint8_t>& players, const std::vector<uint8_t>& villaged)
{
    std::vector<uint8_t> out;
    for (uint8_t p : players)
        if (std::find(villaged.begin(), villaged.end(), p) == villaged.end())
            out.push_back(p);
    return out;
}

// a member of the host's village whose dealt or claimed owner has a village of their own goes to the players who share
// it, in turns (`turn` as tribe_logic's dealing counter); an owner without a village keeps it
inline uint8_t shared_owner(uint8_t owner, const std::vector<uint8_t>& villaged, const std::vector<uint8_t>& shared,
                            uint32_t& turn)
{
    if (std::find(villaged.begin(), villaged.end(), owner) == villaged.end())
        return owner;
    return shared.empty() ? kNobody : shared[turn++ % shared.size()];
}

// ---- founding (host) ----
enum class Wait : uint8_t {
    Ready = 0,
    HostSpecies, // plays the host's species (or none of its own): shares the host's village
    NoProfile,   // its creature is not on this game yet (records not received)
    Settling,    // the world or its creature changed moments ago (its creature for this world may still come)
    HasVillage,  // its seat has a living village
    OtherSeat,   // the species is another seat's village's
    Full,        // no free seat (party.h kPlaySeats)
};
inline const char* wait_name(Wait w)
{
    switch (w) {
    case Wait::Ready: return "ready";
    case Wait::HostSpecies: return "plays the host's species";
    case Wait::NoProfile: return "its creature is not here yet";
    case Wait::Settling: return "settling";
    case Wait::HasVillage: return "has its village";
    case Wait::OtherSeat: return "another seat's species";
    case Wait::Full: return "no seat";
    }
    return "?";
}
constexpr double kSettleWorld = 15, kSettleKey = 10; // s (Lineages BUG-9137: the partner's creature comes after the world)
struct FoundingIn {
    int seat = -1;               // the player's party seat (-1: none)
    uint32_t species = 0;        // the player's creature (key instance), 0 none
    uint32_t host_species = 0;   // the host's village's species
    bool profile = false;        // the species profile exists on the host's game
    bool village = false;        // the seat has a living village now
    bool other_seat = false;     // a living village of another seat (or the host's) is of this species
    double world_age = 0;        // s since the world was entered
    double key_age = 0;          // s since the player's creature last changed
};
inline Wait founding_wait(const FoundingIn& in)
{
    if (in.seat < 1)
        return Wait::Full;
    if (!in.species || in.species == in.host_species)
        return Wait::HostSpecies;
    if (in.village)
        return Wait::HasVillage;
    if (in.other_seat)
        return Wait::OtherSeat;
    if (!in.profile)
        return Wait::NoProfile;
    if (in.world_age < kSettleWorld || in.key_age < kSettleKey)
        return Wait::Settling;
    return Wait::Ready;
}

// ---- the native counts (h_NounData, Lineages BUG-9047 / BUG-9059) ----
// The NPC tribe controller counts the tribe nouns to pick how many rivals the planet gets and when the first one comes;
// a founder village is no rival: the count it reads leaves out every partner village in the list (each seat once).
// `factions`: the political ids of the list's tribes; `partner`: the founder villages' factions (seats 1..)
inline size_t hidden_villages(const std::vector<uint32_t>& factions, const std::vector<uint32_t>& partner)
{
    std::vector<uint32_t> seen;
    for (uint32_t f : factions)
        if (f && std::find(partner.begin(), partner.end(), f) != partner.end() &&
            std::find(seen.begin(), seen.end(), f) == seen.end())
            seen.push_back(f);
    // never below the host's own village (the count's callers read count - 1)
    return factions.empty() ? 0 : std::min(seen.size(), factions.size() - 1);
}

// ---- the alliance between the players' villages ----
// the relation change that makes two founder villages allies (the game's allied threshold `allied`, manager +0x1C):
// up to one over it, never down; 0 when they are allies already or the values are not usable
inline float ally_delta(float now, float allied)
{
    if (!(allied > 0) || now != now || allied != allied || now >= allied + 0.5f)
        return 0.f;
    return allied + 1.f - now;
}

// ---- the food line (Co-op window, coop.tribe) ----
// host: every founder village's food by seat (negative: no village); "" with no partner village
inline std::string food_line(const std::vector<float>& food, const std::vector<std::string>& names)
{
    std::string out;
    char b[96];
    for (size_t i = 0; i < food.size() && i < names.size(); ++i) {
        if (food[i] < 0)
            continue;
        snprintf(b, sizeof b, "%s%s %.0f", out.empty() ? "Village food   " : "   ", names[i].c_str(), food[i]);
        out += b;
    }
    size_t villages = 0;
    for (float f : food)
        villages += f >= 0;
    return villages >= 2 ? out : std::string();
}

} // namespace tribe_seats
