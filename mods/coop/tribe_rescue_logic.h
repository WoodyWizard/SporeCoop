// BUG-9278 (Lineages port, PR #196's BUG-9118 / BUG-9135 / BUG-9155, the "founder rescue" and the "party wipe" of its
// tribe.cpp): the pure rules of tribe_rescue.cpp. A player's last living villager is not killed by a lethal blow while a
// partner can help: it goes down where it stands for a while (60 s; 30 s with tribe.down_60 off), a living villager of
// another player's village that stays within kReachM of it for kHoldS revives it; nobody came (or its player gave up):
// the whole party pays (the newest win of the campaign is undone, or 10 % of every village's food) and the villager gets
// up at its own village. Portable (no Windows, no game): tests/tribe_rescue_tests.cpp. docs/re/tribe.md#tribe-founder-rescue.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "net/buffer.h"

namespace tribe_rescue {

constexpr size_t kMaxSeats = 4;     // founders::kMaxSeats
constexpr double kReachM = 15.0;    // a helper's distance from the downed villager (Lineages: 15 m)
constexpr double kHoldS = 1.5;      // how long a helper stays that close (Lineages: 1.5 s)
constexpr double kGraceS = 2.0;     // after the end of a window: blows on the villager are ignored this long
constexpr float kUpHealth = 0.4f;   // the villager gets up with 40 % of its maximum health
constexpr double kFoodShare = 0.10; // the food a village loses when the party has no win to lose
constexpr float kRivalRelation = -5.f; // an ally the wipe takes back becomes a rival (Lineages kRivalRelation)

// the time a downed villager waits for its partner (Lineages BUG-9149 core.down_60: a minute; off: BUG-9118's 30 s)
inline double down_seconds(bool minute) { return minute ? 60.0 : 30.0; }

// ---- a lethal blow on the host (h_CitizenTD) ----
struct DownIn {
    bool fix = false;            // tribe.founder_rescue
    bool seat = false;           // the victim's village is a player's (the host's, seat 0, or a partner's village)
    bool player_here = false;    // that seat's player is in the session (an offline partner's village is the AI's)
    bool partner_village = false; // at least one other player's village exists (a single shared village: none)
    bool intact = false;         // its village is intact (a hut with health, not conquered)
    float damage = 0, health = 0; // the blow and the victim's health before it
    bool dead = false;           // the victim is dead already (+0xB5E)
    bool others_living = false;  // another member of its village is alive
    bool helper_alive = false;   // a village of another present player has a living member
    bool party_wipe = false;     // tribe.party_wipe (with it the villager goes down even with no helper alive: a partner
                                 // may still come; else the window ends in the wipe and it gets up at home)
};
inline bool lethal(float damage, float health) { return std::isfinite(damage) && damage > 0 && health > 0 && damage >= health; }
inline bool should_down(const DownIn& in)
{
    if (!in.fix || !in.seat || !in.player_here || !in.partner_village || !in.intact || in.dead || in.others_living)
        return false;
    if (!lethal(in.damage, in.health))
        return false;
    return in.helper_alive || in.party_wipe;
}

// ---- one window (host, every tick) ----
struct Window {
    uint16_t member = 0;    // the downed villager (the host's member id)
    uint32_t faction = 0;   // its village
    double until = 0;       // the window's end (0: no window)
    double near_since = -1; // a helper is near since (-1: none)
    double grace_until = 0; // blows ignored until (after the end)
    bool gave_up = false;   // its player gave up (hold G, a guest's CMD_GIVE_UP, coop.tribe rescue giveup)
    bool open() const { return until > 0; }
};
enum class Step : uint8_t { Waiting, Revived, TimedOut };
// a helper near now or not; returns what happened this tick (the caller ends the window on Revived / TimedOut)
inline Step step(Window& w, bool helper_near, double now)
{
    if (!w.open())
        return Step::Waiting;
    if (w.gave_up)
        w.until = std::min(w.until, now);
    if (!helper_near)
        w.near_since = -1;
    else if (w.near_since < 0)
        w.near_since = now;
    if (!w.gave_up && helper_near && w.near_since + kHoldS <= std::min(now, w.until))
        return Step::Revived;
    return now < w.until ? Step::Waiting : Step::TimedOut;
}
inline void close(Window& w, double now)
{
    w.until = 0;
    w.near_since = -1;
    w.gave_up = false;
    w.grace_until = now + kGraceS;
}
// how far the revive is (0..1), for the host's own view
inline float revive_progress(const Window& w, double now)
{
    if (!w.open() || w.near_since < 0)
        return 0.f;
    return (float)std::clamp((now - w.near_since) / kHoldS, 0.0, 1.0);
}

// ---- the party wipe ----
// The campaign's wins are the stage's goal history (TribeUI +0x40 / +0x44: records of 16 bytes {vt, kind 1 conquered / 2
// allied, cTribe*, 0}) and cPlayer +0x10F0 (the goal bar). With a win: the newest record goes, the bar drops by one, the
// top totem piece goes from every player village whose totem has exactly that many pieces, an ally becomes a rival.
// Without one: every present player village loses 10 % of its food (rounded up).
enum class Wipe : uint8_t { LoseWin, LoseFood };
inline Wipe wipe_kind(size_t wins) { return wins ? Wipe::LoseWin : Wipe::LoseFood; }
inline double food_cost(double food) { return std::isfinite(food) && food > 0 ? std::min(food, std::ceil(food * kFoodShare)) : 0; }
inline float bar_after(float bar) { return std::isfinite(bar) ? std::max(0.f, bar - 1.f) : bar; }
// a totem's piece vectors (bytes of the kinds, u32 each, and of the colours, 3 x float each) with `wins` pieces: the top
// piece may go
inline bool totem_pop_ok(size_t kinds_bytes, size_t colours_bytes, size_t wins)
{
    return wins > 0 && kinds_bytes % 4 == 0 && kinds_bytes / 4 == wins && colours_bytes == kinds_bytes * 3;
}
// the relation change that makes an ally a rival (0: it is not above the rival's relation)
inline float rival_delta(float now)
{
    return std::isfinite(now) && now > kRivalRelation ? kRivalRelation - now : 0.f;
}

// ---- the give-up of a guest's own villager (MSG_TRIBE_CMD CMD_GIVE_UP; the action word: the seat + 1, 0 = not given) ----
inline uint32_t seat_action(int seat) { return seat >= 0 ? (uint32_t)seat + 1 : 0; }
// the sender's seat may give up: its own seat's window (a claimed seat must be the sender's)
inline bool give_up_ok(int sender_seat, uint32_t action)
{
    if (sender_seat < 0 || sender_seat >= (int)kMaxSeats)
        return false;
    return action == 0 || action == seat_action(sender_seat);
}

// ---- the guests' view (MSG_TRIBE_ANIM / MSG_NPC_ANIM kind K_RESCUE = 22) ----
// u8 n, n x {u8 seat, u8 player (0xFE: nobody), u16 member, u16 tenths left (<= kMaxTenths), u8 revive % (0..100)}.
// The host sends it every second while a window is open and once with n = 0 when the last one closed.
constexpr uint16_t kMaxTenths = 600; // a minute (BUG-9149)
struct Line {
    uint8_t seat = 0, player = 0xFE;
    uint16_t member = 0, tenths = 0;
    uint8_t revive = 0;
    bool operator==(const Line&) const = default;
};
inline uint16_t tenths_left(double until, double now)
{
    const double t = std::ceil((until - now) * 10);
    return (uint16_t)std::clamp(t, 0.0, (double)kMaxTenths);
}
inline void write_lines(net::Writer& w, const std::vector<Line>& lines)
{
    const size_t n = std::min(lines.size(), kMaxSeats);
    w.u8((uint8_t)n);
    for (size_t i = 0; i < n; ++i) {
        w.u8(lines[i].seat);
        w.u8(lines[i].player);
        w.u16(lines[i].member);
        w.u16(lines[i].tenths);
        w.u8(lines[i].revive);
    }
}
// false: malformed (the guest keeps what it had)
inline bool read_lines(net::Reader& r, std::vector<Line>& out)
{
    out.clear();
    const uint8_t n = r.u8();
    if (!r.ok() || n > kMaxSeats)
        return false;
    for (uint8_t i = 0; i < n; ++i) {
        Line l;
        l.seat = r.u8();
        l.player = r.u8();
        l.member = r.u16();
        l.tenths = r.u16();
        l.revive = r.u8();
        if (!r.ok() || l.seat >= kMaxSeats || l.tenths > kMaxTenths || l.revive > 100 || !l.member)
            return false;
        out.push_back(l);
    }
    return true;
}

} // namespace tribe_rescue
