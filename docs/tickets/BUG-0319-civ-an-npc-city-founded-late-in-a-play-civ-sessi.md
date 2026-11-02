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
- 2026-10-30 (fix/w14-tribe-civ): not reproduced in 5 more `play_civ.py --minutes 10` runs on lane 7 (seeds 9, 11, then 9, 11, 5 with the branch): every founding of the host reached the guest and was founded there ("here founded" in the guest's log, the new line), `founds_outside=0` (no founding message arrived while the guest was outside the Civ mode), `cities_caught_up=0`. In the run of the report `cities_in=6` and `cities_caught_up=4`: all six messages were applied, yet the count stayed short 4 times, so the guest's replayed SpawnNextCity did not give a city (inferred: no free site or its materialisation never came) and each catch-up waited 6 + 15 s. Added diagnostics (4f73f10): the guest logs every replayed founding with its result and the city count, and counts foundings received outside the Civ mode. Status kept open until a sighting with these lines.
- 2026-10-30 (fix/w17-tribe): likely the same mechanism as BUG-0331: a city founded while the guest's pause menu is open (play_civ's bots press Esc: `pause` actions) appears there only after the pause (coop_civ_pause_city.py shows it: guest 4 cities while paused, 5 three seconds after Resume); the catch-up's 6 + 15 s then adds delay. Not proven for the original run (its pause times are not logged).
- 2026-11-01 (fix/w34-vote-cinematic, BUG-0404): the mod's busy gate reads the game time's pause flag (cGameTimeManager +0x48 bit 0: the pause menu, the Sporepedia, My Collections alike); `coop.status` prints `busy:` and the campaign's sync and census checks skip a guest whose game stands by it, not only by the "Resume Game" button. Not checked: whether the city planner sets the flag, and play_civ.py's own audit still has no pause check.
