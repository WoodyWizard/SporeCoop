---
id: BUG-0586
title: "Tests: play_tribe's minimap calibration ran during the guest's cold join pause (the view did not move)"
status: fixed
kind: test
stage: Tribe
reported: 2026-11-04
source: lab rel/0.7.7 20261104-220200, fix/w43-realign 20261104-213145, fix/w43-world-state 20261104-210354
role: guest
versions: 
fixed_in: 0.7.7
branch: fix/w43-tests
commits: 74fef091
tests: play_tribe
signature:
docs: docs/06-testing.md#testing-harness-g1
related: BUG-0444, BUG-0481
---

## Report
play_tribe failed its first check "guest: the minimap takes the view where it points" in lab 20261104-220200
(rel/0.7.7), fix/w43-realign 20261104-213145 and fix/w43-world-state 20261104-210354: the three calibration clicks
on the guest's minimap read the same view centre (406.26 -4.88 302.62, the start view over the player tribe) for o
and a (and b in two of them); `coop.status` had busy=1 (the game time stands) in the first two.

## Root cause
* Observed: in those runs the guest's trace lines had `busy=paused` until s 30-50 after its join (the cold join's
  pause, BUG-0444), and the calibration runs right after the setup's split of the player tribe, inside that pause.
* Observed (lane 3, 2026-11-04, scratch run): holding only the pause bit by hand (cGameTimeManager +0x48 = 3) does
  not stop the view: the calibration passed 3 of 3 with the bit held or not. So the bit is a marker of the join's
  pause (the loading game), not what stops the view.
* Inferred: during the cold join's pause the game takes no camera input from the minimap yet.

## Fix
Test only: play_tribe waits for both games' own clock to run (coop_sync.wait_running, at most 90 s) before it
calibrates, and the failure line prints the pause state. No threshold changed.

## Verification
* FAIL (the old test): lab 20261104-220200 (rel/0.7.7), fix/w43-realign 20261104-213145, fix/w43-world-state
  20261104-210354 (each run dir in that branch's worktree).
* PASS: out/lab/20261104-232522 play_tribe (lane 2, cold; "both games' time runs", both calibrations ok), and a
  scratch play_tribe on lane 3 (10 min, RESULT OK).
* Not verified: the pause as the cause (inferred; holding the bit alone does not stop the view).
## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-tests): the calibration waits for both games' clock.
- 2026-11-05: released in 0.7.7.
