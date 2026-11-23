// Revive a downed player (WP-R1 of 0.7.17, BUG-1472; the rule header and the K_DOWN / K_RESULT / K_LINES flow ported
// from PR #214's creature_rescue_logic.h by IltonBJSilva, on our teamplay.cpp and own_progress). The game glue:
// revive.cpp; the design: docs/03-gameplay.md#gameplay-revive.
//
// * The first line is the pack rescue (teamplay.cpp, BUG-0099: a lethal hit next to a partner leaves 40 % health, once a
//   minute; owner question 4 of docs/plan-0.7.17.md, provisional). When it does not save the creature (on cooldown, no
//   partner within 15 m, creature.pack_rescue off) and a partner plays in the same world, the lethal hit downs it
//   instead: 1 health, held where it fell (stopped, put back, no hit lands), for kWindowS.
// * A standing partner within kReachM of it for kHoldS revives it at kUpHealth of its maximum health. The window runs out,
//   its player gives up (hold G) or the game kills it another way: it dies the game's way (the caption, the respawn at
//   the nest). Everyone down for kWipeAfterS: the party wipe (every window ends at once).
// * The price of a death after a knockout is the downed player's own (never #214's 10 % of the host's DNA):
//   kCostShare of its own DNA, from its own wallet (all.shared_dna own wallet), the shared pool (shared), or as a debt on
//   its evolution bar (own progress: the next gains pay it first; the `progress` record's debt column).
// * The host decides (it holds the NPCs' damage): it watches every knockout (its own, a guest's from K_DOWN), tells the
//   verdict (K_RESULT, with the price) and sends the windows every second (K_LINES: the "DOWN 55 s" tags and arrows).
// * Old peers: a guest only downs itself under a host of creature 1.3 (kMinor); the host ignores K_DOWN of an older
//   peer and sends its lines only to peers that read them. A 0.7.16.1 peer dies as before.
//
// Portable (no game, no Windows): tests/revive_tests.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "net/buffer.h"
#include "net/compat.h"

