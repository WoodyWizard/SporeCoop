---
id: BUG-1246
title: Cell: the mate a player summons with the mating call is not in the other player's game
status: open
kind: bug
stage: Cell
reported: 2026-11-18
source: owner: players' video, second analysis
role: guest
versions: <0.7.15
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-1245
---

## Report
A players' video: the guest calls a mate ("do you see the male? on the left"), the host swims right next to it and sees nobody. Suggested cause: the guest's summoned mate is made locally (mate_idx G+0x51D4, cellsync.cpp) and the cell pool only goes host -> guests (MSG_CELL_STATE).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
