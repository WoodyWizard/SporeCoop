// BUG-9268: the pure rules of every player's own village (mods/coop/tribe_seats.h) and the founders' roster
// (founders.h roster_valid). Built and run by `make test` (unit-cpp).
#include "founders.h"
#include "tribe_seats.h"

#include <cmath>
#include <cstdio>

static int g_fail;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                   \
            g_fail++;                                                                                                  \
        }                                                                                                              \
    } while (0)

using namespace tribe_seats;

static void owners()
{
    CHECK(village_owner(3) == 3);
    CHECK(village_owner(0xFE) == kNobody); // the seat's player is offline
    CHECK(village_owner(0xFF) == kNobody);
    // the host (0) and guest 2 share the host's village; guest 1 has its own
    std::vector<uint8_t> players{0, 1, 2}, villaged{1};
    auto shared = shared_players(players, villaged);
    CHECK((shared == std::vector<uint8_t>{0, 2}));
    uint32_t turn = 0;
    CHECK(shared_owner(0, villaged, shared, turn) == 0 && turn == 0); // kept
    CHECK(shared_owner(2, villaged, shared, turn) == 2 && turn == 0);
    CHECK(shared_owner(1, villaged, shared, turn) == 0 && turn == 1); // dealt to the sharers in turns
    CHECK(shared_owner(1, villaged, shared, turn) == 2 && turn == 2);
    // nobody shares (every guest villaged, the host gone is impossible, but the rule holds)
    std::vector<uint8_t> none;
    CHECK(shared_owner(1, villaged, none, turn) == kNobody);
    // nobody villaged: everybody shares, every owner kept
    CHECK(shared_players(players, {}) == players);
}

static void founding()
{
    FoundingIn in;
    in.seat = 1;
    in.species = 0x7C000001;
    in.host_species = 0x065CBE70;
    in.profile = true;
    in.world_age = 20;
    in.key_age = 12;
    CHECK(founding_wait(in) == Wait::Ready);
    FoundingIn x = in;
    x.seat = -1;
    CHECK(founding_wait(x) == Wait::Full);
    x = in;
    x.species = 0;
    CHECK(founding_wait(x) == Wait::HostSpecies);
    x.species = x.host_species;
    CHECK(founding_wait(x) == Wait::HostSpecies);
    x = in;
    x.village = true;
    CHECK(founding_wait(x) == Wait::HasVillage);
    x = in;
    x.other_seat = true;
    CHECK(founding_wait(x) == Wait::OtherSeat);
    x = in;
    x.profile = false;
    CHECK(founding_wait(x) == Wait::NoProfile);
    x = in;
    x.world_age = 5; // the world was entered moments ago
    CHECK(founding_wait(x) == Wait::Settling);
    x = in;
    x.key_age = 3; // the creature changed moments ago
    CHECK(founding_wait(x) == Wait::Settling);
}

static void counts()
{
    // the host's village, one partner village, two rivals
    CHECK(hidden_villages({0x10, 0x20, 0x30, 0x40}, {0x20}) == 1);
    // a partner faction listed twice counts once; one not in the list not at all
    CHECK(hidden_villages({0x10, 0x20, 0x20, 0x30}, {0x20, 0x99}) == 1);
    // never down to no village at all (the callers read count - 1)
    CHECK(hidden_villages({0x20}, {0x20}) == 0);
    CHECK(hidden_villages({0x10, 0x20, 0x30}, {0x20, 0x30}) == 2);
    CHECK(hidden_villages({}, {0x20}) == 0);
    CHECK(hidden_villages({0, 0x10}, {0}) == 0); // an unreadable entry (0) is no partner's
}

static void alliance()
{
    CHECK(ally_delta(0.f, 5.f) == 6.f);
    CHECK(ally_delta(-4.f, 5.f) == 10.f);
    CHECK(ally_delta(5.5f, 5.f) == 0.f); // allies already
    CHECK(ally_delta(8.f, 5.f) == 0.f);
    CHECK(ally_delta(NAN, 5.f) == 0.f);
    CHECK(ally_delta(1.f, 0.f) == 0.f); // no threshold known
}

static void food()
{
    CHECK(food_line({12.f, 20.f}, {"Host", "Bob"}) == "Village food   Host 12   Bob 20");
    CHECK(food_line({12.f, -1.f}, {"Host", "Bob"}).empty()); // no partner village
    CHECK(food_line({12.f, -1.f, 7.f}, {"Host", "Bob", "Ann"}) == "Village food   Host 12   Ann 7");
}

static void roster()
{
    founders::Roster r;
    CHECK(founders::roster_valid(r) && r.seats() == 0);
    r.entries[0] = {0x065CBE70, 0x80002274, 1};
    r.entries[2] = {0x7CA81684, 0x2294, 2}; // seat 1 plays the host's species: no village, its entry empty
    CHECK(founders::roster_valid(r) && r.seats() == 3);
    CHECK(r.seat_of_faction(0x2294) == 2 && r.seat_of_faction(0x1234) == -1 && r.seat_of_faction(0) == -1);
    founders::Roster bad = r;
    bad.entries[1] = {0x7CA81684, 0x5555, 3}; // the same species as seat 2
    CHECK(!founders::roster_valid(bad));
    bad = r;
    bad.entries[1] = {0x7C000002, 0x2294, 3}; // the same faction as seat 2
    CHECK(!founders::roster_valid(bad));
    bad = r;
    bad.entries[1] = {0x7C000002, 0x5555, 2}; // the same player as seat 2
    CHECK(!founders::roster_valid(bad));
    bad = r;
    bad.entries[0] = {};
    CHECK(!founders::roster_valid(bad)); // a partner's village without the host's
    bad = r;
    bad.entries[1] = {0x7C000002, 0, 0}; // half an entry
    CHECK(!founders::roster_valid(bad));
}

int main()
{
    owners();
    founding();
    counts();
    alliance();
    food();
    roster();
    if (g_fail) {
        std::printf("tribe_seats_tests: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("tribe_seats_tests: ok\n");
    return 0;
}
