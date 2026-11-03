---
id: BUG-0478
title: Civ: the guest has a vehicle more than the host now and then; roads between cities built on the host are missing on the guest
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
Detector: `Civilization:unmatched:vehicle` in 28% of Civ comparisons (4 on the host, 5 on the guest, 1 without a partner: a stale or duplicate mirror), `guest_only:vehicle` 2%; `noun_only_host:inter_city_road` 10% (a road the host's game has, none in the guest's). Classes G5 (stale mirrors) and G4 (roads not mirrored).
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
