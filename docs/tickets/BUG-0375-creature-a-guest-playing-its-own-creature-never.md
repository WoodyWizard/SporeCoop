---
id: BUG-0375
title: Creature: a guest playing its own creature never gets the brain level's health (max HP stays at level 0)
status: open
kind: bug
stage: Creature
reported: 2026-11-01
source: player reports 2026-11-01 (host marks 13:39:07, 13:48:17)
role: guest
versions: 0.7.3.3
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
- 2026-11-01: opened.
