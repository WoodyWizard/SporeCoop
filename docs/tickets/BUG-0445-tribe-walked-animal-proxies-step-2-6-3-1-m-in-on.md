---
id: BUG-0445
title: Tribe: walked animal proxies step 2.6-3.1 m in one frame on the guest's screen (npc_walk_tribe fails 4 of 6 on lane 7)
status: open
kind: desync
stage: Tribe
reported: 2026-11-01
source: lab 20261101-180236, 20261101-183139, 20261101-184014 npc_walk_tribe, lane 7 (fix/w35-monitor-gates)
role: guest
versions: main 6ac351e
fixed_in:
branch:
commits:
tests: npc_walk_tribe (tools/tests/coop_npc_walk.py)
signature:
docs:
related: BUG-0361
---

## Report
`npc_walk_tribe` (TRACK_FIXTURE=tribe coop_npc_walk.py, cold and warm, lane 7, clean network) failed 4 of 6 runs on
its own check "no jump of a moving proxy (Tribe: at most 3 under 2.5 m)": out/lab/20261101-180236 (one step of
3.11 m in a 16 ms frame on the guest's screen), 20261101-183139 #1 (2.69 m in a 41 ms frame, 2.96 m in 14 ms),
20261101-184014 #1 (5 steps 2.56-3.06 m in 15-16 ms frames; at s 38.78 three walked proxies, #29, #30, #44, stepped
2.6-2.9 m in the same frame), #3 (no jump; "stand in 6 % <= 5 %"). 20261101-184014 ran main's own coop_npc_walk.py
(this branch's test change does not touch Tribe: the Tribe host lists no family), the build is main's mod plus a
trace field. Passing runs had 0 jumps (20261101-183139 #2, 20261101-184014 #2; 20261101-153010 on
fix/w33-proxy-ground). Earlier passes after BUG-0361: 20261101-085045 (3/3), 094111, 125335 (fix/w30-npc-walk).

How to reproduce: `tools/lab.py run npc_walk_tribe --repeat 3 --lane N`; the test prints `jumps on the guest's
screen (id, t, m, frame ms)`; the per-frame rows are in out/laneN/track/npc_walk_tribe_*.json.

## Root cause
Not analysed (outside fix/w35-monitor-gates's task). Several proxies stepping in the same frame suggests a late
or bunched state of the host (the test also counts "windows left out because the host's path itself jumped",
148 in 20261101-180236), which the Tribe walk driving (npc.walk_tribe) does not smooth.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened (fix/w35-monitor-gates, seen in its regression runs).
