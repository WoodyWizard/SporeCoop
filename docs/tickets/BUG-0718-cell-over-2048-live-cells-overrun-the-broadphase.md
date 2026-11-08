---
id: BUG-0718
title: Cell: over 2048 live cells overrun the broadphase merge's stack buffer (MergeAxis 4096 endpoints), bounded only by soft budgets
status: open
kind: crash
stage: Cell
reported: 2026-11-08
source: BUG-0432 sibling list, K2b audit 2026-11-08
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
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-08: opened.
