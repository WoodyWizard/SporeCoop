---
id: BUG-0364
title: cell: a knocked-out cell still eats food, so it respawns bigger (cell_coop 'small again' failed)
status: open
kind: bug
stage: Cell
reported: 2026-11-01
source: lab 20261101-023131 cell_coop (main d6b81d9, lane 3)
role: guest
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
smoke in lab 20261101-023131: cell_coop failed only "small again (target size 0.55 -> 0.41)"; in the
14 earlier runs that pass the respawn target was 0.28. The guest's log: knocked out at s 46.46
(state 1, hp 1), then "own cell ate +12 plant (total 44)" at s 46.99 and "+12 plant (total 56)" at
s 47.23, both while still knocked out; the respawn at s 56.47 came with food 56 instead of 32, so a
bigger target size. Observed: a knocked-out cell eats what it touches. Inferred: the respawn size
follows the food total, so the eaten food made it bigger. Not analysed further; 1 failure in 41
cell_coop runs.
How to reproduce: `lab.py run cell_coop --repeat 6`, look for "ate" lines between ko state 1 and 3.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
