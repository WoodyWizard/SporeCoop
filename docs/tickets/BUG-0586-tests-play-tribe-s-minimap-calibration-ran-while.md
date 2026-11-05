---
id: BUG-0586
title: Tests: play_tribe's minimap calibration ran while the guest's game time was still paused after the cold join (the view did not move)
status: open
kind: test
stage: Tribe
reported: 2026-11-04
source: lab rel/0.7.7 20261104-220200, fix/w43-realign 20261104-213145, fix/w43-world-state 20261104-210354
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
- 2026-11-04: opened.
