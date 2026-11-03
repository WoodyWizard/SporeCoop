---
id: BUG-0474
title: Tribe: eggs of nests exist in the host's game only (the guest never shows them)
status: open
kind: desync
stage: Tribe
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
Detector: `Tribe:noun_only_host:egg` in 38% of 218 Tribe comparisons (5 eggs in the host's game, 0 in the guest's), `Tribe:host_only:egg` 30%. The egg mirrors of BUG-0384 run in the Creature stage only. Creature still differs at times (`Creature:host_only:egg` 13%, `guest_only:egg` 8%). Class G4.
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
