---
id: BUG-0329
title: Tribe campaign: all three guests report tribe members 4.0-4.8 off at the same sample (18:21:43)
status: fixed
kind: desync
stage: Tribe
reported: 2026-10-30
source: lab 20261030-175722 campaign#3 (lane 7)
role: guest
versions: main de87f05
fixed_in: 0.7.3.2
branch: fix/w16-tribe, fix/w17-tribe
commits: cd4030a 2d79697
tests: tribe_own_height (coop_tribe_own_height.py), npc_tests (test_own_height)
signature:
docs: docs/re/tribe.md#tribe-ground-height, docs/06-testing.md#testing-tribe-own-height
related: BUG-0305, BUG-0310, BUG-0332
switch: drive.own_height
---

## Report
Final verification lab 20261030-175722 campaign#3 (main de87f05, lane 7, 4 players, run dir
out/lane7/campaign/20261030-175735): from the first sync check of tribe/play (18:21:12) to its end
(18:27:39) every check had kind t (tribe members) with p90 3.9-4.8 m on all three guests at once,
the worst keys varying (t16, t17, t19, t10, t1, t2, t14), and after Carol's rejoin (18:25:31)
5.2-5.9 m; at times also an animal 4.0-4.8 m (n186, n111) on every guest. Two checks in a row made
the desync findings at 18:21:43. The campaign of 10:41 (lab 20261030-104149 campaign#3, BUG-0310's
report) showed the same: kind t 4.1-4.8 m on all three guests at every check from 11:07.

## Root cause
Observed (the probe lines of lab 20261030-210914, campaign#3 lane 7 and campaign#1 lane 2):
* Every offset is radial: the host's member at 509.3-511.0 m from the planet's centre, the guest's
  mirror at 514.6-516.1 m in campaign#3 (radial 4.87-5.31 m, sideways 0.02-0.74 m); in campaign#1 the
  other way (host 516.3, guest 511.4) and the same for animals near the village (host 505.9, guest
  502.0: Dave's n 3.7-4.3). So each game's ground under the village differs in height by ~5 m: the
  guests load the host's save of the Tribe stage, the host made the village live (BUG-0332).
* The guest's mirror was driven at the host's absolute position, 5 m under (or over) its own ground:
  it never arrived (`speed=5.5` on the guest while `host_speed=0.0`: walking in place), and NPC
  proxies were teleported again and again (`tp=14-33 trail=8-18`).
* All guests agree with each other because they all loaded the same save.

## Fix
* `drive.own_height` (cd4030a, drive_plan.h at_own_height, driver.h): in Tribe the target of a
  member's mirror and of an NPC proxy keeps the host's direction from the planet's centre but takes
  the mirror's own distance from it when the two differ by less than 10 m: it stands on its own
  ground where the host's stands. `coop.fix drive.own_height off` = the old driving. Test stand-in
  `coop.tribe lift <m>` (guest).
* The campaign's sync check (2d79697): a t / n kind over the limit whose worst object is in place
  sideways and at the same height over each game's own ground is noted as "ground height differs"
  (BUG-0332), not a desync; the probe lines print each game's ground (`coop.ground`).

## Verification
`coop_tribe_own_height.py` (the guest's copy of the host's members 5 m higher: `coop.tribe lift 5`):
* FAIL with `SPORE_FIX_OFF=drive.own_height`: lab run 20261030-232117-fix-w17-tribe: 37 of 56 samples
  of members standing on the host had the guest's mirror moving; 12-14 teleports per member in 20 s.
* PASS: lab run 20261030-232236-fix-w17-tribe (2 runs): 1 of 55 and 0 of 52 moving, 0 teleports.
* Unit: npc_tests test_own_height.
Not verified: the full campaign with the fix (the terrain difference was not made outside it: a
live Creature -> Tribe on the creature_cadian fixture put the village on flat ground, 503.68 m on
both machines).

## History
- 2026-10-30: opened.
- 2026-10-30 (fix/w17-tribe): root cause from the probe lines (a ground height difference, BUG-0332); mirrors driven at their own height (drive.own_height); the campaign classifies the ground difference.
- 2026-11-01: released in 0.7.3.2.
