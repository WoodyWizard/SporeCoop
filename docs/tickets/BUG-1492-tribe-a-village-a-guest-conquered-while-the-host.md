---
id: BUG-1492
title: Tribe: a village a guest conquered while the host was away does not count for the stage goal (TR-5)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w79-tribe
commits: 70979dc4
tests: tribe_away_goal, tribe_away_goal_off
signature:
docs: docs/re/tribe.md#tribe-audit-wp-b1
switch: tribe.away_goal
related: BUG-1238
---

## Report
External static audit of 0.7.15.5, item TR-5 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-cr-tr.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-B1 (fix/w79-tribe). A behaviour change gets a switch and a twin.

## Root cause
* Code (rel/0.7.17 4daf12d1, inferred, not run): tribe.cpp conquer_on_host (BUG-0879's away conquest, MSG_TRIBE_CMD
  CMD_CONQUER held by the host until it is back) felled the village's hut with `fell_hut_by` and nothing else. The goal
  bar counts a village when its cTribeToTribeMission ends (TribeToTribeMission_Update 0xC2F750 reads the conquered
  flag +0x556); that mission is made only by the host's own input (DoActionGeneric -> SetTargetTribe 0xCD3D70) or, for a
  guest's raid order run on the host, by BUG-1068's `wsync::goal_target` (tribe_host.cpp host_command). An away raid
  runs in the guest's own game, so the host never had a mission for that village: a save's village fell and counted
  nothing (BUG-1068 root cause, lab 20261113-072615). BUG-0879's test checks the flag and the hut, not the goal.
* Part 2 (REAL-THEORETICAL in the audit): busy_model's held commands drain all at once; a fall refused because a
  cinematic plays (-1) was counted refused and lost.

## Fix
tribe.cpp: conquer_on_host first makes the village's mission (`wsync::goal_target(best)`: SetTargetTribe outside the
frame, as BUG-1068 does at a guest's order) and queues the conquest; `away_goal_tick` (host, in Tribe) fells the hut
0.5 s later, when the mission exists (its update ends it from the conquered flag). Part 2 on the way: a fall refused
for a playing cinematic is retried every second for 30 s, then counted refused. The queue is cleared with the world and
the session. Counters in `coop.tribe` (`tribe audit 0.7.17: away_goal missions= pending= retries=`). Switch
`coop.fix tribe.away_goal off`: the fall at once, no mission, a cinematic drops it (0.7.16.1).

## Verification
open, waiting for the lab run. pending night run: tribe_away_goal, tribe_away_goal_off.
`coop_tribe_away_conquest.py --goal` (suite `tribe_away_goal`, tribe, warm): BUG-0879's conquest while the host is in
the main menu, then the host's goal progress (cPlayer +0x10F0, `coop.wsync dump tribe_goal`) one village above the
save's within 30 s of its conquest. Twin `tribe_away_goal_off` (SPORE_FIX_OFF=tribe.away_goal): the progress stays,
must FAIL.
0.7.17 release lab: PASS: tribe_away_goal (lab 20261124-112022); FAIL (fix off): tribe_away_goal (lab 20261124-020506).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-B1), branch fix/w79-tribe.
- 2026-11-23: fix on fix/w79-tribe (switch tribe.away_goal, tests tribe_away_goal, tribe_away_goal_off); lab run pending (no lab by day).
- 2026-11-24: released in 0.7.17.
