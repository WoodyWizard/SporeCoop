---
id: BUG-1215
title: leave_avatar_cell and parts_transition: after the host's first creature editor the world goes only by BUG-0508's 30 s fallback, held by the host's evolution caption; one test never confirms it, the other votes into it
status: open
kind: test
stage: Creature
reported: 2026-11-16
source: final check 20261116-025154
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Final check 20261116-025154: leave_avatar_cell FAIL "the guest leaves its stage for the world the host shares after its
editor"; parts_transition FAIL "the guest's open editor is found (no editor request (mode 03684580))" (its own editor
"opened" into GalaxyGameEntry). Both reproduced on lane 10 (rel/0.7.15, 20261116-082820). On v0.7.14.1 (lane 10,
fix/w51-t2-v07141/out/lab/20261116-084702) leave_avatar_cell FAILS the same; parts_transition passed there by timing.

## Root cause
Observed (host game logs): after the host's first creature editor (Cell -> Creature, Landfall editor FD4902BD) the host
keeps the guest in place (BUG-0508) with an unchanged species key ("host species 2924E0EB -> 2924E0EB"), and 30 s later
"Bob still plays 2924E0EB after 30 s: sending the world". That share waits for the host's evolution cinematic
(BUG-0019, `share_wait=cinematic`, the host's game paused under its caption):
* leave_avatar_cell never confirms that caption: the share waited past the test's 90 s (it came only with round 1's
  share). The test (2026-10-29) is older than BUG-0508 (0.7.4, 2026-11-03), when every editor shared the world at once.
  An old failure of the test (the same on v0.7.14.1).
* parts_transition confirms it (wait_no_cinematic) and starts its editor vote right away: the world sent after all
  arrives 1-3 s before the vote, the guest is in the galaxy menu / loading when the vote ends and its own editor never
  opens. On v0.7.14.1 the transfer took 11.6 s (busy machine), the editor opened first and the world loaded after it
  ("the world of Alice is loaded when you leave the editor"); in 0.7.15 the transfer took 0.4 s. A race of the test,
  not a 0.7.15 change of the mod.
? Whether the Landfall editor changes the species' content under the same key (the world after all then carries the
new body) was not looked at; not changed.

## Fix
* coop_leave_avatar.py (--cell): after the host's editor the test confirms the evolution captions of both games like a
  player (Enter while a cinematic plays) and waits up to 150 s until either the guest left its stage for the world
  sent after all (then checked as before: "the guest leaves its stage for the world the host shares after its
  editor", and its leaving window) or the host kept it in place and sent nothing after its 30 s check.
* coop_parts_shared.py (--from-cell): before the editor vote it waits until the evolution has settled: no 30 s check
  running, a world sent after all acknowledged (the host's log), the guest in the stage receiving nothing.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
