// BUG-9283 (Lineages port, PR #196's creature_rescue.cpp: its BUG-9118 / BUG-9146 / BUG-9149 / BUG-9226): the pure rules
// of creature_rescue.cpp. A lethal hit on a player's own creature in the Creature stage while a partner is in the same
// world does not kill it: it goes down where it stands for a while (60 s; 30 s with creature.down_60 off). A partner who
// stands within kReachM of it for kHoldS revives it (40 % health); nobody came, or its player gave up (hold G): it dies
// the game's way and respawns at its own nest, and the pack loses 10 % of its DNA. When every player is down at once
// nobody can revive anybody: after kWipeAfterS every window runs out together (the party wipe). Portable (no Windows, no
// game): tests/creature_rescue_tests.cpp. docs/re/creature.md#creature-pack-rescue-downed.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "net/buffer.h"

namespace creature_rescue {

constexpr size_t kMaxPlayers = 4;     // net::kMaxPlayers
constexpr float kReachM = 6.f;        // a reviving partner's distance (Lineages: 6 m)
constexpr double kHoldS = 1.5;        // how long the partner stays that close (Lineages: 1.5 s)
constexpr float kUpHealth = 0.4f;     // a revived creature gets up with 40 % of its maximum health
constexpr double kProtectS = 2.0;     // after a revive: no hit for this long (the attacker is usually still there)
constexpr double kWipeAfterS = 2.0;   // everybody down this long: the party wipe (not one frame between two states)
constexpr double kNoHostS = 10.0;     // a guest whose host never answered ends its own window this long after its end
constexpr double kRequestS = 1.0;     // a downed guest repeats its request this often (the host may have missed it)
constexpr float kDnaShare = 0.10f;    // the pack's DNA a knockout nobody revived costs (Lineages' non-debt charge)

// the time a downed player waits for a partner (Lineages BUG-9149 core.down_60: a minute; off: BUG-9118's 30 s)
inline double down_seconds(bool minute) { return minute ? 60.0 : 30.0; }

// ---- a hit on the own creature (teamplay::on_own_damage) ----
struct DownIn {
    bool fix = false;          // creature.rescue
    bool creature = false;     // the Creature stage, the hit is on this game's own creature
    bool connected = false;    // in a session
    bool partner_here = false; // a partner plays in the same world (its creature is in it)
    float damage = 0, health = 0;
};
inline bool lethal(float damage, float health) { return std::isfinite(damage) && damage > 0 && health > 0 && damage >= health; }
inline bool should_down(const DownIn& in)
{
    return in.fix && in.creature && in.connected && in.partner_here && lethal(in.damage, in.health);
}

// ---- one knockout, as the host watches it ----
struct Watch {
    uint32_t id = 0;        // the downed game's knockout number (it counts up)
    bool active = false;
    double begin = 0;       // the window's start (host time)
    double near_since = -1; // a standing partner is within reach since then (-1: none)
    bool gave_up = false;   // its player gave up (hold G, coop.rescue giveup)
};
// a new knockout of that player starts (a repeated or older request of an ended one does not)
inline bool begin_ok(const Watch& w, uint32_t id) { return id && !w.active && (!w.id || int32_t(id - w.id) > 0); }
enum class Step : uint8_t { Waiting, Revived, Expired };
inline Step step(Watch& w, bool near, double now, double window)
{
    if (!w.active)
        return Step::Waiting;
    if (!near)
        w.near_since = -1;
    else if (w.near_since < 0)
        w.near_since = now;
    const bool saved = !w.gave_up && near && w.near_since + kHoldS <= std::min(now, w.begin + window);
    if (saved)
        return Step::Revived;
    if (w.gave_up || now - w.begin >= window)
        return Step::Expired;
    return Step::Waiting;
}
inline float revive_progress(const Watch& w, double now)
{
    if (!w.active || w.near_since < 0)
        return 0.f;
    return (float)std::clamp((now - w.near_since) / kHoldS, 0.0, 1.0);
}
inline double seconds_left(const Watch& w, double now, double window)
{
    return w.active ? std::max(0.0, w.begin + window - now) : 0.0;
}

// ---- the party wipe (Lineages BUG-9226) ----
// since: when nobody stood any more (-1: someone stands or nobody is down); true once: end every window now
inline bool wipe_due(double& since, bool any_down, bool anyone_standing, double now)
{
    if (!any_down || anyone_standing) {
        since = -1;
        return false;
    }
    if (since < 0) {
        since = now;
        return false;
    }
    if (now - since <= kWipeAfterS)
        return false;
    since = -1;
    return true;
}

// the pack's DNA a knockout nobody revived costs (rounded up, never more than the whole DNA)
inline float dna_cost(float dna)
{
    return std::isfinite(dna) && dna >= 1 ? std::min(std::floor(dna), std::ceil(dna * kDnaShare)) : 0.f;
}

// ---- MSG_CREATURE_RESCUE (95, family creature): u8 kind, u32 world (the sender's modes::current_world), then
//   K_DOWN    guest -> host: u32 knockout  (its own creature is down; repeated every second while it waits)
//   K_GIVE_UP guest -> host: u32 knockout  (its player gave up)
//   K_RESULT  host -> guest: u32 knockout, u8 result (1 revived, 2 expired)
//   K_LINES   host -> guests: u8 n, n x {u8 player, u16 tenths left, u8 revive %}: every second while a window is
//             open and once with n = 0 when the last one closed (the "DOWN" tags over the downed creatures)
// A peer before BUG-9283 drops the type (an unknown type of the newer family minor).
enum Kind : uint8_t { K_DOWN = 0, K_GIVE_UP = 1, K_RESULT = 2, K_LINES = 3 };
enum Result : uint8_t { R_REVIVED = 1, R_EXPIRED = 2 };
constexpr uint16_t kMaxTenths = 600; // a minute (BUG-9149)

struct Line {
    uint8_t player = 0;
    uint16_t tenths = 0;
    uint8_t revive = 0;
    bool operator==(const Line&) const = default;
};
inline uint16_t tenths_left(double seconds)
{
    const double t = std::ceil(seconds * 10);
    return (uint16_t)std::clamp(t, 0.0, (double)kMaxTenths);
}
inline void write_lines(net::Writer& w, const std::vector<Line>& lines)
{
    const size_t n = std::min(lines.size(), kMaxPlayers);
    w.u8((uint8_t)n);
    for (size_t i = 0; i < n; ++i) {
        w.u8(lines[i].player);
        w.u16(lines[i].tenths);
        w.u8(lines[i].revive);
    }
}
// false: malformed (the guest keeps what it had)
inline bool read_lines(net::Reader& r, std::vector<Line>& out)
{
    out.clear();
    const uint8_t n = r.u8();
    if (!r.ok() || n > kMaxPlayers)
        return false;
    for (uint8_t i = 0; i < n; ++i) {
        Line l;
        l.player = r.u8();
        l.tenths = r.u16();
        l.revive = r.u8();
        if (!r.ok() || l.player >= kMaxPlayers || l.tenths > kMaxTenths || l.revive > 100)
            return false;
        out.push_back(l);
    }
    return r.left() == 0;
}

} // namespace creature_rescue
