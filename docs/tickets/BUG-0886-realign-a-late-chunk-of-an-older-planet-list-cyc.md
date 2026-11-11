---
id: BUG-0886
title: realign: a late chunk of an older planet-list cycle still restarts the cycle (CycleTracker not used there)
status: open
kind: desync
stage: All
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0616, BUG-0612, BUG-0425, BUG-0427
---

## Report
The guest's whole-planet realign list (villages, huts, tools, nests, cities every 2 s) is chunked like the Tribe / Civ snapshots. A late chunk of an older cycle discards the newer cycle being received, so that list is lost, and a one-chunk older cycle can even become the compared list. Under loss or reordering this delays realign's corrections (its streaks keep a single stale list from removing anything).

Severity: low. Confidence: high.

## Root cause
* Observed (code): mods/coop/realign.cpp:249 `if (cycle != g_rx_cycle || g_rx_chunks.empty())` restarts on any other cycle, the exact pre-BUG-0616 pattern. net::CycleTracker (net/validate.h) is used only at tribe.cpp:2528 and civ.cpp:3760; squadron.cpp:887 has its own age check; realign has none.

## Fix idea
Use net::CycleTracker in realign::on_planet (drop older chunks within half the u16 range, count `older_chunks=`), behind net.cycle_order.

## How to test
Unit test in the tests/validate_tests.cpp style or a realign-level test: chunks 0, 1 of cycle 5, the last chunk of cycle 4, then chunk 2 of cycle 5; assert cycle 5 completes. FAIL with net.cycle_order off or on the current code, PASS with the fix.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0616. Severity low, confidence high.
