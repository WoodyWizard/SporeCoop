---
id: BUG-0289
title: cell: a guest's cells out of sync by 2.2 (limit 1.2) in the 4-player campaign
status: open
kind: bug
stage: Cell
reported: 2026-10-29
source: final campaign lab 20261029-203821 campaign#3 lane 7
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
Final 4-player campaign (main 3780da0, lab 20261029-203821 campaign#3, lane 7, run out/lane7/campaign/20261029-203834): "Bob: objects of kind c out of sync in cell (Bob:c:2.2)" (limit 1.2). Bob is the 2nd player, so this is not the extra-guest case of BUG-0275. No crash; the run reached the finale. Not analysed yet.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