namespace revive {

constexpr size_t kMaxPlayers = 4;   // net::kMaxPlayers
constexpr double kWindowS = 60.0;   // how long a downed player waits for a partner (PR #214: a minute)
constexpr float kReachM = 6.f;      // a reviving partner's distance
constexpr double kHoldS = 1.5;      // how long the partner stays that close
constexpr float kUpHealth = 0.4f;   // a revived creature gets up with 40 % of its maximum health
constexpr double kProtectS = 2.0;   // after a revive: no hit for this long (the attacker is usually still there)
constexpr double kWipeAfterS = 2.0; // everybody down this long: the party wipe (not one frame between two states)
constexpr double kNoHostS = 10.0;   // a guest whose host never answered ends its own window this long after its end
constexpr double kRequestS = 1.0;   // a downed guest repeats its request this often (the host may have missed it)
constexpr float kAnchorM = 0.4f;    // a held creature further than this from where it fell is put back
// the price of a death after a knockout: this share of the downed player's own DNA (owner question 4, provisional:
// "10 % of the downed player's own DNA"; the one constant)
constexpr float kCostShare = 0.10f;

// the creature family minor from which a peer reads MSG_RESCUE (WP-0 of 0.7.17: creature 1.3)
constexpr uint16_t kMinor = 3;
inline bool peer_reads(const std::vector<net::FamilyRev>& fams, bool has)
{
    return net::compat::has_minor(fams, has, (uint8_t)net::Family::creature, 1, kMinor);
}

inline bool lethal(float damage, float health)
{
    return std::isfinite(damage) && std::isfinite(health) && damage > 0 && health > 0 && damage >= health;
}

// ---- a lethal hit on the own creature: who catches it (teamplay::on_own_damage). The pack rescue first (owner
// question 4: "keep pack rescue as the first line and revive after it", provisional), then the knockout
enum class Catch : uint8_t { Pass, Pack, Down };
struct HitIn {
    bool lethal = false;
    bool pack = false;       // the pack rescue would save it now (on, ready, a partner within 15 m)
    bool revive = false;     // creature.revive on, in a session, the Creature stage, the own creature
    bool partner_here = false; // a partner plays in the same world (its creature in it, fresh)
    bool host_reads = true;  // a guest: its host reads MSG_RESCUE (creature 1.3); the host itself: true
};
inline Catch catch_hit(const HitIn& in)
{
    if (!in.lethal)
        return Catch::Pass;
    if (in.pack)
        return Catch::Pack;
    if (in.revive && in.partner_here && in.host_reads)
        return Catch::Down;
    return Catch::Pass;
}

// ---- one knockout, as the host watches it
struct Watch {
    uint32_t id = 0;        // the downed game's knockout number (it counts up)
    bool active = false;
    double begin = 0;       // the window's start (host time)
    double near_since = -1; // a standing partner is within reach since then (-1: none)
    bool gave_up = false;   // its player gave up (hold G, coop.revive giveup) or its game killed it another way
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

// ---- the party wipe: since = when nobody stood any more (-1: someone stands or nobody is down); true once: end every
// window now
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

// ---- the price (host): of the downed player's own DNA, rounded up, never more than it has
inline float dna_cost(float dna)
{
    return std::isfinite(dna) && dna >= 1 ? std::min(std::floor(dna), std::ceil(dna * kCostShare)) : 0.f;
}
enum PriceKind : uint8_t {
    kPriceNone = 0,
    kPricePool = 1,   // the shared pool (the host's total: everybody's DNA)
    kPriceWallet = 2, // the downed player's own wallet (the host's own DNA, or a guest's book on the host)
    kPriceDebt = 3,   // own progress: a debt on its bar, paid from its next gains
};
struct Price {
    PriceKind kind = kPriceNone;
    float amount = 0;
};
struct PriceIn {
    bool own_price = true; // creature.revive_debt (off: #214's rule, kCostShare of the host's total for every death)
    int dna_mode = 0;      // own_progress::Mode as played: 0 own wallet, 1 shared, 2 own progress
    bool own_wallet = false; // the player has its own wallet now (the host itself in own wallet; a guest of creature 1.2+)
    float player_dna = 0;  // the downed player's own DNA (its wallet, its own progress DNA)
    float pool_dna = 0;    // the host's DNA (the shared pool in shared mode)
};
inline Price price_for(const PriceIn& in)
{
    Price p;
    if (!in.own_price) {
        p.kind = kPricePool;
        p.amount = dna_cost(in.pool_dna);
    } else if (in.dna_mode == 2) {
        p.kind = kPriceDebt;
        p.amount = dna_cost(in.player_dna);
    } else if (in.dna_mode == 0 && in.own_wallet) {
        p.kind = kPriceWallet;
        p.amount = dna_cost(in.player_dna);
    } else {
        p.kind = kPricePool;
        p.amount = dna_cost(in.pool_dna);
    }
    if (p.amount <= 0)
        p.kind = kPriceNone;
    return p;
}

// ---- the debt (own progress): a gain pays it first; returns what is left of the gain for the game (DNA and bar)
constexpr float kMaxDebt = 1.0e7f;
inline float pay_from_gain(float& debt, float gain)
{
    if (!(gain > 0) || !(debt > 0) || !std::isfinite(debt))
        return gain;
    const float pay = std::min(debt, gain);
    debt -= pay;
    if (debt < 0.01f)
        debt = 0;
    return gain - pay;
}
inline bool sane_debt(float d) { return std::isfinite(d) && d >= 0 && d <= kMaxDebt; }

// ---- MSG_RESCUE (77, family creature 1.3): u8 kind, u32 world (the sender's modes::current_world), then
//   K_DOWN    guest -> host: u32 knockout  (its own creature is down; repeated every second while it waits)
//   K_GIVE_UP guest -> host: u32 knockout  (its player gave up, or its game killed it another way)
//   K_RESULT  host -> guest: u32 knockout, u8 result (1 revived, 2 expired), u8 price kind, f32 price
//   K_LINES   host -> guests: u8 n, n x {u8 player, u16 tenths left, u8 revive %}: every second while a window is
//             open and once with n = 0 when the last one closed (the "DOWN" tags over the downed creatures)
// A peer before creature 1.3 drops the type (unknown), and is sent none.
enum Kind : uint8_t { K_DOWN = 0, K_GIVE_UP = 1, K_RESULT = 2, K_LINES = 3 };
enum Result : uint8_t { R_REVIVED = 1, R_EXPIRED = 2 };
constexpr uint16_t kMaxTenths = 600; // a minute

struct Verdict {
    uint32_t id = 0;
    uint8_t result = 0;
    Price price;
};
inline void write_verdict(net::Writer& w, const Verdict& v)
{
    w.u32(v.id);
    w.u8(v.result);
    w.u8(v.price.kind);
    w.f32(v.price.amount);
}
// after the kind and world; false: malformed
inline bool read_verdict(net::Reader& r, Verdict& out)
{
    Verdict v;
    v.id = r.u32();
    v.result = r.u8();
    const uint8_t k = r.u8();
    v.price.amount = r.f32();
    if (!r.ok() || !v.id || (v.result != R_REVIVED && v.result != R_EXPIRED) || k > kPriceDebt ||
        !std::isfinite(v.price.amount) || v.price.amount < 0 || v.price.amount > kMaxDebt)
        return false;
    v.price.kind = (PriceKind)k;
    out = v;
    return true;
}

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

} // namespace revive
