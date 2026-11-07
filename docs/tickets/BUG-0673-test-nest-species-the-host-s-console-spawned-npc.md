---
id: BUG-0673
title: Test nest_species: the host's console-spawned NPC of its own creation is not in its NPC list
status: open
kind: test
stage: Creature
reported: 2026-11-07
source: regression run of fix/w46-tribe-civ, 2026-11-07
role: 
versions: 0.7.10
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0017
---

## Report
nest_species fails at its third check on lanes 4 and 5, 4 of 4 runs on fix/w46-tribe-civ (5bd355ed, on rel/0.7.10 94066205): `coop.npc spawn 290229B9` on the host answers `spawned <ptr>` and the host's log counts one NPC more (npcs simulated 8 -> 9), but `coop.npc list` on the host has no entry of species 290229B9 within 10 s ("the host mirrors it (None)"). Runs: out/lab/20261107-094633 (lane 5), out/lab/20261107-100325 (2 runs, lane 4), and out/lab/20261107-100618 with every switch of fix/w46-tribe-civ off (creations.png, creations.host_designs, wsync.tribe_level, wsync.tribe_color): FAIL too, so not caused by that branch. Last passing runs in flaky.json are from before 0.7.9.

## Root cause
Not analysed. Next: run on rel/0.7.10 and on v0.7.9, read `coop.npc list` on the host right after the spawn.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
