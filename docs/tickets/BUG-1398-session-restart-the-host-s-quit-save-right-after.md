---
id: BUG-1398
title: session_restart: the host's quit save right after a share save was dropped by the game (species_line, every run), refused since BUG-1365 and not asked again
status: open
kind: test
stage: All
reported: 2026-11-21
source: final check 20261121-003202
role: host
versions: 0.7.16
fixed_in:
branch: fix/w62-saves
commits:
tests: species_line, own_editor_drop, civ_own_species, civ_own_species_hostmode, civ_owners_session, dna_own_store, space_session, space_session_npc, tribe_owners_session, tribe_own_species
signature: species_line: session_restart: host save: the previous save is still finishing (the game drops a save now) ... FAIL the host saved its game before quitting
docs: docs/06-testing.md#testing-save-taken
related: BUG-1365, BUG-1397, BUG-1149
---

## Report
Third final check of 0.7.16 (lab 20261121-003202, lane 6): species_line failed `the host saved its game before
quitting`: `session_restart: host save: the previous save is still finishing (the game drops a save now): try again in a
moment`, `store writes 2 -> 2`. It passed in 20261120-133703 and 20261120-041304. The suspicion was that the w61 fix
BUG-1365 (`world.save_allowed`) refuses saves that used to work.

## Root cause
* Observed (species_line.host.killed1.game.log, 20261121-003202): the guest's rejoin after `creature.mode own` made
  the host save for the world share (box up s 13.10, confirmed, written s 13.80); session_restart's `coop.save` came
  about 1.5 s later (its 20 s store wait ended with the guest's leave at s 35.78). That is inside the 1.5 s after the
  box's OK in which the game's Save handler drops a Save message (docs/re/creature.md#creature-save-dropped).
* Observed (the PASS runs): the save was never written there either. 20261120-133703: `host save: saved (mode
  0x01654C01)`, then `store writes 2 -> 2` and no save after the share's in the host's log (no "saving" hitch, no box,
  no store write); 20261119-101851: refused by the older box check ("waiting for the previous save's box"), also
  `2 -> 2`. Every other session_restart in the lab logs of rel/0.7.16 went `n -> n+1`. The next session loaded the
  share's save, which held the same world, so the test passed on a save that was lost.
* So the w61 fix is NOT a regression: the quit save was dropped silently before it (the bug BUG-1365 describes), the
  fix made coop.save say so. The bug is in the test helper: twoinst.session_restart asked once and counted the answer
  "saved" as the save (bookkeeping, not the game's state).
* The same class in other tests (one coop.save, the answer only printed, then a check on "the newest save"):
  civ_own_species / civ_own_species_hostmode were refused the same way in 20261121-003202 and their check read the
  older save ("saved 14 s ago") and passed; tribe_epic was refused and its check after the save ran with no save.

## Fix
Tests only (no mod change, `world.save_allowed` kept as it is):
* twoinst.save_taken(con, secs): `coop.save` again every 0.5 s while the mod refuses it for now (prints each new
  reason), returns the answer, the refusals and the time of the ask the game took. One shared method for the class.
* twoinst.session_restart asks with save_taken; info['saved'] is true only when the save was posted AND the store was
  written by it (writes > before): a dropped save now fails `the host saved its game before quitting` (stricter).
* twoinst.save_fresh(profile, since, secs): the newest planet file of the profile is newer than the request.
  civ_own_species checks it before it reads "the newest save" (new check `the host's game wrote this save`).
* save_taken in own_editor_drop (its own BUG-1365 loop replaced), civ_own_species, civ_own_arrival,
  tribe_own_outfitter, tribe_own_species, tribe_epic, migration, space_captain_none and the guest-save tests
  content_civ, civ_save_strip, space_save_strip, tribe_save_strip.

## Verification
Open, waiting for the lab run (no lab runs today, owner rule). FAIL run: 20261121-003202 (species_line). Lab:
`SPORE_DETECTOR=20 python3 tools/lab.py run species_line own_editor_drop civ_own_species civ_own_species_hostmode
tribe_epic civ_owners_session tribe_owners_session --lane N --lane M --retry 0 --no-build`: all must PASS;
species_line must show `refused for now` then `store writes 2 -> 3`. The twin of the mod's side stays
saved_box_twice_off (BUG-1365).

## History
- 2026-11-21: opened (fix/w62-saves): the logs of three runs show the quit save was always lost; test helpers save_taken / save_fresh.
