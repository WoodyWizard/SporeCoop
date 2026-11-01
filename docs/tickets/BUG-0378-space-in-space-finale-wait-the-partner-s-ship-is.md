---
id: BUG-0378
title: space: in space_finale_wait the partner's ship is shown about 20.7 m from where it is (sync-check p0 apart)
status: open
kind: bug
stage: Space
reported: 2026-11-01
source: lab log gates report over recorded runs (feat/w30-lab-gates, 2026-11-01)
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
`lab.py gates`: space_finale_wait 4 of 4 passed runs, `p0 20.7` apart; campaigns have 25 results with Space p apart and 12 with p missing/extra. Related: BUG-0190.
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
