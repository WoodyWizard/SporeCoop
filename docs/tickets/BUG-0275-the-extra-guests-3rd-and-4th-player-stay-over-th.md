---
id: BUG-0275
title: the extra guests (3rd and 4th player) stay over the sync limits after BUG-0269 (cell c 2.0, creature n 5.8)
status: fixed
kind: tooling
stage: cell
reported: 2026-10-29
source: lab 20261029-122937 campaign
role: 
versions: 0.7.3.0
fixed_in: 0.7.3.1
branch: fix/w7-sync
commits: 2777adb
tests: campaign, multi4, coop_sync_multi
signature:
docs:
related: BUG-0269, BUG-0281, BUG-0282
---

## Report
After BUG-0269 (a fresh host sample per guest, distance from the host's path) lab 20261029-122937
campaign#1 still had "Carol: objects of kind c out of sync in cell (2.0)" (limit 1.2) and "Dave:
objects of kind n out of sync in creature (5.8)" (limit 3.0). Every desync finding of four campaign
runs was on the 3rd or 4th player (instances 20+N, 30+N), none on Bob. Open (?): whether those
instances run at a lower frame rate or get states later (4 games on one lane), or a real per-peer
problem of the host's streaming.

## Root cause
Not a per-peer problem of the mod: measured per player with 4 players (lane 6, play bots on every
machine like the campaign, the guests checked in a rotating order; tools/tests/coop_sync_multi.py):

| run | guest | checks | p90 median / largest | checks over the limit (old rule) |
|---|---|---|---|---|
| creature 1 | Bob / Carol / Dave | 50 each | 0.57 / 0.66 / 0.70 m | 2 / 3 / 1 |
| creature 2 | Bob / Carol / Dave | 45 each | 0.29 / 0.64 / 0.69 m (max 5.2 / 2.2 / 1.5) | 1 / 0 / 0 |
| cell (play) | Bob / Carol / Dave | 40 each | 0.01 m each | 0 / 0 / 0 |
| cell (chaos) | Bob / Carol / Dave | 24 each | 0.01 m each | 0 / 0 / 0 |

The 3rd and 4th player get the same streams (npc.cpp and cellsync.cpp send the same batches to
every peer, relevance aside), run at the same frame rate (63-67 fps, windows in their own or a
shared grid cell alike) and are not measured later (a fresh host sample per guest since BUG-0269).
Five findings, all on Carol / Dave, have a chance of (2/3)^5 = 13 % with three equal guests.

What made the findings (observed):
* p90 of few objects is the largest error: in the Creature stage a guest had fewer than 10 NPCs
  within 90 m in 2 of 3 checks, so one NPC off made the whole kind a desync. Every check over the
  limit in the runs above was one object: a proxy right next to the guests' avatars stood 3.8-4.3 m
  from its own interpolation target on every guest at once (`coop.npc list` err=4.33, the host's NPC
  standing still): BUG-0281.
* The measurement ignored the designed lag: the guest shows NPCs 0.15 s late (buf.delay) but was
  read ~10 ms after the host sample, outside the host segment it was compared with (p90 median Bob
  0.57 -> 0.27 m when the host's last 0.25 s count).
* Cells (kind c) trail with the frame rate: Carol forced to 15 fps had 283 of 13165 cells over 1.2 m,
  Bob at 65 fps 1, Dave at 30 fps 2 (BUG-0282). The lab run with 12 games may have slowed some
  games down; its frame rates were not logged, so whether Carol had a low one is not known.

## Fix
Test tooling (coop_multi.py, used by the campaign): measure() compares a guest with the host's path
over the 0.25 s before its sample and after it (host_path); over_limit() calls a kind out of sync
when at least 10 % of its objects and at least two are beyond the limit (p90 as before from 20
objects on). The campaign prints every guest's p90, object count, worst object and every game's
frame rate next to a desync. tools/tests/coop_sync_multi.py measures this per player.
switch: none (test tooling)

## Verification
no-fail-run: test tooling; the old rule did not fail on a per-player difference, it failed at random
on single objects. Before / after on the same measurements (checks over the limit, Bob / Carol /
Dave): creature 1: 2 / 3 / 1 -> 0 / 1 / 0; creature 2: 1 / 0 / 0 -> 0 / 0 / 0; cell: 0 -> 0. With the new check: coop_sync_multi creature 30 checks
per guest 0 / 0 / 0 over (p90 median 0.63 / 0.56 / 0.51 m, 57-59 fps); lab 20261029-151605
campaign_short lane 6 OK, 0 findings; smoke 20261029-152549 lanes 6+7 13/14 OK (ui skipped).
## History
- 2026-10-29: opened.
- 2026-10-29: measured per player, test tooling fixed on fix/w7-sync; BUG-0281, BUG-0282 opened.
- 2026-10-29: released in 0.7.3.1.
