---
id: BUG-0817
title: Creature: the guest's mirror of the host's drama UFO is not kept out of the guest's saves
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: 
branch: fix/w48-saves
commits: bcc17e96
tests: ufo_mirror_save
signature:
docs: docs/re/creature.md#creature-saved-box-kinds, docs/06-testing.md#testing-saves-w48
related: BUG-0558 BUG-0470 BUG-0495 BUG-0020
---

## Report
No player report. A save the guest's game writes while the host's drama UFO is over the players contains the mod's mirror ship (a real type-7 UFO).

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): player_rules.cpp:1000-1011 makes the mirror with UFOAbductionEvent_SpawnUFO; no savestrip filter covers it: squadron.cpp ufo_save_start returns unless in_space(), savestrip's creature filter looks at creatures and herds only. Breaks CLAUDE.md 'our objects never go into a save'. Confidence: medium on the code; how often a guest saves then is not known.

## Fix
Kept as hygiene (CLAUDE.md "our objects never go into a save"): a savestrip filter in player_rules.cpp writes a null
reference for the guest's UFO mirrors (`coop.fix creature.ufo_save_strip`). The visible harm the ticket feared is not
there: see Verification.

## Fix idea
Add a savestrip filter (and save hooks) for g_ufo_mirrors in player_rules.cpp.

## How to test
Host `coop.drama start 2` (UFO visit), guest saves while its mirror exists; assert the save log line counts the mirror among 'objects of ours kept out of it' and a solo reload has no type-7 ship. Main: the ship is in the save.

## Verification
no-fail-run: the game does not bring the ship back. `ufo_mirror_save` (coop_ufo_mirror_save.py): the guest saves
under the mirror (census: one UFO noun 018EBADC), leaves, loads its own slot alone.
* With the filter off: out/lab/20261112-015510 and 20261112-015723 (`SPORE_FIX_OFF=creature.ufo_save_strip`, lane 3):
  no ship and no UFO noun after the load, no fault.
* With it on: out/lab/20261112-015420 (the filter nulled 2 references to the mirror in the save).
So the serializer did reference the mirror, but a Creature load does not restore a type 7 ship. The test stays as a
guard.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12: not reproduced on fix/w48-saves (a solo load of a guest save made under the mirror has no ship with the filter off, lab 20261112-015510 / 015723); the filter creature.ufo_save_strip kept as hygiene.
