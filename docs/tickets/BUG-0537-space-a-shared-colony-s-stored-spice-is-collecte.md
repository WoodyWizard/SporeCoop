---
id: BUG-0537
title: Space: a shared colony's stored spice is collected by each game on its own (spice twice; the spice pickups by the cities stay in the other game)
status: fixed
kind: desync
stage: Space
reported: 2026-11-04
source: divergence detector (space_play, out/detector/20261104-0803.md of rel/0.7.5; Space:noun_only_host:planetary_artifact / Space:host_only:planetary_artifact)
role: 
versions: 
fixed_in: 0.7.6
branch: fix/w42-space-planets
commits: 6117b9d3 44483073 edcad39c 09db7d24
tests: space_colony_spice (tools/tests/coop_space_planet.py --case spice)
signature:
docs: docs/re/space.md#space-planet-life, docs/06-testing.md#testing-space-planet
related: BUG-0479, BUG-0490
switch: space.colony_spice
---

## Report
Detector, space_play (3 of 3 runs of rel/0.7.5 and the baseline of this branch, out/lab/20261104-094153):
`Space:noun_only_host:planetary_artifact` (9 in the host's game, 0 in the guest's, whole list) and
`Space:host_only:planetary_artifact` (one near the guest on the host only). Found by the lab (no player report).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lane 3, GOG 2024; docs/re/space.md#space-planet-life): the 9 planetary artifacts are the spice pickups
of the home colony's 9 cities (record items of type 4, key 0x1CA01562 Red spice, ~30 m from each city), kept in
step with each city's stored spice (cCityData +0x1C) by the game's pickup update 0xC82B70. A ship entering a star
system collects the stored spice of the player's colonies there into its hold: in space_play the guest jumps to
the next star and back, its game collected 10 spice (8 of 9 cities 0.0), and its pickup update destroyed the
pickups (the diagnostic hook on DestroyInstance: 9 artifacts from 0xC82E07 inside 0xC82B70); the host, which
never left, kept 1.2 per city and its 9 pickups. The colony is the shared empire's, so the same spice was in both
games: the host could collect it again (spice twice), and the planets differed. Not the game's planet loading:
the record kept its 9 items until the next update, and the host's game lost them the same way after its own trip.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
`mods/coop/space_planets.cpp` (Space feature `colonyspice`, MSG_SPACE kinds 80..84): the host sends the stored spice of
each city of the player's colonies (on a change, else every 10 s); a guest writes it into its cities; a guest's
game that takes spice out of a city (the arrival's collection, a pickup) tells the host, which takes the same out
of its city. The game's own pickup update puts or removes the pickups. Not in rivals mode. Switch
`coop.fix space.colony_spice off`.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
* FAIL with SPORE_FIX_OFF=space.colony_spice: out/lab/20261104-103213 (after the guest's arrival collected the spice,
  the host's colony still stored 10.8 and its planet showed 9 pickups, the guest's 0).
* PASS: out/lab/20261104-102216 and out/lab/20261104-103824 (the host's colony 0.9 like the guest's, 0 pickups on both
  planets, the host's hold unchanged).
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

Detector after the fix (out/lab/20261104-104326): no planetary_artifact row in space_play, detect_space, play_space.
Regression: space_trade, space_allies, space_death, space_map out/lab/20261104-104712; space_jump out/lab/20261104-105657
(it failed in 104712 because the feature was first named `planets`, the name of an existing `coop.space` command).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w42-space-planets): fixed (docs/re/space.md#space-planet-life).
- 2026-11-04: released in 0.7.6.
