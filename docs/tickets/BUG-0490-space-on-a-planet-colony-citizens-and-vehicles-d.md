---
id: BUG-0490
title: Space: on a planet, colony citizens and vehicles differ between the games
status: fixed
kind: desync
stage: Space
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, out/detector/20261103-final.md)
role: 
versions: 
fixed_in: 0.7.6
branch: fix/w42-space-planets
commits: 6117b9d3 44483073 edcad39c
tests: space_planet_citizens, space_planet_vehicles (tools/tests/coop_space_planet.py)
signature:
docs: docs/re/space.md#space-planet-life, docs/06-testing.md#testing-space-planet
related: BUG-0476, BUG-0537
switch: space.citizens, space.vehicles
---

## Report
Detector (rare, 1% of Space comparisons): `Space:unmatched:citizen` (38 host / 20 guest, 22 without a partner), `unmatched:vehicle` (1 host / 3 guest), `scale:citizen` (2.19 / 1.82). Colonies on a planet are simulated by each game; the Space stage mirrors ships, not planet life. Class G4, low priority.
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lane 3, GOG 2024, the `space` fixture's home planet; docs/re/space.md#space-planet-life):
* Citizens: a pool of 150 per planet in every game, put at the cities near the game's own camera (moving the guest's
  ship over another city moved 42 of the guest's citizens there; the host's stayed). Most stand, some run about (~5 m/s). Where both
  games populate a city, 123-145 of 150 stand within 3 m of a partner, the others elsewhere, each of its own size.
  The detector's `guest_only:citizen` of space_play (19 citizens near the guest, none on the host) was the guest
  respawned at a city the host's camera never came near: the host's game had no citizens there at all (level of
  detail).
* Vehicles: 40 on both games (land, sea, air of the player's empire), ~20 m/s, each driven by its own game's planner:
  25 of 40 more than 15 m from any partner.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
World-object kinds of the host on a planet (worldobj.cpp, stage bit kStSpace = 8): `space_citizen` (the host's citizens
within 400 m of a guest's ship's ground point; the guest binds its own by position (30 m) and puts each where the
host's is every frame, moved on with its velocity: a MoveTo hardly moved them) and `space_vehicle` (every host vehicle of the planet with its orientation every 0.4 s; the
guest binds its own of the same class (pol, locomotion, purpose) and draws each where the host's is every frame:
StopMovement + Teleport, as civ.cpp's mirrors). Their size: fields kinds `space_citizen` / `space_vehicle`
(K_SPACE_CITIZEN 6, K_SPACE_VEHICLE 7). Citizens at a city only the guest's camera reached stay the guest's own (the
host has none); the detector skips those. Switches `coop.fix space.citizens off`, `space.vehicles off`,
`space.planet_scale off`.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
* FAIL with SPORE_FIX_OFF=space.citizens: out/lab/20261104-103904 (34 of 57 host citizens near the guest without a guest
  citizen within 3 m; 4 of 57 of another size); earlier code: out/lab/20261104-102245 (40 of 57).
* FAIL with SPORE_FIX_OFF=space.vehicles: out/lab/20261104-102424 (74 of 160 host vehicles more than 15 m from any guest
  vehicle).
* PASS: out/lab/20261104-103824 (space_planet_citizens, space_planet_vehicles, space_colony_spice: 3 of 3); vehicles
  also out/lab/20261104-102216 (0 of 160 off). A put-only follow failed its own test (out/lab/20261104-103143: 6 of 57
  off: some citizens run): replaced by the per-frame drive.
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

Detector after the fix (SPORE_DETECTOR=20, out/lab/20261104-104326, report out/detector/20261104-1105.md):
space_play and detect_space no citizen / vehicle row; play_space `Space:unmatched:citizen` in 1 of 62 comparisons
(46 host / 48 guest citizens in the area, 3 guest citizens without a host partner within 15 m: own copies at the edge
of the host's 400 m snapshot, not dropped), not fixed. Detector rule: citizens near the guest are compared only when the
host's game has some there (level of detail).

## History
- 2026-11-03: opened.
- 2026-11-04 (fix/w42-space-planets): fixed (docs/re/space.md#space-planet-life).
- 2026-11-04: released in 0.7.6.
