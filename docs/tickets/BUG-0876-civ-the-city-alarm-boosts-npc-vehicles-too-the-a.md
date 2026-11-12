---
id: BUG-0876
title: Civ: the city alarm boosts NPC vehicles too, the attacking army included
status: fixed
kind: bug
stage: Civ
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 
fixed_in: unreleased
branch: fix/w48-civ-speed
commits: 69eecd6d, 942fa887, 1342d697
tests: civ_boost_stack
signature: coop.civ boosts lists owner=254 entries after an alarm
docs: docs/re/civ.md#civ-boost-stacking, docs/re/civ.md#civ-boost-measured, docs/06-testing.md#testing-civ-speed
related: BUG-0771, BUG-0773
---

## Report
Found with GitHub #199 (BUG-0771): the +50 % alarm boost is given to every vehicle within 600 m that is not the alarmed city's owner, also to NPC nations' vehicles, so the army that attacks the city drives faster as well (and stacks the same way).

## Root cause
Read in the code, confidence high: `mods/coop/civ.cpp:1880` filters only `h.owner == owner`; NPC vehicles are in `g_hv` with owner 0xFE.

## Fix idea
Skip owner >= 0xFE and players that are not in the session when the boost is handed out.

## How to test
`coop.civ alarm <a player's city>` with NPC vehicles within 600 m, then `coop.civ boosts`: no entry with owner=254 (there are on main); read +0x218 of an NPC vehicle before and after.

## Fix
civ.cpp h_OnAttacked: vehicles with owner >= 0xFE (NPC nations) get no boost. Players who left keep no vehicles
(BUG-0031 gives them to a present player), so the owner check is enough. Switch `coop.fix civ.boost_players off` =
0.7.12.

## Verification
Test `civ_boost_stack` step 1: NPC vehicle #2 moved within 600 m of the capital, the alarm, then no boost entry
with owner 254 and the NPC vehicle's +0x218 unchanged.
* FAIL: out/lab/20261112-003106 (`SPORE_FIX_OFF=civ.boost_players`, lane 3): 3 NPC vehicles boosted, #2 12.27 -> 18.41.
* PASS: out/lab/20261112-002848, out/lab/20261112-003341, out/lab/20261112-003838 (0 boosted, 12.27 kept).

## History
- 2026-11-10: opened from the Tribe / Civ / Space code audit for the 0.7.13 plan.
- 2026-11-12: fixed on fix/w48-civ-speed (civ.boost_players), FAIL 20261112-003106, PASS 20261112-002848 / -003341 / -003838.
