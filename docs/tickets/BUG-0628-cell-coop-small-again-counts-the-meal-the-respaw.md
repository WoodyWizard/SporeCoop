---
id: BUG-0628
title: cell_coop: 'small again' counts the meal the respawned cell ate before a slow first sample
status: open
kind: test
stage: Cell
reported: 2026-11-06
source: INT3 rerun lab 20261106-130235 cell_coop#2
role: 
versions: 0.7.9 828055b8
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0364, BUG-0356, BUG-0627
---

## Report
INT3 rerun lab 20261106-130235 (cell_coop x3 cold on lane 2, a loaded PC): #2 `FAIL small again (target size 0.55 ->
0.41)`; the respawn itself, its body 0.5 and its place next to the partner passed.

## Root cause
Observed (guest log): `Bob respawned small next to Alice` 13:08:28.590, `eat: own cell ate +20 meat` 13:08:30.330;
the test's first sample after the respawn showed own_eats 3 -> 5, own_points 32 -> 44 and target 0.41 = 0.28 + 0.13:
the sample came ~1.7 s after the respawn (console answers slowed by the load), after a legitimate meal of the
respawned cell (the test comment already notes the respawned cell "eats what floats by"). Not a game or mod bug.

## Fix
Test (coop_cell.py): the check allows the growth of the food points counted since the knocked-out sample (0.01
per point, the game's rule the test checks itself in "eating grows it back"); meals of a knocked-out cell stay
cell_ko_food's (BUG-0364).

## Verification
FAIL: lab 20261106-130235 (#2). PASS: lab 20261106-131524 (3 of 3 cold, target 0.28 each; the allowance was not
needed there: the case needs a meal inside the slow sample, seen once in 10 runs).

## History
- 2026-11-06: opened.
