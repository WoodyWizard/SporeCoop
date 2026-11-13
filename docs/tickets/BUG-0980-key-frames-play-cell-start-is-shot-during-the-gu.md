---
id: BUG-0980
title: key frames: play_cell start is shot during the guest's Cell intro (input lock): the meteor rock and the letterbox
status: fixed
kind: test
stage: cell
reported: 2026-11-12
source: lab-final-0.7.13
role: guest
versions: 0.7.13
fixed_in: 0.7.13
branch: fix/w48-fmisc
commits: ea7552b3
tests: play_cell, keyframe_settle
signature:
docs: docs/06-testing.md#testing-keyframe-framing
related: BUG-0924
---

## Report
Key frame 035 (play_cell start) of the 0.7.13 final check, lab 20261112-175447: the guest's screen has the
letterbox, the card "loading the world of Alice..." and a big rock over Alice's cell that the host's screen does not
have.

## Root cause
Observed: a test artefact, not a desync. The rock is the guest's own Cell intro (the meteor that the stage opens
with; the guest's game loads the host's world and plays it at the save's start point, where the host's cell is):
the guest's log has `busy=cinematic,input_lock` from 19:10:16, its cell joined the host at 19:10:21.5, and the frame
was shot at 19:10:21.8, when the game's cinematic flag and input lock were already off (the key frame's busy wait
saw nothing) but the intro's picture (meteor, letterbox) was still on. The same in 6 of 6 earlier play_cell start
frames (rel/0.7.12 and 0.7.13 runs). The mid frame (5 min later) never has it.

## Fix
Test only: `keyframe(..., calm=N)` (twoinst.keyframe_settle) waits until the games have also stayed not busy for N
s; play_cell's start frame uses calm=8.

## Verification
no-fail-run: a key frame's content, not a check. Before: out/lab/20261112-175447 frames/play_cell/01_start_guest.jpg
(and 6 earlier runs) with the meteor; after: out/lab/20261112-212739 (play_cell OK, the start frame waited 9.7 s,
both cells in open water, no letterbox, no rock). keyframe_settle still OK: out/lab/20261112-213006.

## History
- 2026-11-12: opened from key frame 035; calm wait added (fix/w48-fmisc).
- 2026-11-12: released in 0.7.13.
