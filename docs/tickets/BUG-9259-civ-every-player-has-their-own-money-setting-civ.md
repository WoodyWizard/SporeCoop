---
id: BUG-9259
title: Civ: every player has their own money (setting civ.own_money)
status: open
kind: feature
stage: Civ
reported: 2026-11-19
source: owner request 2026-11-19: separate money per player
role: both
versions: 0.7.15.2
fixed_in:
branch: feat/civ-own-money (fork IltonBJSilva/SporeCoop)
commits:
tests: tests/civ_tests.cpp (wallets), tools/tests/coop_civ_own_money.py (suite civ_own_money)
signature:
docs: docs/re/civ.md#civ-own-money
related:
---

## Report
The owner plays Civilization with friends and wants every player to have their own money instead of one shared
treasury (Space has its own budgets in the rivals mode already; Civ had no option).

## Root cause
By design: shared.cpp keeps one Civ money total (the host's), guests send their spending and drop their own income.

## Fix
The session setting civ.own_money (off by default): civ_wallet.cpp keeps a wallet per player, splits the host's
income by the cities each player owns, pays a guest's purchases that the host's game makes from that guest's wallet
(PayAs), gives a newcomer an equal part of the host's money and keeps the wallets by store key across rejoins and
sessions. MSG_CIV_WALLET (74, civ family 1.1). docs/re/civ.md#civ-own-money.

## Verification
`make test` passes (SKIP_SANITIZERS=1, WSL Ubuntu 24.04): tests/civ_tests.cpp `wallets` (the split, the newcomer's
part, the ledger).
Run in the game on 2026-11-19, Windows 11, Steam build (`3010a413...`), two instances on one PC with test profiles, a
new Civ world (economic), the setting switched on in the galaxy menu before the world loaded:
* the join: the host had 2000; the guest's game showed 1000 and the host's 1000
* `coop.wallet income 400` (the capital shared: owner 253): the guest +200, the host +200; the game's own income
  (spice) was split the same way afterwards (`shared_out` grew)
* the guest bought a land vehicle (`coop.civ buy 0 0`): the guest's money 1600 -> 760 (+160 income, -1000 the
  vehicle), the host's money did not pay it; the host's log: "player 1's purchase in the host's game: wallet 1600,
  spent 1000"
* a rejoin (and a hot reload of the host's mod) gave the guest its kept wallet back (`kept_back=1`, from the store on
  the host's disk)
* found and fixed on the way: inside `PayAs` the unqualified `active()` was the member (`on_`, false), so the first
  build charged the guest's vehicle to the host's money (host 1200 -> 200, guest unchanged)
Not seen yet: a purchase refused for the guest's money (the city had no vehicle slot left in the run). The lab run of
tools/tests/coop_civ_own_money.py is still to do (no Linux lab on this PC).
no-fail-run: a new feature: on main `coop.wallet` and the setting do not exist.

## History
- 2026-11-19: opened.
- 2026-11-19: implemented on feat/civ-own-money, tested in the game on two instances (Windows, Steam).
