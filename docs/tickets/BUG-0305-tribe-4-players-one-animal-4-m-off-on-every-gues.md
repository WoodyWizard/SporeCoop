---
id: BUG-0305
title: tribe, 4 players: one animal ~4 m off on every guest at once (campaign desync n 3.7-4.4), not a regression of the view centres
status: open
kind: desync
stage: tribe
reported: 2026-10-30
source: lab 20261030-075052 campaign#3
role: 
versions: 0.7.3.1
fixed_in:
branch: fix/w11-finale
commits:
tests: campaign, coop_sync_multi --fixture tribe
signature:
docs: docs/known-issues.md#issue-tribe-npc-4m
related: BUG-0281, BUG-0275, BUG-0280, BUG-0291
---

## Report
Lab 20261030-075052 campaign#3 (main 390f73d, lane 7, 4 players), Tribe play: `FINDING [desync]
Bob / Carol / Dave: objects of kind n out of sync in tribe (3.7 / 4.4 / 4.0)`. The sync lines show
the same host animal n212 off by 3.4-4.4 m on all three guests at once, at 08:19:00 and 08:19:31
(campaign clock), again on Dave at 08:21:49 (4.4 m); 7-14 animals per check, so two objects over
1.5 m make a finding (coop_multi.over_limit). Fps 64-68 on every game. The other two campaigns of
the run (lanes 2 and 4) passed; the earlier tribe sync findings in the lab history were kind t
(20261029-174717 campaign#2).

## Root cause
Not found. Question of the brief: a regression of the view-centre work merged today
(npc.view_centres_tribe BUG-0280, npc.rts_views BUG-0291)? **No difference measured** (fix/w11-finale,
`coop_sync_multi.py --fixture tribe --rounds 30`, 4 players, play_tribe bots on every machine, lanes 2
and 3 at the same time, 21-43 fps):

| run | n checks | animals per check | animals > 1.5 m | checks over the limit (Bob / Carol / Dave) | largest n p90 |
|---|---|---|---|---|---|
| fixes on (lane 2) | 79 | 9.8 | 20 (2.6 %) | 1 / 0 / 0 | 3.38 m |
| both off (lane 3, SPORE_FIX_OFF=npc.rts_views,npc.view_centres_tribe) | 79 | 11.0 | 19 (2.2 %) | 0 / 0 / 0 | 4.89 m |

With both switches off one animal was again 4-5 m off on two guests in the same round (n14: Carol 4.9,
Dave 4.0), the campaign's pattern. So it is older than today's merge; it looks like BUG-0281 (Creature:
an NPC proxy stays ~4 m from where the host has it, on every guest at once), here in the Tribe stage.
The game logs of the campaign have nothing about n212 (no per-NPC log). Inferred, not verified: the
proxies do not reach the pose they are driven to (a fight: the campaign logged "pincer attack" at that
time), or the host's animal is not where its sent state says.

## Fix
None. Measurement tool: `coop_sync_multi.py --fixture tribe` (e4bdf7f).

## Verification
Measurements: fix/w11-finale out/w11/tribe_on.{log,jsonl}, out/w11/tribe_off.{log,jsonl} (worktree, not in git).
Next: `coop.npc list` on every guest while an animal is off (err= of the proxy, the host's speed), as
BUG-0281's reproduction; a per-object speed in measure()'s jsonl.

## History
- 2026-10-30: opened; A/B with the view-centre switches: no regression, open (fix/w11-finale).
- 2026-10-30: fix/w12-cell-fault: lab 20261030-104149 campaign#3 (main 813f8cd, overloaded machine, host fps 17-27): kind t (tribe members) 4.1-4.8 m off on all three guests at every check from 11:07 for 7 minutes (n stayed under 2 m mostly); recorded in BUG-0310's report.
