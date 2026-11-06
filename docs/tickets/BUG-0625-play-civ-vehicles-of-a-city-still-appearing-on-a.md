---
id: BUG-0625
title: play_civ: vehicles of a city still appearing on a slow guest and the last audit right after a capture cinematic count as problems
status: fixed
kind: test
stage: Civ
reported: 2026-11-06
source: lab rel/0.7.9 20261106-105201 play_civ
role: 
versions: 0.7.9 1e155b53
fixed_in: unreleased
branch: fix/w45-int3
commits: f66de16c, 5bb3f4b8
tests: play_civ
signature:
docs:
related: BUG-0319, BUG-0324, BUG-0624
---

## Report
rel/0.7.9 1e155b53 integration run 20261106-105201, play_civ (seed 42422141, lane 3, a loaded PC): `FAIL no problems
(3)`: `11:18:13 vehicles differ: host only [18, 19], guest only []`, then at the last audit `city 1: 1 ruins on the
host, 2 on the guest` and `city 1: 2 buildings on the host, 3 on the guest`.

## Root cause
Observed (both game logs):
* vehicles 18, 19: the host's AI founded a city at 11:18:04 (broadcast); the guest founded it at 11:18:05 ("the host
  founded a city: here founded"). Each game runs its own appearance timer on its own game time: the host's city
  appeared at 11:18:10, the guest's (550 ms hitches) at 11:18:17. The audit at 11:18:13 counted the new city as
  "missing" (its 3-audit grace) but the two vehicles of its nation, which the guest cannot have without the city,
  as a problem at once (1 s recheck). Test inconsistency; the late appearance itself is the game's timer on a slow
  guest (docs/re/civ.md#civ-instant-found).
* city 1: the host captured city 1 at 11:22:51 ("Alice captured a city"), its capture cinematic held both worlds
  ("the world waits for the OK (or 8 s)") until 11:22:59.8 / 59.9, and the last audit ran at 11:23:00, 0.1 s later.
  The guest takes a city's buildings from the host's snapshots (one city per cycle), so the last audit compared a
  state the guest had not received yet. During the session the audits count such differences (ruins_differ=1 and
  buildings_differ=1 from 387 s on, then 2 at the end) without failing. Not seen whether the difference lasted:
  the test ended.

## Fix
Test only (tools/tests/play_civ.py, f66de16c): the vehicles of a nation whose new city is still within its grace
(`game.missing`) are not "vehicles differ" (they become "city N missing" with the city after 3 audits; counter
`vehicles_of_new_city`); the last audit reads a city whose ruins or building count differ once more after 4 s
(counter `final_buildings_rechecked`) and reports what still differs. No limit changed.

## Verification
FAIL (the old audit): lab rel/0.7.9 20261106-105201 play_civ (the three problems above). PASS: lab
20261106-125914 play_civ #1 and #2 (10.5 min each, no problem). The two rechecks are counted (`vehicles_of_new_city`,
`final_buildings_rechecked`) so later runs show how often they apply.

## History
- 2026-11-06: opened.
