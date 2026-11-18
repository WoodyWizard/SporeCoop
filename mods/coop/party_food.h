// BUG-9154 (owner decision 2P, 2026-11-12): the party's share of the food a player gathers in Tribe.
// "Let's do 25% is split ... if the 25% split equals 0.2, it'll always be at least 1, even if the total is more than
// obtained legitimately": a quarter of each delivery goes to the partners in the game, split evenly, each partner at
// least 1 food (a party bonus when the quarter is small); the gatherer keeps three quarters. Pure logic, host tests.
#pragma once

#include <algorithm>

namespace party_food {

constexpr double kShare = 0.25;    // of each delivery, for the partners together
constexpr double kMinEach = 1.0;   // food each partner gets at least
constexpr double kDelivery = 0.5;  // a rise smaller than this is not a delivery (float noise, fractions)

struct Split {
    double keep = 0; // what the gatherer's village keeps of the delivery
    double each = 0; // what every partner's village gets
};

// a delivery of `gathered` food by a player with `partners` partners in the game
inline Split split(double gathered, int partners)
{
    if (!(gathered >= kDelivery) || partners <= 0) return {gathered > 0 ? gathered : 0, 0};
    const double give = gathered * kShare;
    return {gathered - give, std::max(give / partners, kMinEach)};
}

// the food the share adds to the party beyond the delivery (the minimum's bonus)
inline double created(double gathered, int partners)
{
    const Split s = split(gathered, partners);
    return s.keep + s.each * (partners > 0 ? partners : 0) - (gathered > 0 ? gathered : 0);
}

// a gift between party villages: true when the giver can pay `price` (the game's own gift cost)
inline bool can_gift(double giver_food, double price) { return price >= 1 && giver_food >= price; }

} // namespace party_food
