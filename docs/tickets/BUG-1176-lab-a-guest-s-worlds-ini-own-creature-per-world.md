---
id: BUG-1176
title: lab: a guest's worlds.ini (own creature per world, BUG-0345) survives the cold start, so whether the guest plays its own creature in the fixture world depends on the lane's history
status: fixed
kind: test
stage: Tools
reported: 2026-11-15
source: fix/w51-evolve (BUG-1170, BUG-1173)
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-labstate
commits: eddafb3a, c2068f03
tests: warm_fingerprint, evolve_world, editor_strays, guest_editor, puppet_jump
signature:
docs: docs/06-testing.md#testing-warm-fingerprint, docs/archive/known-issues-2026.md#issue-lab-worlds-ini
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
* tools/tests/twoinst.py `_start` (cold): removes `SporeCoop/worlds.ini` of both profiles with `choices.ini` (the
  fixture state: no record; the guest plays the fixture world's species). `SPORE_WORLDS_INI_RESET_FIX=0`: the old start
  (the twin).
* The warm fingerprint (D2, BUG-1179) covers `choices.ini`, `worlds.ini` (entries with a value) and `EditorSaves.package`
  of both profiles: a test that leaves a record, a choice or a new own creation makes its lane's next start cold.
* Tests that need the own creature set it: evolve_world now chooses it (`creations_state.choose_own`, `restore` at the
  end; BUG-1173 needs it); guest_editor and puppet_jump did already. editor_strays runs with the world's species now
  (the "shared" branch of its wait, not run before: BUG-1170). Lanes 2-6, 9 and 10 had a record before (lane 7 none).

## Verification
* FAIL (twin `SPORE_WORLDS_INI_RESET_FIX=0`, warm_fingerprint): out/lab/20261115-202555 (lane 10: after the cold start
  the guest still has the record 290225CD for the fixture world).
* PASS warm_fingerprint: out/lab/20261115-202307 (lane 9: the record refuses the warm reuse, the cold start leaves
  worlds.ini with no record).
* The tests that depend on it, lanes 9 and 10: out/lab/20261115-202828 (lane 10: editor_strays, evolve_world,
  puppet_jump, guest_editor PASS); out/lab/20261115-202823 (lane 9: evolve_world, puppet_jump, guest_editor PASS;
  editor_strays died with both games at once mid-test (no crash report, no fault; host log ends normally), PASS on the rerun out/lab/20261115-204255). evolve_world on both lanes: "guest profile before: record 00000000 | chose 290225CD",
  the race of BUG-1173 set up ("own avatar ... waits: cinematic").

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-labstate (the cold start resets worlds.ini; evolve_world chooses the own creature).
- 2026-11-18: released in 0.7.15.
