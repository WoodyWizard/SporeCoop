// Civilization: every player's own money (the session setting civ.own_money, BUG-9259). The rules are in
// civ_wallet_logic.h; docs/re/civ.md#civ-own-money.
//
// Wire (MSG_CIV_WALLET, civ family): u8 kind, then
//   K_ASK    guest -> host  (nothing): the guest's Civ world opened, it wants its wallet
//   K_SET    host -> guest  u32 seq, f64 money: the guest's wallet (its game's money is set to it)
//   K_ADD    host -> guest  u32 seq, f64 change: an income share or a refund (+), a purchase the host made for it (-)
//   K_REPORT guest -> host  u32 last seq applied, f64 money now (every 2 s while it has its wallet)
//   K_NATION host -> guest  u32 seq, f64 money: BUG-9274, the guest's own nation's money now (its game's money is set
//                           to it without a message); a guest with its own nation gets its wallet from that nation
//                           (cCivilization +0x98 on the host), also with the setting off (civ_nations.h)
#pragma once

#include <cstdint>
#include <string>

#include "net/buffer.h"
#include "sporekit.h"

namespace civ_wallet {

// the setting is on and a session runs (both roles); shared.cpp then leaves the Civ money alone
bool active();
void tick(double now);
void on_message(uint8_t from, net::Reader& r);
void on_player_leave(uint8_t id);

// host: the game's AddMoney for the player civilization (civ.cpp h_AddMoney): true when the gain was taken and split
bool host_add_money(void* civ, float amount, int difficulty);
// host: a refund for guest `player` (a building the host's game could not make): true when it went to its wallet
bool host_refund(uint8_t player, double amount);
// host, BUG-9274: guest `player`'s own nation has `money`: its game shows it (K_NATION when it changed)
void nation_sync(uint8_t player, double money);

// host: the game's own purchase code runs with guest `player`'s wallet as the civilization's money while this lives;
// what it spent is charged to the guest, the host's own money is put back. A no-op for the host itself or when off.
class PayAs {
public:
    explicit PayAs(int player);
    ~PayAs();
    PayAs(const PayAs&) = delete;
    PayAs& operator=(const PayAs&) = delete;
    bool active() const { return on_; }

private:
    bool on_ = false;
    uint8_t player_ = 0;
    double host_money_ = 0, wallet_ = 0;
    void* civ_ = nullptr;
};

std::string status();
int command(void*, int argc, const char* const* argv, SkOut* out); // coop.wallet

} // namespace civ_wallet
