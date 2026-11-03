---
id: BUG-0490
title: Space: on a planet, colony citizens and vehicles differ between the games
status: open
kind: desync
stage: Space
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, out/detector/20261103-final.md)
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
Detector (rare, 1% of Space comparisons): `Space:unmatched:citizen` (38 host / 20 guest, 22 without a partner), `unmatched:vehicle` (1 host / 3 guest), `scale:citizen` (2.19 / 1.82). Colonies on a planet are simulated by each game; the Space stage mirrors ships, not planet life. Class G4, low priority.
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
