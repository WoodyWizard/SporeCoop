---
id: BUG-0885
title: Detector: coop.census has no stage-change guard (the same freed-list read BUG-0605 fixed for coop.wstate)
status: open
kind: crash
stage: Civ
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0605, BUG-0271, BUG-0427
---

## Report
Lab only (players never call it). The detector's `coop.census all` (tools/tests/worlddiff.py:223) can run during a Civ -> Space change like coop.wstate; it then walks the game's vehicle and object lists while they hold freed objects, the crash BUG-0605 fixed for coop.wstate only.

Severity: low. Confidence: medium.

## Root cause
* Observed (code): mods/coop/wstate.cpp:152 has the guard (`game::mode_id() != mode || game::leaving_stage()`, census.wstate_live).
* Observed (code): mods/coop/census.cpp:130-155 c_census takes `mode` from C.place (the lagging frame mode) with no such check, then calls civ::census_rows (civ.cpp:4417: all_vehicles(), reads of +kPol / +kVHp on each) and all_rows (census.cpp:83: for_each_object, object_position, object_scale, wobj::bundle_holder).
* Inferred (medium; not seen in a run): the same freed-list reads as BUG-0605 in the same window.

## Fix idea
Apply the census.wstate_live check at the top of c_census (print `st phase changing=1` and `end objects=0`), behind the same switch or a twin `census.census_live`.

## How to test
campaign_space with SPORE_DETECTOR=20 (already its regression) plus a direct check: call `coop.census all` on the guest every frame through the Civ -> Space change (a tight loop from the lab); assert no fault record and no crash report. Hard to FAIL on demand: name the twin run with the switch off.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0605. Severity low, confidence medium.
