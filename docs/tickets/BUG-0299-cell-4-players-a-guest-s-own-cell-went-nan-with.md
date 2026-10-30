---
id: BUG-0299
title: Cell, 4 players: a guest's own cell went NaN with no NaN mirror (BUG-0292 again), then PopulateTiles faulted and the level-up crashed in PopulateTile+0x8A
status: open
kind: crash
stage: Cell
reported: 2026-10-30
source: lab 20261030-053709 campaign_short#2 i14 (fix/w10-cell-4p)
role: guest
versions: fix/w10-cell-4p aaa9f5f
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
