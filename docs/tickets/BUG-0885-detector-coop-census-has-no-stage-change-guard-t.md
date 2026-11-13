---
id: BUG-0885
title: Detector: coop.census has no stage-change guard (the same freed-list read BUG-0605 fixed for coop.wstate)
status: fixed
kind: crash
stage: Civ
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-small
commits: 78418470
tests: census_leave
signature:
docs: docs/06-testing.md#testing-p1-g1
related: BUG-0605, BUG-0271, BUG-0427
switch: census.census_live
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
census.cpp `c_census`: the guard of coop.wstate (BUG-0605): while the game's own mode is not the frame's (a stage change) or the stage is being left (the galaxy menu quit, BUG-0264's window) the census reads no list and prints `st phase changing=1` and `end objects=0` (worlddiff takes an empty census for "loading", a gap, not a finding). The header now names the game's mode and the flag: `gamemode=<id> leaving=<0|1>` (after t=, so the parsers' patterns still match). `coop.fix census.census_live off`: reads anyway (0.7.12).

## Verification
census_leave (`coop_census_leave.py`, creature, warm, ~50 s): the guest posts the quit to the galaxy menu (`coop.menu`) and asks `coop.census all` until its game's mode changes; every census answered in that window (the header's game mode still Creature) must list 0 objects. The window is about one census long.
* FAIL with SPORE_FIX_OFF=census.census_live: out/lab/20261112-122411 (the census during the leave listed 789 objects of the stage being torn down, leaving=1).
* PASS: out/lab/20261112-124352, out/lab/20261112-124446 (0 objects, changing=1). (20261112-122501 listed 0 too but failed an older "asked 3 times" check of the test, since lowered to once: the window is about one census long.)
No fault was seen in the FAIL run (the freed blocks keep their content for a moment): the test asserts the read, not a fault; the Civ -> Space change of BUG-0605 takes the same path (campaign_space with SPORE_DETECTOR=20 stays its long regression).

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0605. Severity low, confidence medium.
- 2026-11-12 (fix/w48-small): fixed (census.census_live), test census_leave.
- 2026-11-12: released in 0.7.13.
