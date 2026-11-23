// The rules of every player's own Civ money (the session setting civ.own_money, BUG-1468; idea and rules from PR #205
// by IltonBJSilva). civ_wallet.cpp is the game glue; docs/re/civ.md#civ-own-money. Portable (no Windows, no game):
// tests/civ_tests.cpp checks them natively.
//
// * The host keeps every guest's wallet; the host's own wallet is its game's money (cCivilization +0x98).
// * Income: the host's game earns it (guests ignore AddMoney for the player civilization, civ.cpp h_AddMoney). The
//   game's passive city income (the 6 s city and spice tick, the trade deliveries: the return addresses of BUG-1236,
//   docs/re/civ.md#civ-income-addmoney) is split by the player cities each player owns (civ.cpp g_city_owner); a shared
//   city (the capital while a player would have none) counts for every player with a wallet in equal parts; a city of
//   a player without a wallet (away, an older peer, not in Civ yet) stays with the host. Every other gain of the host's
//   game (a refund, a gift of another nation, a hut's loot) is the host's.
// * A guest's purchase that the host's game makes (a vehicle, a city bought, a superweapon) is paid from that guest's
//   wallet, which the host knows: the guest's last report plus the host's credits and charges the guest has not
//   confirmed yet (Ledger).
// * A guest the host never saw in this world gets an equal part of the host's money (money moves, none is made); a
//   rejoin gets its kept wallet (the per-player store, record civ.wallet).
// * A session that plays the world with the setting off gives the kept wallets back to the host's money once.
#pragma once

#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace civwallet {

// the civ family's minor of 0.7.17 (net/messages.def): a peer from it knows MSG_CIV_WALLET; an older one (0.7.16.1:
// civ 1.1) plays the shared money as before, the host gives it no wallet and its cities' income stays with the host
constexpr uint16_t kMinor = 2;

constexpr double kMoneyMax = 99999999.0; // the game's money clamps to 0..99 999 999 (shared.cpp money_clamp)

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
    size_t guests = 0;
    for (uint8_t g : wallets)
        guests += g != host;
    const double holders = 1.0 + (double)guests; // the host and every guest with a wallet
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

// the trade between the players' cities (civ_host.cpp trade_tick): a pair's `gain` goes half to each end; an end
// without a wallet (the host, or a guest without one) is the host's part. Returns the guests' parts.
inline std::map<uint8_t, double> split_pair(double gain, uint8_t a, uint8_t b, uint8_t host, const std::set<uint8_t>& wallets)
{
    std::map<uint8_t, double> out;
    if (!(gain > 0) || !std::isfinite(gain))
        return out;
    for (uint8_t o : {a, b})
        if (o != host && wallets.count(o))
            out[o] += gain / 2;
    return out;
}

// a newcomer's first wallet: an equal part of the host's money among the players with a wallet after it arrives
// (`holders` counts the host, the guests with a wallet and the newcomer); whole money, never more than the host has
inline double newcomer_share(double host_money, int holders)
{
    if (!(host_money > 0) || !std::isfinite(host_money) || holders < 2)
        return 0;
    return std::floor(clamp_money(host_money) / (double)holders);
}

// the kept wallets go back into the host's money (a session with the setting off on a world that has them): the new
// money of the host; nothing is made or lost but the game's cap
inline double merge_back(double host_money, const std::vector<double>& kept)
{
    double v = clamp_money(host_money);
    for (double k : kept)
        if (amount_ok(k) && k > 0)
            v += k;
    return clamp_money(v);
}

// the host's view of one guest's wallet: the guest reports its money and the last host operation it applied; the
// operations it has not applied yet are added on top
struct Ledger {
    double reported = 0;
    uint32_t acked = 0;                           // the last host operation the guest applied
    std::deque<std::pair<uint32_t, double>> sent; // host operations (seq, +credit / -charge) not confirmed yet

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

// ---- who has a wallet ----
// the host gives guest a wallet: the setting is on, the guest's mod reads MSG_CIV_WALLET (civ minor kMinor or newer, the
// same rev) and it plays the host's Civ world (not another world of its own)
inline bool host_gives(bool setting, bool peer_reads, bool peer_elsewhere) { return setting && peer_reads && !peer_elsewhere; }
// a guest runs its wallet: the setting is on and the host's mod sends wallets (its WELCOME's civ minor)
inline bool guest_runs(bool setting, bool host_reads) { return setting && host_reads; }
// a peer's families say it reads MSG_CIV_WALLET (net::compat::has_minor with the civ family's rev)
inline bool reads_wallet(bool has_families, bool found, uint16_t rev, uint16_t minor, uint16_t my_rev)
{
    return has_families && found && rev == my_rev && minor >= kMinor;
}

// civ.wallet_row: a guest with its own wallet does not take the host's shared Civ money total (shared.cpp MSG_RES id
// 2) nor send its own spending there; its money comes from MSG_CIV_WALLET. Off: the shared total overwrites the wallet.
inline bool guest_takes_shared(bool row_fix, bool wallet) { return !(row_fix && wallet); }

// ---- the stored wallet (record civ.wallet v1 of the per-player store, docs/01-architecture.md#arch-player-store) ----
// v1: i32 money (whole). A reader takes v1 only; another version is kept as it is (player_store.h).
constexpr uint16_t kRecVer = 1;
inline bool stored_ok(uint16_t ver, size_t bytes) { return ver == kRecVer && bytes == 4; }

} // namespace civwallet
