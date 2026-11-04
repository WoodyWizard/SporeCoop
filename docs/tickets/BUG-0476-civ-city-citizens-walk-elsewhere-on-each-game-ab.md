---
id: BUG-0476
title: Civ: city citizens walk elsewhere on each game (about a fifth to a half without a partner) and some exist on one game only
status: fixed
kind: desync
stage: Civ
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, report out/detector/20261103-0236.md)
role: 
versions: 
fixed_in: 0.7.5
branch: fix/w41-civ-citizens
commits: f10322da
tests: civ_citizens (tools/tests/coop_civ_citizens.py)
signature:
docs: docs/re/civ.md#civ-roads-citizens
related:
switch: civ.citizens
---

## Report
Detector: `Civilization:unmatched:citizen` in 59% of 366 Civ comparisons (4 of 19 without a partner within 15 m), `guest_only:citizen` 17% (up to 4), `host_only:citizen` 2% (up to 15). City citizens are simulated by each game; only vehicles and buildings are mirrored. Cosmetic mostly, but a target of the other player's attack or conversion may not be where it shows. Class G4.
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed: both games spawn the same number of citizens (noun 0x18EB4B7) per city (19 / 19 around the host's capital) and each walks its own; 9 of 19 had no partner within 15 m in every comparison (detect_civ, lab 20261103-233836). Citizen_OffStageCheck is not what walks them in Civ (a filter on it skipped 0 calls). Inferred: their behaviour tree picks the walks.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
Citizens are a moving kind of the world-object table (worldobj.cpp `citizen`, stage Civ): the host's 56 citizens nearest each guest's pointer (its capital without one) every 0.5 s; the guest binds its own by position (30 m) and walks each with the game's MoveTo to where the host's is now (velocity from two snapshots, 1.5 s ahead, a new MoveTo only when the goal moved 2 m or after 1.5 s), put there when 12 m off. `coop.fix civ.citizens off`: each game walks its own.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
FAIL with SPORE_FIX_OFF=civ.citizens: out/lab/20261104-001329 (99 of 152 host citizens without a partner, 65 %). PASS: out/lab/20261104-000735 (2 of 2 runs, at most 2 % unpaired). Detector detect_civ: before out/lab/20261103-233836 (unmatched:citizen 24 of 24 comparisons, 464 objects), after 1 s snapshots out/lab/20261103-234403 (116 objects); the regression run with 0.5 s below.
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-04 (fix/w41-civ-citizens): fixed (world-object kind `citizen`, docs/re/civ.md#civ-roads-citizens).
- 2026-11-04: released in 0.7.5.
