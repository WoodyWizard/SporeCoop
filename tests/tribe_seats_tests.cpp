// BUG-9135: the pure seat logic of Tribe with up to four founders (mods/coop/tribe_seats.h).
#include <cstdio>
#include <string>
#include <vector>

#include "tribe_seats.h"

using namespace tribe_seats;

static int g_failures;
static void check(bool ok, const char* name)
{
    if (!ok) {
        ++g_failures;
        printf("FAIL %s\n", name);
    }
}

// a packet tail as MSG_TRIBE_STATE ends: some bytes of earlier blocks, then the rescue trailer
static std::vector<uint8_t> tail_with(const Rescues& r, size_t seats, size_t before = 7)
{
    net::Writer w;
    for (size_t i = 0; i < before; ++i) w.u8((uint8_t)(0xA0 + i));
    write_rescues(w, r, seats);
    return w.data;
}

int main()
{
    // counts: two founders keep the native numbers (four NPC objectives = six villages, five = seven, six = eight)
    check(villages_with(2, 4) == 6 && villages_with(2, 5) == 7 && villages_with(2, 6) == 8, "two-founder counts");
    check(villages_with(3, 4) == 7 && villages_with(4, 6) == 10, "three and four founders");

    // the two-seat rescue trailer is byte for byte the old one: tag, seat 0, seat 1
    Rescues two{};
    two[0] = {0, 0};
    two[1] = {0x1234, 250};
    {
        net::Writer w;
        write_rescues(w, two, 2);
        const std::vector<uint8_t> want{0x54, 0x52, 0x52, 0x56, 0, 0, 0, 0, 0x34, 0x12, 250, 0};
        check(w.data == want, "two-seat trailer unchanged (TRRV, 12 bytes)");
        check(rescue_bytes(2) == kRescueBytes && rescue_bytes(1) == kRescueBytes, "two-seat size");
    }
    // a two-seat trailer reads back with and without the N-seat form
    for (bool multi : {false, true}) {
        auto t = tail_with(two, 2);
        Rescues out;
        size_t used = read_rescues(t.data(), t.size(), out, multi);
        check(used == kRescueBytes && out[1] == two[1] && out[0] == Rescues{}[0] && out[2] == Rescue{}, "two-seat read");
    }
    // three and four seats: the TRRS block, read from the end
    Rescues four{};
    four[0] = {7, 300};
    four[2] = {0x0102, 15};
    four[3] = {0xFFFE, 1};
    for (size_t seats : {3u, 4u}) {
        auto t = tail_with(four, seats);
        check(t.size() == 7 + rescue_bytes(seats) && rescue_bytes(seats) == seats * 4 + 5, "N-seat size");
        Rescues out;
        size_t used = read_rescues(t.data(), t.size(), out, true);
        bool same = used == rescue_bytes(seats);
        for (size_t i = 0; i < kMaxSeats; ++i) same &= out[i] == (i < seats ? four[i] : Rescue{});
        check(same, "N-seat round trip");
        // a guest with tribe.multi_founders off reads no rescue from it (and never gets one: 3+ seats need the switch)
        check(read_rescues(t.data(), t.size(), out, false) == 0 && out == Rescues{}, "N-seat ignored when off");
        // a cut block is not read
        check(read_rescues(t.data() + 7 + 1, t.size() - 7 - 1, out, true) == 0, "cut N-seat block");
    }
    // eight seats fit, nine do not exist
    {
        Rescues all{};
        for (size_t i = 0; i < kMaxSeats; ++i) all[i] = {(uint16_t)(i + 1), (uint16_t)(i * 10)};
        auto t = tail_with(all, 9, 0);
        Rescues out;
        check(t.size() == rescue_bytes(8) && read_rescues(t.data(), t.size(), out, true) == rescue_bytes(8) && out == all,
              "eight seats");
    }
    // no trailer at all, a wrong tag, an impossible count
    {
        std::vector<uint8_t> none(20, 0);
        Rescues out;
        check(read_rescues(none.data(), none.size(), out, true) == 0, "no trailer");
        auto t = tail_with(four, 3);
        t[t.size() - 5] = 2; // a count the 3+ form never writes
        check(read_rescues(t.data(), t.size(), out, true) == 0, "count 2 in the N-seat form");
        t[t.size() - 5] = 200;
        check(read_rescues(t.data(), t.size(), out, true) == 0, "count over the seats");
        check(read_rescues(t.data(), 3, out, true) == 0, "short tail");
    }
    // an old two-seat trailer whose last byte could look like a count: never taken for the N-seat form
    {
        Rescues odd{};
        odd[1] = {0x5354, 0x0300}; // high byte 3 at the end (an invalid tenths value, still parsed as before)
        auto t = tail_with(odd, 2, 20);
        Rescues out;
        check(read_rescues(t.data(), t.size(), out, true) == kRescueBytes && out[1] == odd[1], "old trailer with a count-like byte");
    }

    check(max_food_entries(false) == 2 && max_food_entries(true) == kMaxSeats, "food entries");

    // command seats: 0 = not given (older peers), seat + 1, anything else refused
    int seat = 5;
    check(seat_action(0) == 1 && seat_action(3) == 4 && seat_action(-1) == 0 && seat_action(8) == 0, "seat_action");
    check(action_seat(0, seat) && seat == -1, "not given");
    check(action_seat(1, seat) && seat == 0 && action_seat(4, seat) && seat == 3 && action_seat(8, seat) && seat == 7,
          "given");
    check(!action_seat(9, seat) && !action_seat(-1, seat) && !action_seat(0x7FFFFFFF, seat), "refused");

    // older saves: the BUG-9137 seat is taken over by the only unseated partner; an unknown seat holds founding
    {
        std::vector<PartnerSeat> s{{false, false, true}}; // seat 1: no identity, nobody, the host's creation
        check(claimable_seat(s, 1) == 0 && founding_held(s, 1), "wrong founder claimed by the only partner");
        check(claimable_seat(s, 2) == -1 && founding_held(s, 2), "two partners: nobody claims, founding waits");
        s = {{true, false, false}, {false, true, false}}; // bound offline seat, matched old seat
        check(claimable_seat(s, 1) == -1 && !founding_held(s, 1), "identities known: a new partner is founded");
        s = {{false, false, false}}; // an old seat whose partner is not here
        check(claimable_seat(s, 1) == -1 && founding_held(s, 1), "unknown old seat holds founding");
        s = {{false, false, true}, {false, false, true}};
        check(claimable_seat(s, 1) == -1 && founding_held(s, 1), "two wrong seats: ambiguous");
        s = {};
        check(claimable_seat(s, 3) == -1 && !founding_held(s, 3), "no partner seats yet");
    }

    // HUD lines
    {
        float two_food[2] = {12.5f, 3.f};
        check(food_line_host(two_food, 2) == "Village food   Host 12.50   Guest 3.00", "host line, two seats (as before)");
        float three[3] = {1.f, -1.f, 2.25f};
        check(food_line_host(three, 3) == "Village food   Host 1.00   Guests -, 2.25", "host line, three seats");
        float none[2] = {1.f, -1.f};
        check(food_line_host(none, 2).empty() && food_line_host(none, 1).empty(), "host line hidden");
        check(food_line_guest(4.f, 5.5f, true) == "Village food   Host 4.00   You 5.50", "guest line");
        check(food_line_guest(4.f, 5.5f, false) == "Village food   Host 4.00   Guest 5.50", "guest line, switch off");
        check(food_line_guest(-1.f, 5.5f, true).empty(), "guest line hidden");
    }

    printf("tribe seats: %d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
