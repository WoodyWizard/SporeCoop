---
id: BUG-0455
title: a player's game crashes at 0xE83EC8 in the Cell editor when the host leaves the evolution (editor) screen, 0.7.3.4 (BUG-0004 signature again)
status: fixed
kind: crash
stage: Cell
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: guest
versions: 0.7.3.4
fixed_in: 0.7.4
branch: fix/w40-crashes
commits: 0074b298
tests: cell_unknown_model
signature: dc04aee5:0xE83EC8 read 0x9C (CreateCellObject from cellsync::guest_state with an unknown model key)
docs: docs/re/cell.md#cell-unknown-model, docs/06-testing.md#testing-lost-objects
related: BUG-0004
---

## Report
Another player (forum post 'Cell Editor 0.7.3.4 Crash', with a video): crash dump crash-20261102-144335.dmp: 0xC0000005 at SporeApp.exe+0xA83EC8 (base 0xD90000) = dc04aee5 0xE83EC8, read of 0x9C; 'Game crashed when host leave evolution tab'. 0xE83EC8 is BUG-0004's signature (editor desync, guest crash, fixed long ago): a sibling path or regression. No report zip (the player could not send it); the .txt was only a Discord preview.
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (dump crash-20261102-144335.dmp, symbolized with a v0.7.3.4 build): the guest's main thread, coop.dll `cellsync::guest_state` -> `modes::cell_create` -> CreateCellObject (0xE74ED9) -> 0xE6CAF7 -> 0xE69726 -> 0xE67A41 (GetCellModelData: the model data of the forced key, built on first use) -> 0xE83E30: Editors::cEditor::LoadCreatureData returned nothing, `[0+0x9C]` read at 0xE83EC8. Lab: a host cell sent with a model instance the guest lacks faults the guest at 0xE83EC8 with the same chain; it made no further mirrors.
Inferred: after the host left its Cell editor, the host's cells of its new creation reached the guest before that creation's records (BUG-0004 fixed this for the host's puppet only; the NPC mirrors of cellsync were another path).

## Fix
mods/coop/modes.cpp: every forced model key is checked with creations::key_known first; cell_create makes no cell of an unknown creation (cellsync tries again with the next state round, so the mirror comes once the creation's records are here), cell_spawn (puppets) falls back to the own avatar's model. Switch `coop.fix cell.unknown_model off`.

## Verification
coop_cell_unknown_model.py (suite cell_unknown_model): FAIL with SPORE_FIX_OFF=cell.unknown_model: out/lab/20261103-144746 (guest fault at 0xE83EC8, no mirrors after it); PASS: out/lab/20261103-144842.

## History
- 2026-11-02: opened.
- 2026-11-03: root cause (cellsync mirrors with an unknown model key), fixed on fix/w40-crashes (cell.unknown_model).
- 2026-11-03: seen again in a player report 2026-11-02 (0.7.3.4, Steam guest, reports 20261102-231251 and -231730, two crashes, same chain: coop.dll 0.7.3.4 +0xCD31E CreateCellObject <- +0x108F7 cellsync::guest_state; 'host is back' from its Cell editor 0.07 s before the fault; the player: 'when the host goes from the editor to the game and the guest opens the editor at the same time, the guest crashes'), fixed in 0.7.4 (unreleased).
- 2026-11-03: released in 0.7.4.
