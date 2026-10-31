---
id: BUG-0348
title: Cell -> Creature, 4 players: guest heap corruption found when a Cell pool is freed on leaving (GeneralAllocator_FreeInternal under SimUpdate+0x750), then freeze
status: open
kind: crash
stage: Cell
reported: 2026-10-31
source: lab 20261031-120025 campaign#1
role: guest
versions: 0.7.3.1+main 242a7bd
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
- 2026-10-31: opened.
