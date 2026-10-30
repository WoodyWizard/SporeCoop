---
id: BUG-0295
title: Cell, 3-4 players: the cell broadphase overflows its 4096-endpoint stack buffer when a game holds over ~2000 cells (fault at dc04aee5:Simulator::cStarRecord::GetPlanetRecord+0x4AB, 0x00BBB07B)
status: open
kind: crash
stage: Cell
reported: 2026-10-30
source: lab 20261030-035318 campaign#2/#3
role: 
versions: 0.7.3.1+main 1d51511
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
