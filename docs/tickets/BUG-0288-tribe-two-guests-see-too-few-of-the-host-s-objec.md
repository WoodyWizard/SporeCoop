---
id: BUG-0288
title: tribe: two guests see too few of the host's objects (coverage desync) in the 4-player campaign
status: open
kind: bug
stage: Tribe
reported: 2026-10-29
source: final campaign lab 20261029-203821 campaign#2 lane 4
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
Final 4-player campaign (main 3780da0, lab 20261029-203821 campaign#2, lane 4, run out/lane4/campaign/20261029-203829): at 21:07:57 in tribe/play the sync check reported "Bob: too few of the host objects in tribe (Bob:coverage)" and the same for Carol. No crash; the run went on to the finale. Log tails and screenshots are in that run folder (01_desync_*, 02_desync_*). Not analysed yet.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
