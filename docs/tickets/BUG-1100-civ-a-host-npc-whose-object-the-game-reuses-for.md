---
id: BUG-1100
title: Civ: a host NPC whose object the game reuses for another creature during a pause keeps its id; the guest shows the old one 690 m away (sync-check MISMATCH in civ_wild_pooled)
status: open
kind: desync
stage: Civilization
reported: 2026-11-14
source: final check 0.7.14, lab 20261114-005006 civ_wild_pooled
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
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-14: opened.
