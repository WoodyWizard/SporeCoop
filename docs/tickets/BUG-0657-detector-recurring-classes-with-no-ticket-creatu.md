---
id: BUG-0657
title: Detector: recurring classes with no ticket (Creature species_killed, Civ civ_only and player_cities, Space city_walls and city_territory, projectile nouns, default_ao_e_area)
status: fixed
kind: desync
stage: All
reported: 2026-11-06
source: lab log sweep 2026-11-06 (703 runs, 2026-11-05/06)
role: 
versions: 0.7.8, rel/0.7.9
fixed_in: 0.7.10
branch: fix/w46-s1-sweep
commits: 1081cd4f
tests: unit (tools_selftest.py worlddiff)
signature:
docs: docs/06-testing.md#testing-log-signals
related: BUG-0477, BUG-0530, BUG-0596, BUG-0677
---

## Report
Detector classes that recur in many runs and have no ticket (informational, tests pass): Creature:state:species_killed (12 runs), Civilization:state:civ_only (5) and player_cities (3, about 3 on rel/0.7.9, e.g. out/lab/20261106-105201/play_civ.log:18), Space:noun_only_host:city_walls and city_territory (rel/0.7.8 out/lab/20261105-111032/space_play.log:46), projectile nouns (*_projectile default_tool, resource, cultural, default_beam) and default_ao_e_area (5-20 runs, e.g. rel/0.7.9 out/lab/20261106-105201/play_civ.log:25). Each class: fix it or record why it is expected (transient objects, host-only by design).

## Root cause
Each class, with how often the next sample of the same run still had it (persist), lab runs 2026-11-06/07:
* Civ projectiles and AoE (default_tool / resource / cultural _projectile, default_ao_e_area; 82, 63, 57, 17, 10, 9, 7, 6 runs;
  persist 0-25 %, only in fights): the host's shots are relayed in 0.2 s batches (civ.shots, BUG-0477 / BUG-0530) and live under
  2 s: by design, KNOWN already maps them to BUG-0477 / BUG-0530.
* Civilization:state:civ_only (12, persist 0 of 11) and player_cities (4, 0 of 4): each sample at a capture ("city 1 captured") or a
  new city (a village becoming a city on the host first): transient, gone in the next sample.
* Creature:state:species_killed (8, 0 of 7): a kill the guest's game counts a moment later (the guest's first dump only).
* Space:noun_only_host:city_walls / city_territory (1 run, rel/0.7.8 space_play 20261105-111032, with 9 cities, 40 vehicles, 178
  herds, 77 ornaments in the host's game only): the host on a planet's surface, its guest in orbit: level of detail.
* Space default_beam_projectile in the host's game only (16 + 12 runs, persist 11 of 14 / 8 of 11, never on a guest): real, Space
  shots are not relayed: new ticket BUG-0677 (open).
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
Test tool (tools/tests/worlddiff.py): species_relation / impressed / killed are compared with the guest's second dump too (as
species_only, BUG-0596); in Space a planet's surface nouns (SPACE_PLANET_LOD) are not compared in the whole list (the area compare
still runs). tools/detector_report.py KNOWN: Space / Tribe default_beam_projectile -> BUG-0677, civ_only and player_cities ->
BUG-0657 (transient), species_killed -> BUG-0657 (bracketed).
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: test-tool change. tools_selftest.py: FAIL against the old worlddiff (SELFTEST_WORLDDIFF=<old copy>: "a kill on its way
to the guest: no species_killed row" and "Space: a turret of a planet ... level of detail" fail), PASS with the new one (13 checks,
0 failures). New ticket: BUG-0677 (Space beams).
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened from the orchestrator's lab log sweep; planned for 0.7.10.
- 2026-11-07 (fix/w46-s1-sweep): fixed; see Root cause / Verification.
- 2026-11-07: released in 0.7.10.
