---
id: BUG-1145
title: Tests: monitor 'together' fails space_allies when the guest comes back from the galaxy view (own state read up to 6 s ahead)
status: open
kind: test
stage: Space
reported: 2026-11-15
source: test
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
space_allies#4 of `lab.py run space_allies --repeat 12` on fix/w51-space2 (run out/lab/20261115-042849, lane 7, cold):
every check of the test passed, no fault, but the lab's gate monitor failed it: "together: guest and host play Space
80460035 by their own games, but guest sees host elsewhere (same_place=0) for 10 s from s 10". The guest's own trace
says world `-` from s 8.2 to s 20.6 (the test zooms it out to the galaxy view, `to_galaxy`, and back: 12.4 s this
time); its partner lines at s 10.0 and s 20.0 say same_place=0, and at s 20.6 (back in the system) same_place=1.
Inferred: `Together.check` takes the viewer's own state with `own_at(viewer, t, 6)` (up to 6 s off), so the partner
line at s 20.0 is paired with the own line of s 20.6 (world 80460035 again) and the one at s 10.0 with a line that
also says 80460035, which makes a 10 s "run" out of the galaxy-view stretch. Not seen before in any worktree's logs.
Reproduce: `lab.py run space_allies --repeat 12 --lane N` (1 in 4 here); not fixed (outside the task of fix/w51-space2).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
