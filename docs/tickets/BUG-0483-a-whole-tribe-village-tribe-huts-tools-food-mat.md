---
id: BUG-0483
title: a whole tribe village (tribe, huts, tools, food mat, members) exists in the guest's game only (Tribe and Civ)
status: open
kind: desync
stage: Tribe
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
Detector: `Tribe:guest_only:tribe`, `tribe_hut`, `tribe_tool`, `tribe_food_mat`, `citizen` together (1% of Tribe comparisons, e.g. at (83 -423 -268)), and `Civilization:guest_only:tribe` / `tribe_hut` / `tribe_tool` / `tribe_food_mat` (2%, at (-249 -33 475)). A tribe the host's game no longer has (conquered, allied, removed) or one the guest's game placed itself is still on the guest. Class G4 / M6 (the guest's own NPC tribes).
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
