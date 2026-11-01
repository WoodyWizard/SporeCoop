---
id: BUG-0381
title: tribe: partner / members apart and citizens missing on guests (sync-check p apart, t missing, n apart)
status: open
kind: bug
stage: Tribe
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
`lab.py gates`: Tribe p apart in 31 results, t missing 10, n apart 8 (campaign_tribe 20/50, chaos_tribe 4/89, tribe_coop 1/90).
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
