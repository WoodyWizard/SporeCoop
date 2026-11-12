---
id: BUG-0886
title: realign: a late chunk of an older planet-list cycle still restarts the cycle (CycleTracker not used there)
status: fixed
kind: desync
stage: All
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-net
commits: 2f9c631b
tests: unit (make test: validate_tests test_planet_rx)
signature:
docs: docs/06-testing.md#testing-n1-net
related: BUG-0616, BUG-0612, BUG-0425, BUG-0427
switch: net.cycle_order
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
Confirmed on the code (realign.cpp restarted on any other cycle). The receiver is `realign::PlanetRx`
(realign_rule.h, portable) on net::CycleTracker: a chunk of an older cycle is dropped (`older_chunks=` in
coop.realign), a chunk seen twice or a late duplicate of a complete cycle adds nothing (`dup_chunks=`), the u16
counter wraps. Behind the existing switch `coop.fix net.cycle_order off` (BUG-0612; off: the 0.7.12 receiver,
kept as it was in PlanetRx).

## Verification
* Unit (make test): validate_tests `test_planet_rx` (chunks 0, 1 of cycle 5, the last chunk of cycle 4, chunk 2 of
  cycle 5; a one-chunk older cycle; duplicates; the wrap): FAIL `validate_tests --nofix` ("cycle 5 ... NOT complete,
  1 objects", out/n1-unit/validate_nofix.txt), PASS in make test twice (out/n1-unit/validate_pass_1.txt,
  validate_pass_2.txt).
* No lab FAIL twin: the late chunk needs a reordering on demand. Lab regression: realign_creature_rejoin is not in
  my runs; smoke covers tribe_coop / civ_coop with the receiver.

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0616. Severity low, confidence high.
- 2026-11-12: fixed on fix/w48-net (0.7.13 N1): PlanetRx on CycleTracker; FAIL/PASS unit runs.
