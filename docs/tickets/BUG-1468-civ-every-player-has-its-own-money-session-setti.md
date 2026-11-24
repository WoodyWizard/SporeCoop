---
id: BUG-1468
title: Civ: every player has its own money (session setting civ.own_money; idea and RE from PR #205 by IltonBJSilva)
status: fixed
kind: feature
stage: Civ
reported: 2026-11-23
source: plan, PR #205
role: 
versions: 0.7.17
fixed_in: 0.7.17
branch: feat/w68-wallet
commits: 162b29f0 ebe0e45a 86ef0e48
tests: tests/civ_tests.cpp (wallets), tests/validate_tests.cpp (manifest 74, settings row), tools/tests/coop_civ_own_money.py (civ_own_money, civ_own_money_merge, civ_own_money_old, twins civ_own_money_setting_off, civ_own_money_row_off), coop_campaign.py segment civ_own_money_space
signature:
docs: docs/re/civ.md#civ-own-money, docs/04-netcode.md#netcode-civ-wallet, docs/01-architecture.md#arch-player-store, docs/07-modes.md#modes-session-settings
related: BUG-1236, BUG-1461
---

## Report
Owner decision 2026-11-20 (a friend's request): each player has its own Civ money: its cities' income is its own, its purchases are paid from it, a newcomer gets a fair part, nothing is made or lost. Design, wire (civ 1.2, MSG 74 CIV_WALLET), store record `civ.wallet`, switches and tests: docs/plan-0.7.17.md item 1. Credit IltonBJSilva (PR #205) in the CHANGELOG and release notes.

## Root cause
Design (a feature): by default shared.cpp keeps one Civ money (the host's game's cCivilization +0x98) for the party:
guests drop AddMoney for the player civilization and send their spending as MSG_RES_ADD. The owner's friends asked
for every player's own money (2026-11-20). PR #205 by IltonBJSilva gave the rules (civ_wallet_logic.h: the split by
cities, the newcomer's part, the Ledger) and the game glue (PayAs, K_ASK / K_SET / K_ADD / K_REPORT); ported as an
idea on our code with what #205 lacked (the review out/research/prs-204-209.md): the old-peer guard, the superweapon,
turning it off, Civ -> Space, the shared row, a switch and twins.

## Fix
Session setting `civ.own_money` (Civilization, "Own money"; default off, provisional: settings::kCivOwnMoneyDefault,
owner question 1). mods/coop/civ_wallet.cpp, rules civ_wallet_logic.h (docs/re/civ.md#civ-own-money):
* The host keeps a wallet per guest; its own is its game's money. The game's passive income (the 6 s city and spice
  tick, the two trade deliveries: BUG-1236's return addresses, one classifier in civ.cpp h_AddMoney) is split by the
  player cities each player owns (a shared capital equally between the host and the guests with a wallet; a city of a
  player without one stays with the host); the players' city trade (trade_tick) half to each end.
* A guest's vehicle (host_buy), city purchase (CMD_BUYCITY) and superweapon (sw_fire_for) run inside PayAs: the game's
  own checks and spends use the guest's wallet, the cost is charged to it, the host's money put back; a short wallet:
  the game refuses, the guest gets "no vehicle: not enough money of your own" / the superweapon notice. A refund of a
  building the host's game could not make goes into the wallet.
* A newcomer gets floor(host money / holders) (money moves, none is made); a rejoin its kept wallet (store record
  `civ.wallet` v1: i32 money per stable key, written at the game's saves with the host's save).
* Old-peer guard: MSG_CIV_WALLET 74 only between peers of civ 1.2 (civwallet::kMinor; `peer_families` on the host,
  `host_families` on the guest, each also checks its own announced minor). A 0.7.16.1 guest plays the shared money,
  its cities' income stays with the host; the host keeps sharing MSG_RES with it.
* The guest's HUD shows its wallet (its +0x98 is set by K_SET / K_ADD); a guest with a wallet neither takes the host's
  shared Civ total nor sends its changes (shared.cpp own_budget; switch `civ.wallet_row`, off: the shared total
  overwrites the wallet). CORRECTION of the plan: the wsync row civ_money never touched the player civilization (NPC
  nations only, wsync.cpp npc_civs); the "row" a wallet guest must skip is shared.cpp's MSG_RES id 2.
* Turning it off: a world played with the setting off gives its kept wallets back to the host's money once and erases
  the records (`coop.wallet` merged=). Civ -> Space: RE (objdump) shows the Civ money is not carried at all (every new
  empire gets SetMoney 100000 of the Space config, docs/re/civ.md#civ-own-money), so the wallets end in Space (records
  erased, ended=), the shared Space money is the game's as today and the Rivals split stays total / players.
* A guest in another Civ world of its own gets no wallet operations (peer_elsewhere; it asks again when back).
`coop.wallet` prints the state; `coop.wallet income <n>` (tests) the host's city income through the same split.

## Verification
Unit: tests/civ_tests.cpp `wallets` (split by cities, shared capital, no money made for any owner mix, the trade
pair, the newcomer 2000 -> 1000 / 1000, the Ledger with a purchase and an income in flight, merge back sum before =
sum after, the peer guard with civ 1.1 / 1.2, the row switch), tests/validate_tests.cpp (MSG 74 row, the settings row
and its default): `make test` passes.
Lab: pending night run: civ_own_money, civ_own_money_merge, civ_own_money_old (PASS expected), twins
civ_own_money_setting_off and civ_own_money_row_off (must FAIL), civ_own_money_space (campaign segment, 45 min);
regressions civ_coop, civ_money_short, civ_build_refund, civ_buy_city, civ_sw_vote, civ_other_world, space_rivals*.
Status open until the night run's dirs are here. no-fail-run for the feature itself: a new feature (on rel/0.7.17
before it `coop.wallet` does not exist); the twins are the FAIL side.
0.7.17 release lab: PASS: settings (lab 20261124-020506), civ_own_money (lab 20261124-020506), civ_own_money_merge (lab 20261124-020506), civ_own_money_old (lab 20261124-020506); FAIL (fix off): civ_own_money_row_off (lab 20261124-013101).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 1 (WP-M1), branch feat/w68-wallet.
- 2026-11-23: implemented on feat/w68-wallet (commits 162b29f0, ebe0e45a and after); unit tests pass; lab tests and twins written for the night run.
- 2026-11-24: released in 0.7.17.
