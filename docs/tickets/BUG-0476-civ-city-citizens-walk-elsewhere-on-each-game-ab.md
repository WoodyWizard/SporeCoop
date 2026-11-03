---
id: BUG-0476
title: Civ: city citizens walk elsewhere on each game (about a fifth to a half without a partner) and some exist on one game only
status: open
kind: desync
stage: Civ
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, report out/detector/20261103-0236.md)
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
Detector: `Civilization:unmatched:citizen` in 59% of 366 Civ comparisons (4 of 19 without a partner within 15 m), `guest_only:citizen` 17% (up to 4), `host_only:citizen` 2% (up to 15). City citizens are simulated by each game; only vehicles and buildings are mirrored. Cosmetic mostly, but a target of the other player's attack or conversion may not be where it shows. Class G4.
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
