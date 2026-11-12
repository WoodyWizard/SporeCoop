---
id: BUG-0877
title: Civ: a guest that buys an NPC city pays for it, and the city changes hands in neither game
status: fixed
kind: bug
stage: Civ
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 
fixed_in: unreleased
branch: fix/w48-captures
commits: 70334b34, 7930f984, 5e81941b, 3ec69627
tests: civ_buy_city
signature:
docs: docs/re/civ.md#civ-capture-keeps
related: BUG-0775, BUG-0613
---

## Report
A guest with an economic nation buys an NPC city: it sees the purchase, the money leaves the shared account, the city stays the NPC's on both games.

## Root cause
* Observed (lab 20261112-003450, civ.buy_city off): the guest's game ran the purchase (cCity_Buy 0xBE99A0 for 500,
  as FinishBuyCity calls it); the city stayed the NPC's on both games (owner -1).
* Cause (Ghidra + code): cCity_Buy spends the money and calls Capture type 2; a guest's h_Capture refuses it and the
  protocol had no purchase. That the spend reaches the shared money was not measured (the test does not read money).

Earlier reading (2026-11-10):
Inferred from the code, confidence medium-high (that the spend reaches the shared money is not verified): BuyCity / FinishBuyCity are hooked nowhere; the game's path (buy 0xBE9980, docs/re/civ.md ~417) is SpendMoney first, then Capture; on a guest `h_Capture` (`mods/coop/civ.cpp:1685`) returns 0 outside `g_applying`; the protocol has no "buy a city" command (CMD_* near `civ.cpp:1217`).

## Fix idea
A new guest command (CMD_BUYCITY: city, price) that the host executes in its game (the game's own buy on the host, credited to the guest), refused with the money returned and a notice when the host cannot do it.

## How to test
Economic nation, the guest buys an NPC city through the game's UI; compare the shared money and the city's pol on both games: after the fix the city is the guest's on both, on main the money is gone and the city is still the NPC's.

## Fix
civ.cpp: `h_CityBuy` (hook of SporeCoop::cCity_Buy, GOG 2024 0xBE99A0): a guest's purchase for the player
civilization sends CMD_BUYCITY (6: city index, price) and returns without spending; the host runs cCity_Buy in its
game credited to that guest (its money, its Capture); a purchase that did not take (money unchanged) tells the guest.
The host's own purchase is credited to the host. Switch `coop.fix civ.buy_city off` (0.7.12).

## Verification
`coop_civ_buy_city.py` (suite `civ_buy_city`, civ_npc): `coop.civ buycity <city> 500` on the guest (the game's
cCity_Buy; the talk's accept itself is not driven); within 15 s both games show the city as the player civilization's
and the guest's (owner 1).
* FAIL with `SPORE_FIX_OFF=civ.buy_city`: lab 20261112-003450.
* PASS: lab 20261112-003012, 20261112-004830 (lane 4).

## History
- 2026-11-10: opened from the Tribe / Civ / Space code audit for the 0.7.13 plan.
- 2026-11-12 (fix/w48-captures): fixed, FAIL and PASS runs in Verification.
