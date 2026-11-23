// Civilization: every player's own money (the session setting civ.own_money, BUG-1468; idea and rules from PR #205 by
// IltonBJSilva). The rules are in civ_wallet_logic.h; docs/re/civ.md#civ-own-money.
//
// Wire (MSG_CIV_WALLET 74, civ family 1.2): u8 kind, then
//   K_ASK    guest -> host  (nothing): the guest plays the host's Civ world and has no wallet yet
//   K_SET    host -> guest  u32 seq, f64 money: the guest's wallet (a newcomer's part or its kept one); its game's money
//                           is set to it
//   K_ADD    host -> guest  u32 seq, f64 change: an income part or a refund (+), a purchase the host's game made for it (-)
//   K_REPORT guest -> host  u32 last seq applied, f64 money now (every 2 s while it has its wallet)
// Only between a host and a guest of civ 1.2 or newer (net/compat.h has_minor, civwallet::kMinor): an older guest plays
// the host's shared money (shared.cpp) as before and gets nothing of this.
#pragma once

#include <cstdint>
#include <string>

#include "net/buffer.h"
#include "sporekit.h"

namespace civ_wallet {

// the setting is on and a session runs (either role)
bool active();
// guest: its Civ money is its own wallet (shared.cpp leaves the Civ money total alone: own_budget, civ.wallet_row)
bool guest_own_money();
void tick(double now);
void on_message(uint8_t from, net::Reader& r);

// host: the game's passive income `gain` (already in the player civilization's money, civ.cpp h_AddMoney): the guests'
// parts by their cities leave the host's money into their wallets; returns the sum of those parts (0: none / off)
double host_income(void* civ, double gain);
// host: the trade between two players' cities (civ_host.cpp trade_tick) of `gain`: the guests' halves go to their
// wallets; returns their sum (the rest is the host's)
double host_trade_pair(uint8_t a, uint8_t b, double gain);
// host: a refund for guest `player` (a building the host's game could not make): true when it went to its wallet
bool host_refund(uint8_t player, double amount);
// host: guest `player` has a wallet now
bool host_has_wallet(uint8_t player);

// host: the game's own purchase code runs with guest `player`'s wallet as the civilization's money while this lives
// (its own "enough money?" check and the spend use it); what it spent is charged to the guest and the host's own money
// is put back. A no-op for the host itself, a guest without a wallet, or with the setting off.
class PayAs {
public:
    explicit PayAs(int player);
    ~PayAs();
    PayAs(const PayAs&) = delete;
    PayAs& operator=(const PayAs&) = delete;
    bool paying() const { return on_; }
    double wallet() const { return wallet_; }

private:
    bool on_ = false;
    uint8_t player_ = 0;
    double host_money_ = 0, wallet_ = 0;
    void* civ_ = nullptr;
};

std::string status();
int command(void*, int argc, const char* const* argv, SkOut* out); // coop.wallet

} // namespace civ_wallet
