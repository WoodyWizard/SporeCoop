---
id: BUG-0877
title: Civ: a guest that buys an NPC city pays for it, and the city changes hands in neither game
status: open
kind: bug
stage: Civ
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0775, BUG-0613
---

## Report
A guest with an economic nation buys an NPC city: it sees the purchase, the money leaves the shared account, the city stays the NPC's on both games.

## Root cause
Inferred from the code, confidence medium-high (that the spend reaches the shared money is not verified): BuyCity / FinishBuyCity are hooked nowhere; the game's path (buy 0xBE9980, docs/re/civ.md ~417) is SpendMoney first, then Capture; on a guest `h_Capture` (`mods/coop/civ.cpp:1685`) returns 0 outside `g_applying`; the protocol has no "buy a city" command (CMD_* near `civ.cpp:1217`).

## Fix idea
A new guest command (CMD_BUYCITY: city, price) that the host executes in its game (the game's own buy on the host, credited to the guest), refused with the money returned and a notice when the host cannot do it.

## How to test
Economic nation, the guest buys an NPC city through the game's UI; compare the shared money and the city's pol on both games: after the fix the city is the guest's on both, on main the money is gone and the city is still the NPC's.

## History
- 2026-11-10: opened from the Tribe / Civ / Space code audit for the 0.7.13 plan.
