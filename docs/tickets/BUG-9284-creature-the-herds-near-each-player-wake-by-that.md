---
id: BUG-9284
title: creature: the herds near each player wake by that player's own brain level (galaxy planets)
status: wontfix
kind: feature
stage: creature
reported: 2026-11-20
source: step 4 part 2 (docs/12 #server-party-progress-design)
role: host
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
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-20: opened.
- 2026-11-20: wontfix: every wild herd is [0,5] (two saves); the code was taken out again.
