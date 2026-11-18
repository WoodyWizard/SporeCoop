// BUG-9135 (roadmap phase 3): Tribe with up to four founders (the host and up to three partners, each with a village
// of their own). The pure parts of tribe.cpp's seat logic: the campaign counts that include the founders, the wire
// forms of MSG_TRIBE_STATE's per-seat rescue trailer, the seat carried by CMD_REBUILD / CMD_GIVE_UP, the founding hold
// for seats of older saves and the HUD's food line. Portable (no Windows, no game): tests/tribe_seats_tests.cpp.
//
// Seats are the founder roster's (founders::Roster, docs/re/tribe.md#tribe-multi-founders): 0 the host's village (the
// native player tribe), 1.. the partners' villages in the order they were founded. Two seats keep every wire form of
// the two-founder co-op byte for byte (an older partner reads them); the N-seat forms are only written for 3+ seats.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "net/buffer.h"

namespace tribe_seats {

constexpr size_t kMaxSeats = 8; // founders::kMaxSeats (founders.h is not portable)

// ---- the campaign's village counts (h_NounData, missing_last_objective, the six-rival trial) ----
// The native Tribal goals expect the player village plus its NPC rivals; the partners' villages are counted next to
// them (two founders: 4 NPC -> 6 villages, 5 -> 7, 6 -> 8).
inline size_t villages_with(size_t founder_seats, size_t npc) { return founder_seats + npc; }

// ---- MSG_TRIBE_STATE rescue trailer (BUG-9118 rescues, by founder seat) ----
// The blocks after the clock trailer are read from the end of the packet backwards; the rescue block is the last.
// Two seats (as before, TRRV): u32 tag, 2 x {u16 member, u16 tenths}.
constexpr uint32_t kRescueTag = 0x56525254; // TRRV
constexpr size_t kRescueBytes = 12;
// 3+ seats (TRRS): n x {u16 member, u16 tenths}, u8 n, u32 tag. The count and the tag come last so the reader finds
// the variable block from the end. A guest of the two-founder format does not know the tag (it is never written to a
// session of two founders).
constexpr uint32_t kRescueSeatsTag = 0x53525254; // TRRS
constexpr size_t kMaxRescueTenths = 300;         // BUG-9118: up to 30 s

struct Rescue {
    uint16_t member = 0; // the downed founder's member id, 0 = none
    uint16_t tenths = 0; // time left, tenths of a second
    bool operator==(const Rescue&) const = default;
};
using Rescues = std::array<Rescue, kMaxSeats>;

inline size_t rescue_bytes(size_t seats) { return seats <= 2 ? kRescueBytes : std::min(seats, kMaxSeats) * 4 + 5; }

inline void write_rescues(net::Writer& w, const Rescues& r, size_t seats)
{
    if (seats <= 2) {
        w.u32(kRescueTag);
        for (size_t i = 0; i < 2; ++i) { w.u16(r[i].member); w.u16(r[i].tenths); }
        return;
    }
    seats = std::min(seats, kMaxSeats);
    for (size_t i = 0; i < seats; ++i) { w.u16(r[i].member); w.u16(r[i].tenths); }
    w.u8((uint8_t)seats);
    w.u32(kRescueSeatsTag);
}

// the rescue trailer at the end of [p, p + n) (the bytes after the clock trailer): returns the bytes it takes (0: none
// there) and the rescues by seat in `out`. `seats_form`: the 3+ seat form is accepted too (tribe.multi_founders).
inline size_t read_rescues(const uint8_t* p, size_t n, Rescues& out, bool seats_form)
{
    out = {};
    if (seats_form && n >= 5 + 3 * 4) {
        uint32_t tag;
        memcpy(&tag, p + n - 4, 4);
        const size_t count = p[n - 5];
        if (tag == kRescueSeatsTag && count >= 3 && count <= kMaxSeats && n >= count * 4 + 5) {
            net::Reader r(p + n - 5 - count * 4, count * 4);
            for (size_t i = 0; i < count; ++i) { out[i].member = r.u16(); out[i].tenths = r.u16(); }
            return count * 4 + 5;
        }
    }
    if (n >= kRescueBytes) {
        net::Reader r(p + n - kRescueBytes, kRescueBytes);
        if (r.u32() == kRescueTag) {
            for (size_t i = 0; i < 2; ++i) { out[i].member = r.u16(); out[i].tenths = r.u16(); }
            return kRescueBytes;
        }
    }
    return 0;
}

// food entries a guest accepts in one MSG_TRIBE_STATE (the founder villages' stores): two before BUG-9135
inline size_t max_food_entries(bool multi) { return multi ? kMaxSeats : 2; }

// ---- the seat of CMD_REBUILD / CMD_GIVE_UP (in the command's action word, which older hosts ignore for both) ----
// 0: not given (as before: two founders, "the partner"); seat + 1 otherwise.
inline uint32_t seat_action(int seat) { return seat >= 0 && seat < (int)kMaxSeats ? (uint32_t)seat + 1 : 0; }
// false: not a seat action; seat -1: not given
inline bool action_seat(int action, int& seat)
{
    seat = -1;
    if (action == 0) return true;
    if (action < 1 || action > (int)kMaxSeats) return false;
    seat = action - 1;
    return true;
}

// ---- partner seats of an older save (no player identity yet) ----
struct PartnerSeat {
    bool identity = false;      // the seat knows its player's identity (Founder::player)
    bool matched = false;       // a connected player commands it now (identity or the creature match)
    bool wrong_founder = false; // BUG-9137: founded with one of the host's creations, no outfit since
};
// The seat (index into `seats`, the partner seats 1..) the only unseated partner takes over: an unmatched seat
// without identity that was founded with one of the host's creations (BUG-9137: its partner's own creature came after
// the founding), when it is the only such seat and one partner is unseated. -1: none.
inline int claimable_seat(const std::vector<PartnerSeat>& seats, size_t unseated)
{
    int found = -1;
    for (size_t i = 0; i < seats.size(); ++i) {
        const auto& s = seats[i];
        if (s.identity || s.matched) continue;
        if (!s.wrong_founder) return -1; // an unknown partner's seat: nobody may take it over automatically
        if (found >= 0) return -1;
        found = (int)i;
    }
    return found >= 0 && unseated == 1 ? found : -1;
}
// Automatic founding waits while a seat without identity has no connected player: its partner may be here with
// another creature (a new village would be a second one for them). The host can bind it (coop.tribe bind_founder).
inline bool founding_held(const std::vector<PartnerSeat>& seats, size_t unseated)
{
    if (claimable_seat(seats, unseated) >= 0) return true; // the claim comes first
    for (const auto& s : seats)
        if (!s.identity && !s.matched) return true;
    return false;
}

// ---- the HUD's food line ----
// host: every founder village's food by seat (negative: unknown). Two seats: as before.
inline std::string food_line_host(const float* food, size_t seats)
{
    if (seats < 2 || food[0] < 0) return {};
    char line[200];
    if (seats == 2) {
        if (food[1] < 0) return {};
        snprintf(line, sizeof line, "Village food   Host %.2f   Guest %.2f", food[0], food[1]);
        return line;
    }
    std::string out;
    snprintf(line, sizeof line, "Village food   Host %.2f   Guests", food[0]);
    out = line;
    bool any = false;
    for (size_t i = 1; i < seats && i < kMaxSeats; ++i) {
        if (food[i] < 0) snprintf(line, sizeof line, "%s -", any ? "," : "");
        else snprintf(line, sizeof line, "%s %.2f", any ? "," : "", food[i]);
        out += line;
        any = true;
    }
    return out;
}
// guest: the host's village and its own (multi: "You", before: "Guest")
inline std::string food_line_guest(float home, float own, bool multi)
{
    if (home < 0 || own < 0) return {};
    char line[100];
    snprintf(line, sizeof line, multi ? "Village food   Host %.2f   You %.2f" : "Village food   Host %.2f   Guest %.2f", home, own);
    return line;
}

} // namespace tribe_seats
