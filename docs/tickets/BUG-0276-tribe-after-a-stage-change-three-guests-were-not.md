---
id: BUG-0276
title: tribe: after a stage change three guests were not in the host's world within 300 s (they got there after a rejoin)
status: open
kind: bug
stage: tribe
reported: 2026-10-29
source: lab 20261029-122937 campaign
role: 
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Lab 20261029-122937 campaign#2 (lane 3, out/lane3/campaign/20261029-122952): after the stage change
to Tribe, "Bob, Carol, Dave did not get into the host's world in tribe within 300 s (together on the
host: [])". The campaign then made each leave and join again; after the rejoins all were together
(12:56:38). Logs: the 0N_stuck_* tails in that folder.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
