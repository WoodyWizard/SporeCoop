---
id: BUG-0475
title: Tribe and Civ: citizens (tribe members, city citizens) are shown at another size on the guest
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
Detector: `Tribe:scale:citizen` in 70% of Tribe comparisons (3.79 host / 4.47 guest), `Civilization:scale:citizen` 50% (1.50 / 1.21). Same class as BUG-0461 (NPC scale sent and not applied): the mirrors keep a default or own scale. Class G2 (BUG-0419).
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
