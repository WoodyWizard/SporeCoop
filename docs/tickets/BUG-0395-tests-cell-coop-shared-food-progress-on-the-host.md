---
id: BUG-0395
title: tests: cell_coop 'shared food progress on the host' fails when the host's own cell eats a plant in the 2 s window
status: open
kind: test
stage: Cell
reported: 2026-11-01
source: lab 20261101-104055 cell_coop (fix/w31-cell-gate, lane 7)
role: 
versions: 
fixed_in:
branch:
commits:
tests: cell_coop
signature:
docs:
related: BUG-0364
---

## Report
cell_coop failed only "shared food progress on the host (0 -> 32)" and "guest has the same total (20)"
in lab 20261101-104055 (lane 7, fix/w31-cell-gate, 1 of 130 recorded cell_coop results). The host's
log: the guest's `coop.pool food 20` applied at s 31.46, then "eat: own cell ate +12 plant (total
32)" at s 33.40: the host's idle cell ate a plant that floated into it in the test's 2 s window; the
guest got 32 at s 33.75 (after its check read 20). Not a desync.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed: the test expected the host's total to be exactly the guest's +20; the host's own meal
in that window (the game's own eat, counted in the host's `own_points`) added 12.

## Fix
Test only (fix/w31-cell-gate): the check allows what the host ate itself in the window (`own_points`
of its `coop.pool`). The guest's "same total" check waits up to 3 s for the host's total (sent every 0.5 s).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
