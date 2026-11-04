---
id: BUG-0517
title: the host's avatar keeps its pre-editor species after its evolution editor until the game is saved
status: fixed
kind: bug
stage: Creature
reported: 2026-11-03
source: test (coop_evolve_in_place.py, fix/w40-no-reload)
role: host
versions: 
fixed_in: 0.7.5
branch: fix/w41-creature-left
commits: 49d287f4
tests: evolve_key (coop_evolve_in_place.py --case creature --nosave), evolve_in_place_creature
signature:
docs: docs/re/creations.md#creations-evolve-key
related: BUG-0508, BUG-0457
---

## Report
Found while making BUG-0508: with the world share after the host's evolution editor skipped, the host's own
avatar kept the pre-editor species (065CBE70) for 2+ minutes (the trace's `species=`), although the game's world
player species (cSpeciesManager +0xA4) was the new one (291433C0) and the new creation was in the host's My
Creations (lab 20261103-173652). With a save of the game (the share's save of 0.7.3.6, or the one BUG-0508's fix
makes) the avatar takes the new species 0.6-1.7 s later (20261103-173315, 20261103-174221).

## Root cause
Observed (Ghidra, docs/re/creations.md#creations-evolve-key): the stage's return 0xD43DE0 applies the editor's result by reloading the avatar's species profile in place (0x4DFBC0) and vt 0xAC; the creatures' own species key +0xB28 is not written, only a save writes it. Observed (lab 20261104-000929, no save): the host's avatar kept +0xB28 = 065CBE70 after the editor and the guest got the world after all. Inferred (not run alone): a host without guests keeps the old key until it saves.

## Fix
coop.cpp evo_key_tick, switch creature.evolve_key: within 60 s after an evolution editor every creature with the avatar's profile and the pre-editor key gets the profile's key (+0x504); the in-place save of BUG-0508 is then skipped. coop.fix creature.evolve_key off: the save as in 0.7.4.

## Verification
evolve_key (coop_evolve_in_place.py --case creature --nosave: the host's in-place save off, as a host alone):
- FAIL with SPORE_FIX_OFF=creature.evolve_key: out/lab/20261104-000929 (the host's key stayed the old one, the guest was sent the world: GalaxyGameEntry, LoadGame).
- PASS: out/lab/20261104-000647 (keys_set=6, saves 0, the guest stayed in the stage, host 065CBE70 -> 29148F23).

## History
- 2026-11-03: opened.
- 2026-11-03: fixed on fix/w41-creature-left (creature.evolve_key).
- 2026-11-04: released in 0.7.5.
