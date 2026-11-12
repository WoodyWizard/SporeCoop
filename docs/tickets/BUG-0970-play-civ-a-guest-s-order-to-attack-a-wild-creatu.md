---
id: BUG-0970
title: play_civ: a guest's order to attack a wild creature is rejected by the host (the creature is not within 10 m there)
status: open
kind: desync
stage: Civ
reported: 2026-11-12
source: rel/0.7.13 integration out/lab/20261112-155526 play_civ
role: guest
versions: rel/0.7.13
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
- 2026-11-12: opened.
