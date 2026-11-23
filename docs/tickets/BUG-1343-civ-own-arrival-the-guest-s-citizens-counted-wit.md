---
id: BUG-1343
title: civ_own_arrival: the guest's citizens counted with its camera away from its city, and no NPC city of the guest's vehicle specialty to buy in
status: fixed
kind: test
stage: Civ
reported: 2026-11-20
source: lab
role: 
versions: rel/0.7.16
fixed_in: 0.7.16
branch: fix/w60-tribe
commits: a73bda43
tests: civ_own_arrival
signature:
docs:
related: BUG-1276, BUG-1323, BUG-1273, BUG-1274, BUG-0982
---

## Report
The 0.7.16 final check (lab 20261120-041304, lane 2): civ_own_arrival "FAIL the guest's city's citizens are of the
guest's species on both games (7CBC3253: {'host': {'7CBC3253': 19}, 'guest': {}})" and "FAIL the guest's purchase
made a vehicle on the host (None)". `lab.py flaky`: 8 of 11 runs failed since 2026-11-19 (earlier ones on other steps:
BUG-1306, BUG-1323, a fix/w58-cityname editor step); the PASS runs 20261119-153243, -200005, -210144, -214823.

## Root cause
Both in the test, not the mod.
* Citizens: ✔ (log + key frame) a game makes the citizens of one city only, the one its camera is near (the citizen
  manager's city, mgr +0xAF0, docs/re/civ.md#civ-own-species). `frame_city` pans until the city's point is anywhere in
  a 920 x 580 box: on the guest the bought city 1 stood at the horizon (key frame 05_guest_city_guest, screen point
  (768, 207)), so the guest's game had no citizen of city 1 at all for 31 s; the host's camera was close and its 19
  citizens were the guest's species. In every PASS run both games had 19 at 0 s. The mod's answer per city (WP-C1) was
  right where citizens existed.
* The purchase: ✔ (log) a vehicle bought in a city is of the city's specialty (+0x540); the guest's vehicle editor at its
  arrival is the end-of-Tribe one, the capital's specialty (military land F670AA43 here). The NPC cities' specialties
  are random per run: in 20261120-041304, 20261119-193856 and 20261119-130049 no NPC city was military, the test fell
  back to the nearest (religious: its specialty box answered "Religious", land design 9AD7D4AA, the guest has no own
  design for it), and the only F670AA43 city was the capital, the host's: "this city belongs to player 0". In the PASS
  runs the chosen city was military (land design F670AA43 before and after the purchase).

## Fix
Test only (coop_civ_own_arrival.py), the checks unchanged:
* `camera_on_city`: before the citizens are counted, each game's camera is panned until the city is in the middle of
  the screen and that game's citizen manager runs the city (`coop.civ citizens` line `mgr city=`), up to 3 pans, 8 s
  each; the managers' cities are printed.
* `give_specialty`: when no NPC city has the specialty whose land design the guest's vehicle editor made, the nearest
  one gets it on both games with the game's own setter (`call SporeCoop::cCity_SetSpecialty thiscall <city> <spec> 1`)
  before the guest buys it, checked (`coop.civ owndesign did`). This is step 5's fixture (a city where the guest's design
  applies); its check (the guest's new vehicle shows the guest's own design on both games) is unchanged.

## Verification
no-fail-run: test fixes (the FAILs: lab 20261120-041304, 20261119-193856, 20261119-130049).
Was waiting for the lab run: civ_own_arrival must PASS (print `the citizen managers' city (want N): {'host': N,
'guest': N}`, and in a run without a military NPC city `city N given specialty 0`).
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): civ_own_arrival PASS in 20261120-133703, 20261121-003202, 20261122-194210, 20261123-023643. Closed.

## History
- 2026-11-20: opened.
- 2026-11-20: cause and fix on fix/w60-tribe (no lab today, owner rule); lab run pending.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
