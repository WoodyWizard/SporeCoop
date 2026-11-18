// BUG-9135: the Civilization founder seat rules of up to four players (mods/coop/civ_seats.h)
#include <cstdio>
#include <vector>

#include "civ_seats.h"

static int failures;
static void check(bool ok, const char* name)
{
    if (!ok) {
        ++failures;
        printf("FAIL %s\n", name);
    }
}

using founders::Roster;
using namespace civ_seats;

static Roster roster(std::vector<founders::Founder> seats)
{
    Roster r;
    for (size_t i = 0; i < seats.size(); ++i)
        r.entries[i] = seats[i];
    return r;
}

int main()
{
    // a two-founder campaign as before BUG-9135 (CIV1: no identities)
    const Roster two{0x100, 0x200, 0x21CA, 0x21CB};
    // four founders, identities known for seats 1 and 3
    const Roster four = roster({{0x100, 0x21CA, 0xA0}, {0x200, 0x21CB, 0xA1}, {0x300, 0x21CC, 0}, {0x400, 0x21CD, 0xA3}});
    founders::Outfits outfits;
    outfits.species[2] = {0x2F0, 0x300}; // seat 2's tribal founder changed its look once

    check(play_seats(two) == 2 && play_seats(four) == 4, "play seats");
    Roster five = four;
    five.entries[4] = {0x500, 0x21CE, 0xA4};
    check(play_seats(five) == founders::kPlaySeats, "play seats capped at kPlaySeats");

    // seat_species: the nation's species or any look of the seat
    check(seat_species(four, outfits, 2, 0x300) && seat_species(four, outfits, 2, 0x2F0), "seat species, history");
    check(!seat_species(four, outfits, 1, 0x2F0) && !seat_species(four, outfits, 2, 0), "seat species, other seat / none");
    check(!seat_species(four, outfits, 9, 0x300) && !seat_species(four, outfits, -1, 0x300), "seat species, bad seat");

    // the two-founder rule: one partner in the session, a two-seat roster without identity
    check(sole_partner_seat(two, 1, 1), "sole partner commands seat 1");
    check(!sole_partner_seat(two, 1, 2), "two partners: no sole partner");
    check(!sole_partner_seat(four, 1, 1), "four seats: no sole partner rule");
    Roster bound = two;
    bound.entries[1].player = 0xB1;
    check(!sole_partner_seat(bound, 1, 1), "an identity on seat 1: no sole partner rule");

    // own_seat (guest)
    check(own_seat(two, {}, 0, 0, 1) == 1, "two founders, one partner: seat 1 as before");
    check(own_seat(two, {}, 0, 0x999, 2) == -1, "two founders, a third player with another species: no seat");
    check(own_seat(two, {}, 0, 0x200, 2) == 1, "two founders, three players: the species tells");
    check(own_seat(four, outfits, 0xA3, 0x999, 3) == 3, "identity first");
    check(own_seat(four, outfits, 0xA3, 0x300, 3) == 3, "identity before species");
    check(own_seat(four, outfits, 0x77, 0x2F0, 3) == 2, "species of a seat without identity (earlier look)");
    check(own_seat(four, outfits, 0x77, 0x200, 3) == -1, "a seat with another identity is not taken by species");
    check(own_seat(bound, {}, 0x77, 0x999, 1) == -1, "a bound partner seat is not taken by the sole partner rule");
    check(own_seat(four, outfits, 0xA0, 0x100, 3) == -1, "the host's seat is never a guest's");
    check(own_seat(five, outfits, 0xA4, 0x500, 4) == -1, "seats past kPlaySeats are not played");

    // owner_of_seat
    check(owner_of_seat(0, true, 0, -1, 0, false) == 0 && owner_of_seat(0, false, kNobody, 2, 2, false) == 0, "seat 0 is the host's");
    check(owner_of_seat(2, true, 3, -1, 0, false) == 3, "host: the connected player of the seat");
    check(owner_of_seat(2, true, kNobody, -1, 0, false) == kNoOwner, "host: an away seat has no owner");
    check(owner_of_seat(2, true, kNobody, -1, 0, true) == 0, "host: an away seat is the host's with offline control");
    check(owner_of_seat(2, false, kNobody, 2, 5, false) == 5, "guest: own seat");
    check(owner_of_seat(1, false, kNobody, 2, 5, false) == kOtherSeat, "guest: another partner's seat");
    check(owner_of_seat(1, false, kNobody, -1, 5, true) == kOtherSeat, "guest without a seat: never its own");
    check(owner_of_seat(-1, true, 1, -1, 0, true) == kNobody, "no seat");
    check(kOtherSeat != kNoOwner && kOtherSeat != kNobody && kOtherSeat >= 8, "markers are no player ids");

    // next_capital_seat: in seat order, from the Tribe roster's seats, capped
    check(next_capital_seat(0, 2) == 1, "two founders: the partner's capital first");
    check(next_capital_seat(2, 2) == -1, "two founders: nothing more");
    check(next_capital_seat(0, 0) == -1 && next_capital_seat(0, 1) == -1, "no tribal partner: nothing");
    check(next_capital_seat(2, 4) == 2 && next_capital_seat(3, 4) == 3 && next_capital_seat(4, 4) == -1, "four: one at a time");
    check(next_capital_seat(4, 6) == -1 && next_capital_seat(3, 8) == 3, "capped at kPlaySeats");

    // only_founder_cities and new_capital
    const Roster none;
    std::vector<City> graduation{{0x21CA, true, false}};
    check(only_founder_cities(graduation, none), "the native graduation's single capital");
    check(new_capital(graduation, none) == 0, "nothing new yet");
    std::vector<City> appeared{{0x21CA, true, false}, {0x21CB, false, true}};
    check(new_capital(appeared, none) == 0x21CB, "the new blank nation");
    check(!only_founder_cities(appeared, none), "an unseated nation blocks the next founding");
    check(only_founder_cities(appeared, two), "home and seated founders only");
    std::vector<City> keyed{{0x21CA, true, false}, {0x21CB, false, false}};
    check(new_capital(keyed, none) == kAmbiguous, "a nation with a species is not taken");
    std::vector<City> both{{0x21CA, true, false}, {0x21CB, false, true}, {0x21CC, false, true}};
    check(new_capital(both, none) == kAmbiguous, "two new nations: ambiguous");
    check(new_capital(both, two) == 0x21CC, "the seated nation is skipped");
    std::vector<City> npc{{0x21CA, true, false}, {0x21CB, false, false}, {0x21D0, false, false}};
    check(!only_founder_cities(npc, two), "an NPC nation: no founding");
    check(!only_founder_cities({}, two) && !only_founder_cities({{0x21CB, false, false}}, two), "no home city: no founding");
    std::vector<City> orphan{{0x21CA, true, false}, {0x21CB, false, true}, {0x21D1, false, false, false}};
    check(new_capital(orphan, none) == 0x21CB, "a city without a nation is no new capital");
    check(!only_founder_cities(orphan, two), "a city without a nation blocks a founding");

    // identities_to_bind
    Roster legacy4 = four;
    legacy4.entries[1].player = 0;
    std::vector<Partner> partners{{0xB1, 0x200}, {0xB2, 0x2F0}, {0xA3, 0x400}, {0, 0x300}};
    auto bind = identities_to_bind(legacy4, outfits, partners);
    check(bind.size() == 2 && bind[0] == std::make_pair(1, size_t(0)) && bind[1] == std::make_pair(2, size_t(1)),
          "seats 1 and 2 learn their partners; a seated identity and a partner without one are skipped");
    std::vector<Partner> twins{{0xB1, 0x200}, {0xB5, 0x200}};
    check(identities_to_bind(legacy4, outfits, twins).empty(), "two partners of the same species: nothing bound");
    std::vector<Partner> stale{{0xB1, 0x200}, {0xB1, 0x999}};
    check(identities_to_bind(legacy4, outfits, stale).empty(), "the same identity twice: nothing bound");
    check(identities_to_bind(four, outfits, {{0xA1, 0x300}}).empty(), "an identity already seated is not moved");

    // ---- roadmap phase 4: the late capital ----
    // founders::nations_descend (founders::can_record, Stage::Civilization): tribal seats play their founder's
    // looks; a seat past the Tribe roster is a late joiner's, with an identity and a species of its own
    const Roster tribe3 = roster({{0x100, 0x31CA, 0xA0}, {0x200, 0x31CB, 0xA1}, {0x300, 0x31CC, 0}});
    founders::Outfits looks3;
    looks3.species[0] = {0x100};
    looks3.species[1] = {0x200, 0x210};
    looks3.species[2] = {0x2F0, 0x300};
    Roster nations4 = roster({{0x100, 0x21CA, 0xA0}, {0x210, 0x21CB, 0xA1}, {0x300, 0x21CC, 0}, {0x900, 0x21D7, 0xC3}});
    check(founders::nations_descend(tribe3, looks3, nations4), "a late seat past the Tribe roster may be recorded");
    Roster nations3 = nations4;
    nations3.entries[3] = {};
    check(founders::nations_descend(tribe3, looks3, nations3), "tribal seats with their founders' looks, as before");
    Roster stranger3 = nations3;
    stranger3.entries[2].species = 0x901;
    check(!founders::nations_descend(tribe3, looks3, stranger3), "a tribal seat with a stranger's species: refused, as before");
    Roster anonymous = nations4;
    anonymous.entries[3].player = 0;
    check(!founders::nations_descend(tribe3, looks3, anonymous), "a late seat without an identity: refused");
    Roster copycat = nations4;
    copycat.entries[3].species = 0x2F0;
    check(!founders::nations_descend(tribe3, looks3, copycat), "a late seat playing a tribal founder's look: refused");
    check(!founders::nations_descend(two, outfits, Roster{0x100, 0x200, 1, 2}), "two tribal seats without looks: refused");

    // late_status / late_capital
    const uint32_t host = 0x100;
    Joiner late{3, 0xC3, 0x900, true, true, -1};
    std::vector<LateWhy> why;
    auto plan = late_capital(nations3, tribe3, looks3, host, {late}, &why);
    check(plan.seat == 3 && plan.joiner == 0 && why.size() == 1 && why[0] == LateWhy::Capital,
          "a late joiner with a creature of its own gets seat 3 after the three tribal seats");
    Joiner founder = late;
    founder.nation_seat = 2;
    check(late_status(nations3, tribe3, looks3, host, founder) == LateWhy::HasNation, "a founder has a nation");
    Joiner unseated = late;
    unseated.party_seat = -1;
    check(late_status(nations3, tribe3, looks3, host, unseated) == LateWhy::NoPartySeat, "no party seat: wait");
    Joiner hosts = late;
    hosts.own_creature = false;
    check(late_status(nations3, tribe3, looks3, host, hosts) == LateWhy::NoOwnCreature, "plays the host's creation: no capital");
    Joiner same = late;
    same.species = host;
    check(late_status(nations3, tribe3, looks3, host, same) == LateWhy::NoOwnCreature, "plays the host's species: no capital");
    Joiner nothing = late;
    nothing.species = 0;
    check(late_status(nations3, tribe3, looks3, host, nothing) == LateWhy::NoOwnCreature, "no creature at all: no capital");
    Joiner loading = late;
    loading.species_ready = false;
    check(late_status(nations3, tribe3, looks3, host, loading) == LateWhy::SpeciesLoading, "creature not loaded: wait");
    Joiner look = late;
    look.species = 0x200; // tribal seat 1's first look
    check(late_status(nations3, tribe3, looks3, host, look) == LateWhy::SharedSpecies, "a founder's species: no second nation");
    check(late_status(nations4, tribe3, looks3, host, Joiner{2, 0xC4, 0x901, true, true, -1}) == LateWhy::SeatsFull,
          "four nations: every play seat taken");
    // the Tribe roster's seats come first: seat 2 (no nation yet) is founded before any late joiner
    plan = late_capital(two, tribe3, looks3, host, {late}, &why);
    check(plan.seat == 2 && plan.joiner == -1 && why[0] == LateWhy::TribalFirst, "the tribal seat 2 first, then the late joiner");
    Joiner tribal2{2, 0xD2, 0x300, true, true, -1};
    check(late_status(two, tribe3, looks3, host, tribal2) == LateWhy::TribalSeat, "seat 2's tribal founder (look, no identity)");
    Joiner tribal1{1, 0xA1, 0x999, true, true, -1};
    check(late_status(Roster{}, tribe3, looks3, host, tribal1) == LateWhy::TribalSeat, "seat 1's tribal founder (identity)");
    // no Tribe roster (the host played Tribe alone): the first late joiner takes seat 1
    plan = late_capital(Roster{}, Roster{}, {}, host, {late}, &why);
    check(plan.seat == 1 && plan.joiner == 0, "no rosters: seat 1 for the late joiner");
    // two late joiners: the lower party seat first, one at a time
    Joiner second{2, 0xC2, 0x902, true, true, -1};
    plan = late_capital(nations3, tribe3, looks3, host, {late, second}, &why);
    check(plan.seat == 3 && plan.joiner == 1 && why[0] == LateWhy::Capital && why[1] == LateWhy::Capital,
          "two late joiners: party seat 2 before party seat 3");
    plan = late_capital(nations3, tribe3, looks3, host, {hosts, loading}, &why);
    check(plan.seat == -1 && plan.joiner == -1, "nobody eligible: nothing founded");

    // site_state (cCivExpansion +0xAC all, +0xB0 left)
    check(site_state(10, 6, 4) == SiteState::Ready, "4 founded, 4 live: ready");
    check(site_state(10, 5, 4) == SiteState::Pending, "5 founded, 4 live: one still on its way");
    check(site_state(10, 0, 10) == SiteState::NoSite, "every site founded: none left");
    check(site_state(0, 0, 1) == SiteState::Unknown && site_state(10, 11, 1) == SiteState::Unknown &&
              site_state(100, 1, 1) == SiteState::Unknown,
          "fields that look wrong");

    // new_city_since: the one new city, a fresh nation
    std::vector<City> before{{0x21CA, true, false, true, 1}, {0x21CB, false, false, true, 2}, {0x21CC, false, true, true, 3}};
    std::vector<City> after = before;
    check(new_city_since(before, after, two) == 0, "nothing new yet");
    after.push_back({0x21D9, false, true, true, 9});
    check(new_city_since(before, after, two) == 0x21D9, "the new blank city, although an older NPC city is blank too");
    auto twice_new = after;
    twice_new.push_back({0x21DA, false, true, true, 10});
    check(new_city_since(before, twice_new, two) == kAmbiguous, "two new cities: ambiguous");
    auto keyed_new = before;
    keyed_new.push_back({0x21D9, false, false, true, 9});
    check(new_city_since(before, keyed_new, two) == kAmbiguous, "a new city with a species: not taken");
    auto joined = before;
    joined.push_back({0x21CC, false, true, true, 9});
    check(new_city_since(before, joined, two) == kAmbiguous, "a new city of an older nation: not a fresh nation");
    check(new_city_since({}, {{0x21CB, false, true, true, 9}}, two) == kAmbiguous, "a seated nation's city: not taken");
    check(new_city_since(before, {{0x21CA, true, false, true, 1}, {0x21D9, false, true, true, 9}}, two) == 0x21D9,
          "a city gone meanwhile does not matter");

    // catch_up_buildings: the host capital's mix, the host's slots where free, types in turn
    const uint32_t H_ = 0x18EB106, F_ = 0x18EA2CC, E_ = 0x1A56ABA;
    std::vector<Building> host_city{{H_, 0}, {H_, 1}, {F_, 2}, {E_, 3}, {H_, 4}, {F_, 5}};
    std::vector<Building> fresh{{H_, 1}};
    auto add = catch_up_buildings(host_city, fresh, 14);
    check(add.size() == 5 && add[0] == Building{H_, 0} && add[1] == Building{F_, 2} && add[2] == Building{E_, 3} &&
              add[3] == Building{H_, 4} && add[4] == Building{F_, 5},
          "two more houses (free host slots first), two factories, one entertainment");
    add = catch_up_buildings(host_city, fresh, 3);
    check(add.size() == 3 && add[0].noun == H_ && add[1].noun == F_ && add[2].noun == E_, "few free slots: one of each type first");
    check(catch_up_buildings(host_city, host_city, 14).empty(), "a capital that has the host's buildings: nothing");
    check(catch_up_buildings({}, fresh, 14).empty() && catch_up_buildings(host_city, fresh, 0).empty(), "nothing to copy / no slot");
    std::vector<Building> crowded{{H_, 0}, {H_, 2}, {H_, 3}, {H_, 7}};
    add = catch_up_buildings(host_city, crowded, 14);
    check(add.size() == 3 && add[0] == Building{F_, 5} && add[1] == Building{E_, 3} && add[2] == Building{F_, 2},
          "more houses than the host: none added; free host slots before busy ones");
    check(catch_up_turrets(4, 1) == 3 && catch_up_turrets(1, 4) == 0, "turrets: as many as the host's capital");

    printf("civ seats: %d failures\n", failures);
    return failures ? 1 : 0;
}
