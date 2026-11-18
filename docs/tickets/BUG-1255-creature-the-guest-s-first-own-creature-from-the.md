---
id: BUG-1255
title: Creature: the guest's first own creature from the evolution editor asks to replace the host's species
status: fixed
kind: bug
stage: Creature
reported: 2026-11-18
source: owner: players' video, second analysis
role: guest
versions: <0.7.15
fixed_in: 0.7.15.2
branch: fix/w53-cellvote
commits: 9b1dca20 37042f38 021477ff
tests: evo_screens_switch, evo_screens_switch_slow
signature: the guest's own first Creature editor (landfall FD4902BD) shows "Save '<host species>' as a new creation or replace" on done
docs: docs/re/creations.md#creations-own-editor-world-species
related: BUG-1222, BUG-1240, BUG-0756
switch: creations.own_editor_no_replace
---

## Report
A players' video: the guest accepts the evolution, switches to its own creature, and on the check mark gets the game's dialog to replace the host's species with its own (it had to pick "save as a new object"). BUG-1222 (0.7.15) covers guests on their own creature; the first pick of an own creature through the evolution of the host's world may still show the dialog.

## Root cause
Observed (lab 20261118-124934 and the BUG-1240 runs of fix/w52-evoeditor, shot evo_switch_guest_dialog.png): the
guest's own first Creature editor (the landfall editor after Cell, also an Evolve-now offer) is the mod's own editor
(open_own_editor) on the avatar's species, which for a guest without a creation of its own in play is the world's
species, the host's creation. BUG-0756 gave every own editor the edit-a-creation bytes (+0x39 = 1, +0x3C = 0): done
after an edit then asks "Save 'Sporoo' as a new creation or replace 'Sporoo'", and Replace would overwrite the host's
species in the guest's profile. Not a BUG-1222 case (that one is a guest playing its own creation).

## Fix
`creations.own_editor_no_replace` (vote.cpp build_editor_request / open_own_editor): a guest's own editor that starts
from the world's species (the guest does not play one of its own creations) keeps only the constructor's red cross
(+0x3B = 1) and the evolution's +0x39 = 0 / +0x3C = 1: no replace dialog, done saves a new creature (its price taken
as before). A guest playing its own creation keeps BUG-0756's dialog. `coop.vote` counts it. Off: the dialog (0.7.15).

## Verification
`evo_screens_switch` (cell fixture, a guest without creations, real clicks: History's check, I'm ready, legs dragged in
both editors, done): after the guest's done the save dialog must not be up (its golden buttons, `save_dialog_up`), and
each player plays its own creature afterwards.
* FAIL: out/lab/20261118-124934 (`SPORE_FIX_OFF=creations.own_editor_no_replace,core.evolve_notice`, lane 2): the dialog
  (frame 04_guest_save_dialog_guest).
* PASS: out/lab/20261118-125715 (evo_screens_switch_slow, lane 3: probe 0.00, own creatures; its only failure is the
  lab's sync-check gate under lowband, in every run of that test); out/lab/20261118-124955 (lane 3: frame
  04_guest_save_dialog_guest without a dialog, own creatures; its dialog check still read the mode then, fixed after).
* Regression: own_editor_cancel, editor_sync_guest, guest_editor_locks OK (out/lab/20261118-125348).

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w53-cellvote): reproduced at the landfall editor, fixed with creations.own_editor_no_replace.
- 2026-11-18: released in 0.7.15.2.
