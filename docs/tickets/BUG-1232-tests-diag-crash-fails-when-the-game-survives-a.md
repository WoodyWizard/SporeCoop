---
id: BUG-1232
title: tests: diag --crash fails when the game survives a fault in the frame (BUG-0667 frames go on, no hang or crash report)
status: fixed
kind: test
stage: Tools
reported: 2026-11-18
source: final check 20261117-154911
role: 
versions: 0.7.14.1, unreleased
fixed_in: unreleased
branch: rel/0.7.15
commits: 00208f5f
tests: diag
signature:
docs:
related: BUG-0667, BUG-0966
---

## Report
0.7.15 final check (lab 20261117-154911, lane 5) and its rerun (20261117-213354, lane 3): diag FAIL "a fault in the
frame: a hang or a crash report ([])"; the same on rel/0.7.15 in 4 of 4 runs of lab 20261117-223551-2 (lanes 2, 3),
PASS in other runs of the same build (20261117-223337) and of bisect builds.

## Root cause
Observed (lane 2 log mods/logs/previous, diag#3 of 20261117-223551-2): `call 0x1 cdecl` faulted inside the game's frame
(called from EffectWorld_ctor), the game's own handler caught it, SporeKit logged "console: the command 'call' was cut
off by a fault the game's handler caught (BUG-0966)" and "frames: the frame 402 was cut off ...; frames go on
(BUG-0667)", and the game drew on at 60 fps. No hang report (frames never stopped) and no crash report (nothing
reached the unhandled filter). The test knew two outcomes only (the fault reaches the unhandled filter, or the game
stops drawing), written before SporeKit 0.3.11 made the third one (the game lives on) possible. Which of the three a
run gets depends on where the console command runs in the frame; not a code change of 0.7.15 (the SporeKit frame code
is the same as in 0.7.14.1).

## Fix
Test only: diag_check.py also passes when the host's log gets the BUG-0667 "frames go on" line after the call and a
new console connection then answers `wait.frames 30` (the game lives on). The crash report and minidump checks of the
unhandled fault (a thread of its own) are unchanged.

## Verification
FAIL without the fix: out/lab/20261117-223551-2 (diag 4 of 4). PASS with it: out/lab/20261117-231627 (diag 4 of 4;
run #2 by the new outcome "survived: frames: the frame 330 was cut off ...", the others by a crash report).

## History
- 2026-11-18: opened in the 0.7.15 final-check triage; fixed (test only).
