---
id: BUG-0380
title: civ: vehicles missing on guests and money totals differ in campaigns (sync-check v missing, p apart, totals)
status: open
kind: bug
stage: Civ
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
`lab.py gates`: Civ v missing in 35 results, Civ p apart in 29 (campaign_space 45/45 red). Monitor: Civ money host 10960 vs both guests 10590 for 20+ s in campaign_space (42 violations). Related: BUG-0319, BUG-0325.
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
