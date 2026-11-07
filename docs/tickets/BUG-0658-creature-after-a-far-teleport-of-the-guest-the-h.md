---
id: BUG-0658
title: Creature: after a far teleport of the guest, the host draws its puppet 12-36 m over the host's ground for a few seconds
status: open
kind: bug
stage: Creature
reported: 2026-11-06
source: lab 20261106-204954 mem_census_tour_guest_10 (fix/w46-addr-space)
role: host
versions: 0.7.9 (rel/0.7.9 a6523625)
fixed_in:
branch:
commits:
tests: mem_census_tour_guest, mem_census_tour_guest_10, mem_census_tour_guest_places
signature:
docs:
related: BUG-0631, BUG-0499
---

## Report
The address space tours of BUG-0631 (coop_mem_census.py --tour guest: the guest's avatar goes 500-1000 m to a new place
of the planet every 20 s with `coop.abilities goto`, which puts it 0.05 m over this game's ground radius there) failed
the log gates in every tour run: the invariant monitor's `drawn` and `ground` rules ("host draws guest 15.9 m from
where guest's own game has it", "partner 1's puppet 15.96 m over the ground") and a sync-check "p: 1 apart (p1 15.7)",
each on 2 consecutive lines. Runs: 20261106-202803 (61 hops: 4 drawn, 6 MISMATCH lines), 20261106-204949 (31 hops: 5 / 6),
20261106-204954 (31 hops, npc.view_centres off: 5 / 6), 20261106-210804 (72 hops over 12 places: 7 / 4). Offsets seen:
11.9, 15.9, 29.0, 35.7 m; at s 50, 110, 230, 370 (a few of the hops, not every one). None in the runs of the same
sessions without teleports (20261106-200638 play bots 20 min, 20261106-211958 60 puppet respawns, 20261106-221554 smoke /
play_creature / play_tribe).

## Root cause
Observed: the drawn offset equals the puppet's height over the HOST's ground at that spot, so the host draws the
puppet where the guest says it is and the host's ground there is 12-36 m lower than the guest's: the two games' ground
heights differ for a few seconds after a jump into an area neither has streamed.
Inferred, not checked: the guest's `goto` takes its ground radius from a low terrain LOD of an area just entered (the
avatar then stands or falls on it in its own game), the host's terrain there is at another LOD. Players do not teleport
500-1000 m; the nearest player case is a respawn at a far nest after a death (long_creature's death transition shows no
such violation). Test artefact or a real two-LOD case: not decided. The tour tests run with the sync-check and monitor
gates off (suite.py, their own teleports).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened (fix/w46-addr-space): seen only in the teleport tours of the address space census; evidence above. For 0.7.10 to judge (a ground-LOD case of far jumps, or the test command's ground radius).
