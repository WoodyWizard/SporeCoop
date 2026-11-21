---
id: BUG-9317
title: Civ: a party nation's global superweapon (types 2, 8) costs 12000 for every city not of the firer, the party's own cities included (decision 7T A)
status: fixed
kind: feature
stage: Civ
reported: 2026-11-21
source: owner decision 7T A (2026-11-21)
role: both
versions: 0.7.14.1-lineages67-dev
fixed_in: 0.7.14.1-lineages68-dev
branch: fix/l47-money
commits: cca8c393
tests: manual two-game live test (docs/06-testing.md#testing-reload-spend-sw-price); no lab test yet
signature:
docs: docs/re/civ.md#civ-reload-spend-sw-price
related: BUG-9313, BUG-9306, BUG-9309
---

## Report
Owner decision 7T A (2026-11-21): with 7P A (BUG-9313) a party nation's global superweapon (types 2, 8) spares the party's
cities, but its price was still 12000 for every city not of the firing nation, the party's own included. A player should
pay only for the cities the weapon hits, the host and a guest firing from its own nation alike, and the price the HUD
shows must match the charge.

## Root cause
`Simulator::cCivilization::GetSuperweaponCost` 0xBF6DF0 (Ghidra, GOG 2024): types 2 / 5 / 8 cost 12000 x max(1, every
city of the planet minus this civ's own). It is the one price the game uses: CanUse's affordable check 0xBF7870,
FireSuperweapon's charge (+0x98 -= cost, after the launch) and the superweapon tooltip 0xE05DC0.

## Fix
`civ.global_sw_party_price` (mods/coop/civ.cpp h_SwCost, on by default; off = the game's price): for types 2 and 8 of a
party founder nation, cities of any party nation are not counted: the price is the game's unit x max(1, the cities hit).
Host and guests (each game's tooltip and affordable check follow; the charge is the host's). Type 5 unchanged (it hits
no city). `coop.civ sw` shows `price2= price8=` per nation and the last changed price; the log says each change once.

## Verification
Live, TITAN QA copy (build fix/l47-money), same setup as BUG-9316, 2026-11-21 10:10-10:18. Abber's cities all belong to
the party: `coop.civ test_outsider <faction>` (host only) made one founder nation an outsider; a native NPC nation (21CE)
appeared later. Outputs out/qa-runs/20261121-l47-money/run-9317-*.txt, screenshots out/qa-shots/bug9317_*.png.
- FAIL host-fired (off on both, outsider 21CB, 1 city hit): HUD tooltip "ICBM (48,000)"; host `coop.civ super 2`: shared
  money 80940 -> 33340 on both games in 1 s (48000 charged, income +400), the guest's notice "CivHost spent 48000 money
  (shared)".
- PASS host-fired (on, outsider 21CC + NPC 21CE, 2 cities hit): tooltip "ICBM (24,000)"; 37300 -> 13660 on both
  (24000), "CivHost spent 24000 money (shared)".
- FAIL guest-fired (off, seat 4 = the guest's nation 21D0, outsider 21CD, 1 hit): seat 4 money 60000 -> 12000 on both
  games (48000).
- PASS guest-fired (on, outsider 21CC, 1 hit; the game's price 36000): seat 4 money 32000 -> 20000 on both (12000).
- Without the test knob host and guest show the same price (12000 with one NPC city); the guest's tooltip showed
  "ICBM (12,000)" (bug9317_pass_guest_tooltip_12000.png). No crash report.
- The host-fired runs ran on a build whose price log was not yet deduplicated (one line per call); the guest-fired runs
  on the final build (cca8c393). The price logic is the same in both.

## History
- 2026-11-21: opened (owner decision 7T A).
- 2026-11-21: fixed on fix/l47-money (civ.global_sw_party_price), live FAIL / PASS on TITAN.
- 2026-11-21: set to fixed for 68-dev: merged into feat/lineages-47 with the live FAIL / PASS in Verification (any open part stays in docs/known-issues.md).
- 2026-11-21: released in 0.7.14.1-lineages68-dev.
