---
id: BUG-0319
title: Civ: an NPC city founded late in a play_civ session (and its two vehicles) is missing on the guest for 40-60 s
status: fixed
kind: desync
stage: Civ
reported: 2026-10-30
source: play_civ.py --seed 9, fix/w13-tribe-civ
role: guest
versions: 0.7.3.1 + fix/w13-tribe-civ
fixed_in: 0.7.3.4
branch: fix/w45-placement
commits:
tests: civ_city_burst (coop_civ_city_burst.py), civ_pause_city
signature:
docs:
related: BUG-0120, BUG-0206, BUG-0380, BUG-0331
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
Observed in the report's own log (2026-10-30): cities_in=6 and cities_caught_up=4: the guest's catch-up (BUG-0120)
founded the missing cities itself, one per 6 + 15 s (city_catch_up: 6 s short, then 15 s after any founding), so a
city the guest's replayed founding missed (or a founding it had not received) and its nation's vehicles stayed
missing for 21 s per city: 40-60 s for the last of them, as the report saw. That delay is the class BUG-0380 fixed
on 2026-10-31 (civ.city_burst: every missing city at once, from the expansion's own founded count); the second known
path, a founding while the guest's pause menu is open, waits for the guest's game time by design (BUG-0331).
Not reproduced since in 3 + 5 + 54 play_civ runs (History), nor in this branch's play_civ (out/lab/20261106-060553).

## Fix
No new code: BUG-0380's `civ.city_burst` (civ.cpp city_catch_up) founds every city the guest misses at once and the
vehicles of their nations follow within seconds; the pause path stays BUG-0331's.

## Verification
civ_city_burst (three cities founded without a broadcast, every city and vehicle of the host on the guest within
20 s): FAIL with `SPORE_FIX_OFF=civ.city_burst` out/lab/20261106-065338 ("v: 3 missing on the guest (v5 v6 v7)",
not in 30 s: the report's symptom), PASS out/lab/20261106-065813.

## History
- 2026-10-30: opened.
- 2026-10-30 (fix/w14-tribe-civ): not reproduced in 5 more `play_civ.py --minutes 10` runs on lane 7 (seeds 9, 11, then 9, 11, 5 with the branch): every founding of the host reached the guest and was founded there ("here founded" in the guest's log, the new line), `founds_outside=0` (no founding message arrived while the guest was outside the Civ mode), `cities_caught_up=0`. In the run of the report `cities_in=6` and `cities_caught_up=4`: all six messages were applied, yet the count stayed short 4 times, so the guest's replayed SpawnNextCity did not give a city (inferred: no free site or its materialisation never came) and each catch-up waited 6 + 15 s. Added diagnostics (4f73f10): the guest logs every replayed founding with its result and the city count, and counts foundings received outside the Civ mode. Status kept open until a sighting with these lines.
- 2026-10-30 (fix/w17-tribe): likely the same mechanism as BUG-0331: a city founded while the guest's pause menu is open (play_civ's bots press Esc: `pause` actions) appears there only after the pause (coop_civ_pause_city.py shows it: guest 4 cities while paused, 5 three seconds after Resume); the catch-up's 6 + 15 s then adds delay. Not proven for the original run (its pause times are not logged).
- 2026-11-01 (fix/w34-vote-cinematic, BUG-0404): the mod's busy gate reads the game time's pause flag (cGameTimeManager +0x48 bit 0: the pause menu, the Sporepedia, My Collections alike); `coop.status` prints `busy:` and the campaign's sync and census checks skip a guest whose game stands by it, not only by the "Resume Game" button. Not checked: whether the city planner sets the flag, and play_civ.py's own audit still has no pause check.
- 2026-11-04 (fix/w41-civ-citizens): no sighting of `city N missing on the guest` in the 54 play_civ logs of November (main and worktrees, out/lab/202611*) nor in this branch's play_civ --repeat 2 (see the report). Still open as not reproduced; the diagnostics of 2026-10-30 stay.
- 2026-11-04 (fix/w42-civ-left): not reproduced in 3 more play_civ runs with the detector on lane 9 (out/lab/20261104-100146, 20261104-101322, 20261104-102449): every founding of the host (6 per run) was founded on the guest at once ("here founded"), `cities_caught_up=0`, no "missing on the guest" line, and no Civ city or vehicle row of the detector for a new city. Still open as not reproduced; the only known delay is a guest's pause menu (BUG-0331, civ_pause_city), where the founding waits for the guest's game time by design.
- 2026-11-06 (fix/w45-placement): closed as fixed by BUG-0380 (civ.city_burst, the catch-up delay of the report's log); FAIL / PASS runs above. fixed_in is BUG-0380's release (0.7.3.4).
