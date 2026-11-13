---
id: BUG-1070
title: Tests: same_view_tribe's host game gone 7 s into Tribe (console broken pipe, no crash report, fixture check failed)
status: fixed
kind: tooling
stage: Tribe
reported: 2026-11-13
source: fix/w49-mirror final check, lab 20261113-052132 lane 7
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-tribe3
commits: 30dd6a2f 9051b80c
tests: tools/tests/lab_selftest.py lane_lock (make test); same_view_tribe x6 lane 7, play_tribe --quick x3 lane 6
signature:
docs: docs/06-testing.md#testing-lane-lock
related:
---

## Report
fix/w49-mirror final check, lane 7 (out/lab/20261113-052132): same_view_tribe started cold, the guest joined, the host's
game log ends 6.7 s into Tribe ("Bob appeared in the world"), no fault line, no crash report in the lane; the test's
fixture check found nothing ("the player tribe has members, another tribe exists" both missing) and its next console
call broke (BrokenPipeError). The same test passed on lane 6 in the same final check (20261113-051820-2) and on both lanes in the reruns (20261113-054834, 20261113-054834-2).
Seen once. Reproduce: `lab.py run same_view_tribe --lane 7 --repeat 5`. Not analysed (the game's process was gone; a
Wine-side exit cannot be told from a test-side kill with what the lab kept).

## Root cause
Not the mod: a second lab run started on the lane while the first still used it, and its start-of-run
`spore.sh kill` ended both games of lane 7.

Observed (2026-11-13, fix/w49-tribe3):
* fix/w49-mirror's run 20261113-052132 (lane 7, `--quick`, 7 tests, 19.1 min) had same_view_tribe as its last test:
  games booted 05:40:05 / 05:40:06, the host's log ends 05:40:29.3, the guest's 05:40:30.9 (both mid-play, no fault
  line, no "partner lost" line on the guest: both games went at once), the test's BrokenPipe at ~05:40:36.
* The same branch started two more lab runs at **05:40:31**: 20261113-054031 (lane 6) and **20261113-054031-2 on lane
  7**. `lab.py run` kills the games of its lanes right at its start (`for lane in a.lane_ids: spore(tree, lane,
  'kill')`, before any test) and checked nothing about another run there.
* `journalctl --user`: the two Bottles scopes of lane 7's games (started 05:40:03 / 05:40:05) ended at 05:40:31 and
  05:40:32 ("Consumed 18-19 s CPU over 26-29 s"), the second the new run began. Kernel journal: nothing (no OOM
  kill); the lab's memory record: lowest MemAvailable 35.8 GB, the games 1058 MB.
* The collision went both ways: 052132's own end-of-run kill (05:40:36+) then killed 054031-2's first games:
  its tribe_mirror_stale log says "instance 14 did not answer in 90 s: the lane starts again" (games ready after 127 s).
* No crash report, no SporeKit watchdog / hang file in the lane, no Wine fault line: a SIGKILL leaves none.
* Mod changes of 0.7.14 (mirror_stale_anim's ClearPending, mirror_phase's +0xA8 write, join_apart) ruled out as the
  cause by the timing above; repeats with them on did not lose a game (Verification).

## Fix
tools/lab.py `take_lanes`: a run holds an exclusive flock on `SPORE-lab/.lab-run-lane<N>.lock` for each of its lanes
for its whole life (the kernel drops it when the process ends, also on a crash or SIGKILL; the file names the holder:
run id, pid, start time). A run whose lane is held exits at once with "lane N is in use by another lab run (...)",
before the build and before its kill. Old behaviour (no lock): `SPORE_LAB_NO_LANE_LOCK=1` (the self-test's twin).
Only lab runs of a tree with this change take the lock: a run from an older tree is not stopped by it.

## Verification
tools/tests/lab_selftest.py `lane_lock` (make test): run a holds lane 10 (stubbed, no game) for a 4 s test, run b
starts on lane 10 1.5 s later. FAIL with the lab of rel/0.7.14 (`--tree ../../rel/0.7.14`: b exits 0 and calls
`spore.sh kill` on lane 10 three times while a's test runs), FAIL with `SPORE_LAB_NO_LANE_LOCK=1`, PASS on
fix/w49-tribe3 (b exits 1 "in use by another lab run", no kill; a passes).
no-fail-run: lab tooling, shown by the self-test (no game); the game-side cause is ruled out by the repeats below.
Repeats with all of 0.7.14's mirror changes on: same_view_tribe x6 lane 7 (20261113-060215, all passed, 6 cold
starts), play_tribe --quick x3 lane 6 (20261113-060217, all passed), same_view_tribe --cold x3 lane 6 (FINAL6A), and every
other lab run of this branch: no game lost without a report.

## History
- 2026-11-13: opened.
- 2026-11-13: explained on fix/w49-tribe3: a second lab run on lane 7 killed the lane's games; lab lane lock (30dd6a2f).
