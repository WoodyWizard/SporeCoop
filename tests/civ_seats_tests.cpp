// BUG-9274: the pure rules of every player's own nation in Civilization (mods/coop/civ_seats.h; Lineages' civ_seats
// tests, adapted to the roster by party seat). Built and run by `make test` (unit-cpp).
#include "civ_seats.h"
#include "founders.h"

#include <cstdio>

static int g_fail;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                   \
            g_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

using namespace civ_seats;

static founders::Roster roster(std::initializer_list<std::pair<int, founders::Founder>> seats)
{
    founders::Roster r;
    for (auto& [s, f] : seats)
        r.entries[s] = f;
    return r;
}

static void next_capitals()
{
    const uint32_t host = 0x065CBE70;
    founders::Roster none;
    // seat 1 has its own species (its Tribe village's) and its model is known: its capital comes
    CHECK(next_capital(none, {{1, 0x7CA81684, true}}, host) == 1);
    // the model is not loaded here: wait
    CHECK(next_capital(none, {{1, 0x7CA81684, false}}, host) == -1);
    // a seat on the host's species shares the host's nation
    CHECK(next_capital(none, {{1, host, true}}, host) == -1);
    // no species (plays the host's creation)
    CHECK(next_capital(none, {{2, 0, true}}, host) == -1);
    // the lowest seat first; a gap (seat 1 shares the host's) is fine
    CHECK(next_capital(none, {{3, 0x7C000003, true}, {2, 0x7C000002, true}, {1, host, true}}, host) == 2);
    // a seat with a nation already: the next one
    auto one = roster({{0, {host, 0x80002274, 1}}, {2, {0x7C000002, 0x22A8, 2}}});
    CHECK(next_capital(one, {{2, 0x7C000002, true}, {3, 0x7C000003, true}}, host) == 3);
    // a species another seat's nation has: no second nation of it
    CHECK(next_capital(one, {{3, 0x7C000002, true}}, host) == -1);
    // two seats on one species: only the lower one
    CHECK(next_capital(none, {{2, 0x7C000009, true}, {1, 0x7C000009, true}}, host) == 1);
    CHECK(next_capital(roster({{0, {host, 1, 0}}, {1, {0x7C000009, 2, 0}}}), {{2, 0x7C000009, true}, {1, 0x7C000009, true}}, host) == -1);
    // past the play seats (founders::kPlaySeats)
    CHECK(next_capital(none, {{(int)founders::kPlaySeats, 0x7C000004, true}}, host) == -1);
}

static void sites_and_new_cities()
{
    CHECK(site_state(10, 9, 1) == SiteState::Ready);
    CHECK(site_state(10, 8, 1) == SiteState::Pending); // one founded, still on its way
    CHECK(site_state(10, 0, 10) == SiteState::NoSite);
    CHECK(site_state(0, 0, 0) == SiteState::Unknown);
    CHECK(site_state(10, 11, 0) == SiteState::Unknown);
    std::vector<City> before{{0x80002274, true, false, true, 1}};
    auto r = roster({{0, {1, 0x80002274, 0}}});
    CHECK(new_city_since(before, before, r) == 0); // not appeared yet
    auto after = before;
    after.push_back({0x22A8, false, true, true, 2});
    CHECK(new_city_since(before, after, r) == 0x22A8);
    // a new city with a species already (not a fresh nation)
    auto keyed = before;
    keyed.push_back({0x22A8, false, false, true, 2});
    CHECK(new_city_since(before, keyed, r) == kAmbiguous);
    // two new cities
    auto two = after;
    two.push_back({0x22A9, false, true, true, 3});
    CHECK(new_city_since(before, two, r) == kAmbiguous);
    // the new city of an existing nation (its political id was there before)
    std::vector<City> npc{{0x80002274, true, false, true, 1}, {0x22A7, false, true, true, 4}};
    auto grown = npc;
    grown.push_back({0x22A7, false, true, true, 5});
    CHECK(new_city_since(npc, grown, r) == kAmbiguous);
}

static void adoption() // BUG-9276: no free site, the partner takes over an NPC nation
{
    auto r = roster({{0, {1, 0x800021CB, 0}}, {2, {0x7C000002, 0x22A8, 2}}});
    // the host's, a player's, then NPC nations: one with two cities near, two one-city ones (the nearer wins)
    // (nation, home, own, dist)
    std::vector<Holding> c{{0x800021CB, true, true, 0},  {0x22A8, false, true, 10}, {0x21CC, false, true, 50},
                           {0x21CC, false, true, 60},     {0x21D0, false, true, 305}, {0x21CD, false, true, 575}};
    CHECK(adopt_choice(c, r) == 0x21D0);
    // a nation whose city is on its way to another nation (a capture under way) is not taken
    c[4].own = false;
    CHECK(adopt_choice(c, r) == 0x21CD);
    // the two-city nation when no one-city nation is left
    c[5].own = false;
    CHECK(adopt_choice(c, r) == 0x21CC);
    // only the players' nations left: none
    std::vector<Holding> team{{0x800021CB, true, true, 0}, {0x22A8, false, true, 10}, {0, false, true, 5}};
    CHECK(adopt_choice(team, r) == 0);
    CHECK(adopt_choice({}, r) == 0);
}

static void owners()
{
    CHECK(seat_owner(0, kNobody) == 0);   // the host's nation
    CHECK(seat_owner(1, 3) == 3);         // the seat's connected player
    CHECK(seat_owner(1, kNobody) == kNoOwner); // away: nobody commands it
    CHECK(seat_owner(-1, 3) == kNobody);  // no seat
    CHECK(wire_owner(kNoOwner, true) == kAwaySeat);
    CHECK(wire_owner(kNoOwner, false) == kNoOwner);
    CHECK(wire_owner(2, true) == 2);
    CHECK(team_owner(0) && team_owner(2) && team_owner(0xFD) && team_owner(kAwaySeat));
    CHECK(!team_owner(kNoOwner) && !team_owner(0xFF));
}

static void team()
{
    CHECK(team_tech(1, 10, 1) == 2);
    CHECK(team_tech(4, 10, 2) == 3);
    CHECK(team_tech(10, 10, 3) == 4);
    CHECK(team_tech(3, 10, 3) == 3); // never lower
    CHECK(team_tech(12, 10, 2) == 4);
    CHECK(team_progress(0.1f, 1, 10) > 0.199f && team_progress(0.1f, 1, 10) < 0.201f);
    CHECK(team_progress(0.9f, 5, 10) == 1.f);
    CHECK(team_progress(0.3f, 2, 0) == 0.3f);
    CHECK(!team_won(9, 10, 10));  // an NPC city is left
    CHECK(!team_won(5, 5, 10));   // sites left for NPC nations
    CHECK(team_won(10, 10, 10));
    CHECK(!team_won(0, 0, 10));
    CHECK(friendly_fire(true, true, 1, 2));
    CHECK(!friendly_fire(true, true, 1, 1)); // (its own)
    CHECK(!friendly_fire(true, false, 1, 2)); // an NPC attacks a player's nation
    CHECK(!friendly_fire(false, true, 1, 2)); // a player's nation attacks an NPC
}

int main()
{
    next_capitals();
    sites_and_new_cities();
    adoption();
    owners();
    team();
    if (g_fail)
        std::printf("civ_seats_tests: %d FAILED\n", g_fail);
    else
        std::printf("civ_seats_tests: ok\n");
    return g_fail ? 1 : 0;
}
