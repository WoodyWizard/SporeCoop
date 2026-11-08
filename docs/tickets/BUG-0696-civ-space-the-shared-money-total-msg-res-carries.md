---
id: BUG-0696
title: Civ / Space: the shared money total (MSG_RES) carries no cause: a guest never learns whose earning or spending moved it
status: open
kind: bug
stage: All
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
From the K2a audit of BUG-0423. MSG_RES (money, food totals) and MSG_RES_ADD carry no cause, unlike the DNA total (BUG-0389): a guest's money jumps with no notice of whose purchase or earning it was.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
