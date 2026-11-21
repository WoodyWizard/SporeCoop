---
id: BUG-1378
title: evo_screens_switch_lowfps: the guest's black-screen check counts the game's own stage-entry pause (BUG-0313) at 10 fps (12-22 s in 11 runs against the 20 s limit)
status: open
kind: test
stage: Creature
reported: 2026-11-20
source: final check 20261120-133703
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
- 2026-11-20: opened.
