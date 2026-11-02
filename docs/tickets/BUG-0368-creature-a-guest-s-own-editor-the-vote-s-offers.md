---
id: BUG-0368
title: Creature: a guest's own editor (the vote's) offers every part, also the ones its species has not unlocked
status: fixed
kind: bug
stage: Creature
reported: 2026-11-01
source: player reports 2026-11-01 (host GOG 2024 + Steam guest, 0.7.3.3)
role: guest
versions: 0.7.3.3
fixed_in: 0.7.3.4
branch: fix/w30-progress
commits: cfce326
tests: guest_editor_locks (tools/tests/coop_guest_editor_locks.py)
signature:
docs: docs/re/creations.md#creations-own-editor-items, docs/06-testing.md#testing-own-editor-locks
related: BUG-0278
switch: creations.own_editor_items
---

## Report
Host mark (Creature): the guest "has every single customization unlocked since the beginning of
creature stage": the guest's creature editor offered every part, also ones the species had not
collected.

## Root cause
Observed (Ghidra GOG 2024 + lane 10): the guest's own editor after a vote is opened by the mod
(`open_own_editor`: EditorRequest with the editor id and the species key only). The game's own
evolution request (0xD42E50, at 0xD42F73) also sets request +0x94 to the species' collected parts
(`cPlayer+0x10E8`, a cCollectableItems). The palette's check 0x5C6820 treats a request without it as
"every part available". On lane 10 the guest's editor request had +0x94 = 0: 0 of 312 unlockable
parts locked, a full palette (screenshot).

## Fix
`creations.own_editor_items` (coop.cpp `open_own_editor`): in Creature the request gets the guest's
`cPlayer+0x10E8` through the game's intrusive pointer assignment 0xB5FAB0 (AddRef, Release of the
old). New symbols (GOG 2024): `SporeCoop::IntrusivePtr_Assign`, `Simulator::cCollectableItems::IsUnlocked`.
Test command `coop.editor`: the open editor's request (editor mode 0xDBDBA1 from the mode manager's
list, +0x1CC), its items and how many unlockable parts its palette shows locked.

## Verification
FAIL: SPORE_FIX_OFF=creations.own_editor_items guest_editor_locks, lab 20261101-080312 (lane 10: items 00000000,
0 of 312 locked; also 20261101-075820 with the first version of the command).
PASS: guest_editor_locks 2/2, lab 20261101-080425 (items = collected parts, 258 of 312 locked, "done" leaves the
editor, no fault). By hand on lane 10: screenshots of the palette with the fix (locked silhouettes)
and without (every mouth offered).

## History
- 2026-11-01: opened from the host's mark in the 2026-11-01 reports.
- 2026-11-01: fix/w30-progress: the own editor request gets the collected parts; fixed (unreleased).
- 2026-11-02: released in 0.7.3.4.
