---
id: BUG-0329
title: Tribe campaign: all three guests report tribe members 4.0-4.8 off at the same sample (18:21:43)
status: open
kind: desync
stage: Tribe
reported: 2026-10-30
source: lab 20261030-175722 campaign#3 (lane 7)
role: guest
versions: main de87f05
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0305, BUG-0310
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
Not found. What the evidence says:
* Not one sample or a host jump: 13 checks over 6.5 minutes, many members, two campaigns.
* The guests agree with each other (the same 4.0-4.8 m at the same check), so the difference is
  between the host's members and what every guest makes of the same states (inferred).
* The host's state and its sync sample read the same `game::position` (tribe.cpp host_tick,
  sync_entries); the guests drive members with drive::step (snap 8 m, no stall / trail rule).
* Not reproduced (fix/w16-tribe, lane 7): the tribe fixture with 2 players and `coop.tribe spawn 12`
  (7 min of play_tribe bots): t p90 0.35-1.0 m; 4 players (`coop_sync_multi.py --fixture tribe
  --spawn 12 --rounds 45`, the campaign's setup): t p90 median 0.65-0.69 m, largest 1.48 m on all
  guests; the campaign_tribe replay (seed 612535391): t p90 0.7 m. The difference to campaign#3 is its
  world (the tribe evolved from that campaign's own creature after Cell and Creature).
* The only t offsets over 2.5 m seen: a knocked-out member (hp 0, dead=1) lying 2.7-3.3 m from the
  host's on a guest (its death animation moved it, speed 28-30), a smaller separate effect.
The [info] "tribe -> civ did not finish in 20 min (the raids did not conquer every tribe)" of the same
run: the test's own limit (coop_campaign.py reports a Tribe end that does not come as info on
purpose; kick_tribe only knocks members out with `coop.tribe pincer`, a tribe is conquered when the
raids destroy its hut, and one tribe stayed for 20 min). Not a co-op bug as far as seen.

## Fix
Not fixed. Diagnostics (fix/w16-tribe, 94b168a): when a kind is out of sync the campaign's note now
holds the worst object's `coop.tribe` / `coop.npc list` line on the host and on that guest (the
guest's line has how its mirror is driven); `coop_sync_multi.py --spawn N` and its member probe.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
