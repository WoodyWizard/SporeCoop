---
id: BUG-0756
title: Creature: a guest's own editor lost its red cross and save dialog with creations.editor_request (no way to leave without the edit)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-08
source: owner decision 2026-11-08 (after BUG-0720)
role: guest
versions: 
fixed_in: unreleased
branch: fix/w46-editor-cancel
commits: b745a1f4
tests: own_editor_cancel (coop_own_editor_cancel.py), editor_request, legless_guest_*
signature:
docs: docs/re/creations.md#creations-own-editor-cancel
related: BUG-0720, BUG-0283, BUG-0278, BUG-0516
switch: creations.own_editor_cancel
---

## Report
Found by E1 (fix/w46-e1-merge): on rel/0.7.11 after the K2b merge editor_now failed 0/4, the guest stayed in its own
editor after a click on the red cross. The own Creature editor had no red cross any more, its Esc menu has no exit,
and done after an edit leaves without the Save New / Replace / Don't Save dialog: a guest cannot leave it without
keeping the edit. Not released (0.7.10 had the cross). Owner (2026-11-08): the 0.7.10 own editor stays.

## Root cause
Observed: creations.editor_request (BUG-0720) gives the own request the game's evolution flags +0x39 0, +0x3B 0,
+0x3C 1; cEditor::OnEnter shows the cross window 0x90439D7C from +0x3B (0x58FBE7 -> 0x5DCC80) and sets other
windows from +0x39 (0x5DCDC0). Lane 4: the cross with +0x39 / +0x3B at the constructor's 1, none with the game's
values or with +0x3C alone (docs/re/creations.md#creations-own-editor-cancel).

## Fix
`creations.own_editor_cancel` (coop.cpp build_editor_request): the own editor keeps the constructor's +0x39 1 /
+0x3B 1 / +0x3C 0; the rest of BUG-0720 (the other flags, the consequence traits, +0x94) stays the game's.
editor_request allows the three bytes for the own kind. The tests moved to the check mark by fix/w46-e1-merge
click the cross again.

## Verification
`own_editor_cancel` (coop_own_editor_cancel.py: the cross window 0x90439D7C shown at the cross, one click leaves).
* PASS 2/2: out/lab/20261108-101110 (lane 4, with editor_request 2/2, editor_now, editor_sync_guest / host,
  evolve_in_place_cell / creature, evolve_key, guest_editor_hold, all 2/2).
* FAIL with `SPORE_FIX_OFF=creations.own_editor_cancel`: out/lab/20261108-104950 (no cross window, the guest stayed
  in its editor).
* legless_guest, _cancel (the cross), _replace and _dontsave (their dialogs), _done: 10/10, out/lab/20261108-101110-2.
* editor_request with `SPORE_FIX_OFF=creations.editor_request` still FAILS: out/lab/20261108-104820 (vec70 0/4).

## History
- 2026-11-08: opened and fixed (fix/w46-editor-cancel).
