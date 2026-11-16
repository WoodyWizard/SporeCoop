---
id: BUG-1176
title: lab: a guest's worlds.ini (own creature per world, BUG-0345) survives the cold start, so whether the guest plays its own creature in the fixture world depends on the lane's history
status: open
kind: test
stage: Tools
reported: 2026-11-15
source: fix/w51-evolve (BUG-1170, BUG-1173)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0345, BUG-0638, BUG-1170, BUG-1173, BUG-0689
---

## Report
Found while fixing BUG-1170 / BUG-1173 (2026-11-15): on lanes 9, 10 and 3 the guest profile's
`SporeCoop/worlds.ini` has `[creature] 68AC222A=<own creation>` (the creature fixture's world), so in every Creature
test on these lanes the guest plays its own creature 'Kirgle' ("from this world's record (worlds.ini)") 3 s after
each world load; on a lane without that record it plays the world's species. twoinst's cold start deletes
`choices.ini` (and PRESET_CHOICES writes it) but leaves `worlds.ini`, which any test that ends an own editor with a
creation (or chooses one) writes for the fixture world. editor_strays and evolve_world passed or failed with the
lane's history (BUG-1170: no in-place fallback share for an own creature; BUG-1173: the swap 3 s after the load).

## Root cause
Observed: tools/tests/twoinst.py `_start` (cold) removes only `choices.ini` of both profiles; creations.cpp
`world_record_set` writes `worlds.ini` when an own editor's result or a choice is recorded for a world (BUG-0345,
BUG-0638). Inferred: the fixture world's crc is the same in every test, so one test's record decides the next tests'
guest creature. Not changed here (it changes the precondition of every Creature / Cell test on a lane): proposal: the
cold start removes `worlds.ini` too and a test that needs the own creature sets it (`creations_state.choose_own`, or
a PRESET_WORLDS like PRESET_CHOICES), then a Creature regression run.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
