---
id: BUG-1315
title: Saves: a guest's species line key reaches the host's save (citizens, herds, wild creatures, main stream) despite the save strip
status: fixed
kind: bug
stage: All
reported: 2026-11-19
source: lab
role: 
versions: 
fixed_in: unreleased
branch: fix/w57-savestrip
commits: fc2cc5e3 0db36ea8 04f44839 99f52667
tests: tribe_own_species, civ_own_species, line_stages, line_stages_outfit, line_stages_editors
signature:
docs: docs/re/creations.md#creations-save-keys, docs/06-testing.md#testing-tribe-own-species, docs/06-testing.md#testing-civ-own-species
related: BUG-1270, BUG-1271, BUG-1273, BUG-1277, BUG-0020
switch: savestrip.keys
---

## Report
The first lab run of 0.7.16 (20261119-095414): the owner's rule "the host's save keeps only the host's species and
designs" failed in every test that parses a save: tribe_own_species (the slot's Zorispe.spo named the guest's line key
7B635F66), civ_own_species (citizen records 0x018EB4B7 named the guest's key; the same with civ.own_species_save off,
20261119-102655), line_stages / _outfit / _editors (Tribe: records 0x01897C18, 0x01BE418E, 0x018EB4B7, 0x018EB45E; Civ:
also 0x03572E72 and planetRecords.pkp 0x05220CB8). The per-object strips of WP-T1 / WP-C1 ran ("4 members carry the
tribe's species in this save") and changed nothing.

## Root cause
docs/re/creations.md#creations-save-keys.
* Observed (lab 20261119-113320, Civ, with a logging key filter): the citizens' strip set +0xB28 to the civilization's
  key at the save's start (19 citizens, twice: at SaveGame and at the first object reference) and 30 ms later the
  attribute writer 0xAE33DF wrote the guest's key from those citizens' +0xB28. Inferred: something between the save's
  start and the record's write sets a creature's +0xB28 from its profile again; a strip of +0xB28 cannot keep a
  creature's species out of a save (not found what, not needed).
* Observed (segment saves of 095414): the guest's species also sits in objects no package touches: herds and wild
  creatures of that species, object records of the main stream, and the game's event log (TribeStage_BabyBorn's event
  0x782E0768 names the baby's species twice, little-endian, written raw by GameEventLog_Write 0x7EB210).

## Fix
At the stream, for every object (savestrip.cpp, switch `coop.fix savestrip.keys`):
* `Stream_WriteResourceKey` 0xAE2DC0 (every class's ResourceKey attribute and 11 direct writers), `IO::WriteInt32`
  0x93A9E0 blocks of 3+ dwords ({instance, type, group} or {type, instance, group}) and the event lists of
  `GameEventLog_Write` 0x7EB210 (memory changed for that call and put back): while a save is written (from SaveGame
  0xB29000, the hook in space_allies.cpp now brackets it with `savestrip::world_save`, or from the save's first object
  reference, to the end of that frame), every creature key that is or was a species line's key on this game
  (`species::is_line_key`, which now also remembers every key a line had in this process) is written as the world's
  species: the player tribe's slot in Tribe, the player civilization's species in Civ, else the avatar's / the host's
  creature (`species::set_world_species`, registered by tribe_species.cpp and civ_species.cpp). Memory is not touched.
* The save hooks of every package now also start at SaveGame (before the main stream), and end at the next frame as
  before. The per-object strips stay as a second guard; design keys are civ_own_designs' (not rewritten here).
* `coop.savestrip` shows `keys= line_keys= rewritten= early= raw= events= kept_off=`, `coop.species` the line keys known
  and rekeyed; the first 8 rewrites of a save are logged with the caller and the holder (noun, offset).

## Verification
* FAIL (before the fix): lab 20261119-095414 (tribe_own_species, civ_own_species, line_stages / _outfit / _editors),
  20261119-102655 (civ_own_species with civ.own_species_save off); FAIL twins with `savestrip.keys` off and the
  per-object strips on: lab 20261119-120449 tribe_own_species (records 01897C18, 018EB4B7; `stripped=6` by the
  members' strip) and civ_own_species (018EB4B7; `save_stripped=38` by the citizens' strip).
* PASS: lab 20261119-114816 tribe_own_species (`rewritten=5 events=2`, no key in the slot) and civ_own_species
  (`rewritten=19`); 20261119-113320 civ_own_species; line_stages' Tribe save clean in 20261119-114816 (its Civ step
  stopped before the save, see BUG-1277).

## History
- 2026-11-19: opened.
- 2026-11-19: fix/w57-savestrip: diagnosis with a logging key filter, the stream-level fix, the twin runs.
