---
id: BUG-0305
title: tribe, 4 players: one animal ~4 m off on every guest at once (campaign desync n 3.7-4.4), not a regression of the view centres
status: fixed
kind: desync
stage: tribe
reported: 2026-10-30
source: lab 20261030-075052 campaign#3
role: 
versions: 0.7.3.1
fixed_in: 0.7.3.2
branch: fix/w12-tribe-npc
commits: 8623721, 07c3c07
tests: tribe_npc_dash (coop_tribe_npc_dash.py), coop_sync_multi --fixture tribe, campaign
signature:
docs: docs/re/tribe.md#tribe-animal-proxy-trail, docs/known-issues.md#issue-tribe-npc-4m
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
First look (fix/w11-finale): question of the brief: a regression of the view-centre work merged today
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

Found (2026-10-30, fix/w12-tribe-npc, docs/re/tribe.md#tribe-animal-proxy-trail). Observed with
`coop.npc list` on all four machines for every animal over 1.5 m off (coop_sync_multi probe, lanes 5
and 6, ~250 checks):
* the host's game updates the animal every frame (`upd=64/64`) and the guests get its states;
* every such animal was running or had just stopped (host `velfr` 16-64 of 64 frames, 4-8 m in the
  last 64 frames), often a wounded one in short dashes with a herd mate 0.3-3 m beside it;
* the guest's proxy was 2-4 m from its own interpolation target (`err=`) and walked at exactly the
  animal's speed (`own=` = `spd=`): the game's locomotion runs no faster than the creature's top
  speed, so a proxy thrown off its path never closed in until the animal stopped (0.5-1.5 s later);
* the host's herd mates stood 0.3-2 m apart while their proxies stood 0.7-3.4 m apart on guests:
  on a guest the proxies push each other off the path, the host's animals overlap.
Reproduced on two games: two 06808ACA animals forced 1.2 m apart on a zigzag (`coop.npc dash`): p90
1.5-2.8 m off; one animal alone followed within 0.5-0.9 m. Inferred, not looked up: which game code
separates the guest's puppets (collision or the locomotion's avoidance).

## Fix
`npc.trail` (drive_plan.h, driver.h, npc.cpp): an NPC proxy more than 1.5 m off its target for
0.5 s without getting 0.3 m closer is teleported onto it and held there for 1 s (the per-frame
nudge pulls all of the way instead of 15 %), then 1 s before the next such teleport. The teleport
alone left 20 % of the samples over 1.5 m (the mate pushed it off again at once).
switch: `coop.fix npc.trail off` (the driving before; BUG-0169's 3 s stall rule stays).
Diagnostics: `coop.npc list` prints the host's speed and update / moving frames, the guest's states
per second, own speed, teleports; it is no longer cut at 8 KB. Measurement tool:
`coop_sync_multi.py --fixture tribe` (e4bdf7f) with the probe.

## Verification
Measurements: fix/w11-finale out/w11/tribe_on.{log,jsonl}, out/w11/tribe_off.{log,jsonl} (worktree, not in git).
Test `tribe_npc_dash` (coop_tribe_npc_dash.py, p90 of the guest's error <= 1.2 m):
FAIL with `SPORE_FIX_OFF=npc.trail`: out/lab/20261030-115327 (p90 1.87 m), out/lab/20261030-114616 (2.03 m);
PASS with the fix: out/lab/20261030-115006 (p90 0.84 m), out/lab/20261030-115137 (0.94 m), out/lab/20261030-115335 (1.09 m).
One fix-off run passed narrowly (out/lab/20261030-114958, p90 1.47 m against the earlier 1.5 m share rule).
4 players, real hunts (coop_sync_multi --fixture tribe --rounds 40, lanes 5 / 6 at the same time,
68 fps): animals over 1.5 m 2 of 1335 (0.1 %) with the fix, 11 of 1144 (1.0 %) without; none over
3 m in either (the machine was less loaded than in the probes, 9-55 fps, where 2-6 m were seen).
Other tests with the fix: out/lab/20261030-120130 (lanes 5+6) tribe_coop, tribe_far, chaos_tribe, tribe_play,
npc_far, creature, creature_kills, creature_death, npc_pooled, tribe_npc_dash: 10/10 OK.

## History
- 2026-10-30: opened; A/B with the view-centre switches: no regression, open (fix/w11-finale).
- 2026-10-30: root cause found with the `coop.npc list` probe (proxies of a running herd pushed off the path), fixed by `npc.trail`, test tribe_npc_dash (fix/w12-tribe-npc).
- 2026-10-30: fix/w12-cell-fault: lab 20261030-104149 campaign#3 (main 813f8cd, overloaded machine, host fps 17-27): kind t (tribe members) 4.1-4.8 m off on all three guests at every check from 11:07 for 7 minutes (n stayed under 2 m mostly); recorded in BUG-0310's report.
- 2026-11-01: released in 0.7.3.2.
