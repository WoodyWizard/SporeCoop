// The rules of the Civilization stage's own money per player (civ.own_money, BUG-9259; civ_wallet.cpp is the game
// glue). Portable (no Windows, no game): tests/civ_tests.cpp checks them natively.
//
// * The host keeps every guest's wallet; the host's own wallet is its game's money (cCivilization +0x98).
// * Income: the host's game earns it (guests ignore AddMoney for the player civilization, civ.cpp h_AddMoney). The
//   gain is split by the player cities each player owns (civ.cpp g_city_owner); a shared city (the capital while a
//   player would have none) counts for every player with a wallet in equal parts; a city of a player without a wallet
//   (away, or not in Civ yet) stays with the host.
// * A guest's purchase that the host's game makes (a vehicle, a city bought) is paid from that guest's wallet, which the
//   host knows: the guest's last report plus the host's credits and charges the guest has not confirmed yet (Ledger).
// * A guest the host never saw in this world gets an equal part of the host's money (money moves, none is made).
#pragma once

#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <utility>

namespace civwallet {

constexpr double kMoneyMax = 99999999.0; // the game's AddMoney clamps to 0..99 999 999 (shared.cpp money_clamp)

inline bool amount_ok(double v) { return std::isfinite(v) && v >= -kMoneyMax && v <= kMoneyMax; }
inline double clamp_money(double v) { return !std::isfinite(v) || v < 0 ? 0 : v > kMoneyMax ? kMoneyMax : v; }

constexpr uint8_t kSharedCity = 0xFD; // civ_internal.h kShared

// the guests' parts of an income `gain` the host's game took: `owners` has one entry per player city (its owner id,
// kSharedCity for a shared one), `host` is the host's id, `wallets` the guests that have a wallet now. What is not in
// the result stays with the host.
inline std::map<uint8_t, double> split_income(double gain, const std::multiset<uint8_t>& owners, uint8_t host,
                                              const std::set<uint8_t>& wallets)
{
    std::map<uint8_t, double> out;
    if (!(gain > 0) || !std::isfinite(gain) || owners.empty())
        return out;
    const double per_city = gain / (double)owners.size();
    const double holders = 1.0 + (double)wallets.size(); // the host and every guest with a wallet
    for (uint8_t o : owners) {
        if (o == kSharedCity) {
            for (uint8_t g : wallets)
                if (g != host)
                    out[g] += per_city / holders;
        } else if (o != host && wallets.count(o))
            out[o] += per_city;
    }
    return out;
}

// a newcomer's first wallet: an equal part of the host's money among the players with a wallet after it arrives
// (`holders` counts the host, the guests with a wallet and the newcomer); whole dollars, never more than the host has
inline double newcomer_share(double host_money, int holders)
{
    if (!(host_money > 0) || !std::isfinite(host_money) || holders < 2)
        return 0;
    return std::floor(clamp_money(host_money) / (double)holders);
}

// the host's view of one guest's wallet: the guest reports its money and the last host operation it applied; the
// operations it has not applied yet are added on top
struct Ledger {
    double reported = 0;
    uint32_t acked = 0;                            // the last host operation the guest applied
    std::deque<std::pair<uint32_t, double>> sent;  // host operations (seq, +credit / -charge) not confirmed yet

    void set(uint32_t seq, double v)
    {
        reported = clamp_money(v);
        acked = seq;
        sent.clear();
    }
    void op(uint32_t seq, double amount) { sent.push_back({seq, amount}); }
    // a report: false (ignored) when it is older than the last one or not a sane amount
    bool report(uint32_t applied, double money)
    {
        if (!amount_ok(money) || applied < acked)
            return false;
        acked = applied;
        reported = clamp_money(money);
        while (!sent.empty() && sent.front().first <= applied)
            sent.pop_front();
        return true;
    }
    double value() const
    {
        double v = reported;
        for (auto& s : sent)
            v += s.second;
        return clamp_money(v);
    }
};

} // namespace civwallet
