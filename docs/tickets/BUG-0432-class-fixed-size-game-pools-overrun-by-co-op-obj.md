---
id: BUG-0432
title: class: fixed-size game pools overrun by co-op object counts (umbrella)
status: open
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
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
Fixed per pool in Cell: BUG-0295-0297, 0337, 0338, 0348, 0355. Not covered: the game's MergeAxis 4096-endpoint buffer guarded only by soft budgets at mod spawn points (cellsync.cpp:2208); objects created while the guest's sim is paused bounded only for cells; no inventory of fixed arrays in Creature / Tribe / Civ / Space pools.
General fix: inventory of fixed game arrays per stage; hard guards at the consumer; mirror budget near the camera; "same caught EIP every frame" detector. Outcome check: 4-player stress per stage with fault counters.
Source: docs/generalisation-plan.md G16.
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
