---
id: BUG-0319
title: Civ: an NPC city founded late in a play_civ session (and its two vehicles) is missing on the guest for 40-60 s
status: open
kind: desync
stage: Civ
reported: 2026-10-30
source: play_civ.py --seed 9, fix/w13-tribe-civ
role: guest
versions: 0.7.3.1 + fix/w13-tribe-civ
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0120, BUG-0206
---

## Report
`SPORE_LANE=7 python3 tools/tests/play_civ.py --minutes 10 --seed 9` (fix/w13-tribe-civ, 2026-10-30,
the third seed 9 run of the day; log out/w13/play_civ9_fix2.log of the worktree, not in git): from
14:21:12 to 14:22:09 (the last minute of the session) every audit listed "vehicles differ: host
only [18, 19]" and from 14:21:31 "city 6 missing on the guest" (6 times each): the RESULT was FAIL
(12 problems). At the end the guest's `coop.civ` had cities=10 and mirrors=22, the host's the same
(cities_caught_up=4 on the guest), so it converged after the session. The vehicle sync of the
same run was good (p90 0.64 m, max 9.86 m). Not seen in the two other seed 9 runs that day.

## Root cause
Not analysed. Candidates (inferred): the guest's city founding waits (BUG-0120's catch-up,
`cities_caught_up`) longer than the audit's 1.5 s second look; or the host's SpawnNextCity message
came while the guest was in the city planner (the bots open it).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
