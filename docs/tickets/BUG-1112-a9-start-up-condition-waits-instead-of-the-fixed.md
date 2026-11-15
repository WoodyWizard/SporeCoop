---
id: BUG-1112
title: A9: start-up: condition waits instead of the fixed settle sleeps of cold start and warm reset
status: fixed
kind: tooling
stage: lab
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-labstart
commits: ee9d9990 d1fe9727 ff5df396 7fdc676c 6c35515c
tests: labstart_selftest.py (startwait); team, select, frames_loop (lab A/B)
signature:
docs: docs/06-testing.md#testing-start-waits
related: BUG-1113 BUG-1114
---

## Report
Plan 0.7.15 A9 (docs/audit-0.7.15/tests.md TST-6): the cold start of twoinst ended with a fixed `time.sleep(4)`, the
warm reset with `time.sleep(3)`; more fixed sleeps sat in the start path (2 s after the kill, 2 s between the
launches, 4 s after the host is in, 1-2 s polls, `wait.frames 200` before the join, 5 s between the lab's lane starts,
`sleep 1` in spore.sh kill). Start and reset were 17.5 % of all lab time.

## Root cause
Fixed waits where a signal exists: the lane's processes, the guest's game process, the frame counter, the game
time (`coop.pauses`), the frames the guest's menu has drawn.

## Fix
tools/tests/startwait.py (until / settle / MenuFrames) used by twoinst.start, warm_reset and coop_multi.start_extra;
`spore.sh procs` (every process kill would end), a polled TERM in `spore.sh kill`, lab.py waits for the lane's first
game process between lane starts. Every wait is bounded by its old value. Twin switch: `SPORE_START_FIXED=1` (the old
sleeps). The start lines print the settle (`settle 1.4 s` / `settle at the 4 s bound`).

## Verification
no-fail-run: a lab speed change (no bug to fail); measured instead. A/B at equal load (old code from a detached
rel/0.7.15 checkout beside the new code, lanes 4/5, then swapped; team,select,frames_loop): old labs
20261115-012709, -014126, -015204, -020417; new 20261115-012729, -014106, -015224, -020357. Cold start median
39.5 -> 25.5 s (n 28 each; the three fixed steps 12 -> 4 s), warm reset 20 -> 17 s (n 14 each), the four runs 2831 ->
2488 s; results 41 OK + 1 FAIL on each side (frames_loop's frame count at load 34-40, old and new alike). Twin
SPORE_START_FIXED=1: lab 20261115-022027 (team x3 cold: 29-32 s, every settle at its bound). Stage tests on the new
code: labs 20261115-022047 (lane 5) and 20261115-022517 (lane 4). Unit: tools/tests/labstart_selftest.py.

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-labstart.
