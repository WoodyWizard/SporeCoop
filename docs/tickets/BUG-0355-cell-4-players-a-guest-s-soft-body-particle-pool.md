---
id: BUG-0355
title: Cell: 4 players, a guest's soft-body particle pool fills in ordinary play; cells sharing the spare slot drop its game to 3 fps for good (stuck in Cell)
status: open
kind: freeze
stage: Cell
reported: 2026-10-31
source: lab 20261031-151552 campaign_short#1 (extra1, lane 2)
role: guest
versions: 0.7.3.1+main 251d80d
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
