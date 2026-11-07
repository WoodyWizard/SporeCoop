---
id: BUG-0657
title: Detector: recurring classes with no ticket (Creature species_killed, Civ civ_only and player_cities, Space city_walls and city_territory, projectile nouns, default_ao_e_area)
status: open
kind: desync
stage: All
reported: 2026-11-06
source: lab log sweep 2026-11-06 (703 runs, 2026-11-05/06)
role: 
versions: 0.7.8, rel/0.7.9
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Detector classes that recur in many runs and have no ticket (informational, tests pass): Creature:state:species_killed (12 runs), Civilization:state:civ_only (5) and player_cities (3, about 3 on rel/0.7.9, e.g. out/lab/20261106-105201/play_civ.log:18), Space:noun_only_host:city_walls and city_territory (rel/0.7.8 out/lab/20261105-111032/space_play.log:46), projectile nouns (*_projectile default_tool, resource, cultural, default_beam) and default_ao_e_area (5-20 runs, e.g. rel/0.7.9 out/lab/20261106-105201/play_civ.log:25). Each class: fix it or record why it is expected (transient objects, host-only by design).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened from the orchestrator's lab log sweep; planned for 0.7.10.
