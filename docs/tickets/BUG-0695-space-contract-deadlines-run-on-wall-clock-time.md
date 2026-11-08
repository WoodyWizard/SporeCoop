---
id: BUG-0695
title: Space: contract deadlines run on wall-clock time: a pause, a menu or a load uses them up
status: open
kind: bug
stage: Space
reported: 2026-11-07
source: K2a audit of BUG-0423 (0.7.11)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0423
---

## Report
From the K2a audit of BUG-0423. space_contracts.cpp deadlines (offer deadline, active time limit) use H->time_seconds(), a wall clock: a paused game, the host in a menu or a long load runs a contract out with nobody able to play it.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
