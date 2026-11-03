---
id: BUG-0484
title: Civ: cultural targets, dropped bundles and city turrets differ between the games
status: open
kind: desync
stage: Civ
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
Detector: `Civilization:unmatched:cultural_target` 9% of Civ comparisons (2 of 3 without a partner within 3 m), `unmatched:game_bundle` 5% (3 host / 5 guest), `noun_only_guest:turret` 1% and `noun_only_host:turret` 1% (a city defence turret in one game only: buildings are mirrored, turrets are not?). Classes G4 (not mirrored) and G2.
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
