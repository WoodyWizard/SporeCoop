---
id: BUG-0382
title: campaign: a guest still in Civ 90 s after the host entered Space (monitor stage_apart)
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
Monitor calibration over recorded runs: campaign_space 11 stage_apart violations (a guest stays in Civ about 90 s after the host enters Space).
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
