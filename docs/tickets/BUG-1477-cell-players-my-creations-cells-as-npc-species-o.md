---
id: BUG-1477
title: Cell: players' My Creations cells as NPC species (off-by-default cell.lineage_cells, a guest's cells only with its consent; idea from PR #211 by IltonBJSilva)
status: fixed
kind: feature
stage: Cell
reported: 2026-11-23
source: plan, PR #211
role: 
versions: 0.7.17
fixed_in: 0.7.17
branch: feat/w81-cell
commits: e89fe984, edc53a14
tests: lineage_cells (coop_lineage_cells.py); lineage_cells_ui; twin lineage_cells_off
switch: cell.lineage_cells
signature:
docs: docs/re/cell.md#cell-lineage-cells-0717
related: BUG-1257, BUG-1333, BUG-0975
---

## Report
About 17 % of native spawns from the players' own cells, one chain with our kin filter hook (GetModelKeyForCellResource 0xE65640); creations 1.2 MSG_CREATION kind 12 (reserved in WP-0); the cell pool is not saved (check by a save parse). docs/plan-0.7.17.md item 9. Credit IltonBJSilva (PR #211).

## Root cause
Feature (PR #211 by IltonBJSilva, ported, not merged): #211 reused MSG_CREATION kind 9, had no old-peer guard, sent guests' cells without asking and was on by default; the owner's test of its fork build showed "many giants" (a player cell on a native species of the levels above is drawn large). #211's RE taken: GetModelKeyForCellResource 0xE65640 inside PopulateMarker decides a native spawn's model, the prey-and-threat AI 0x1006 picks prey by the model's mouths (+0x1250 / +0x1251), the cell pool is not saved, a cell's bem is the same bytes under every key, cCellGFX::PreloadCreature 0xE64980 (group global *(0x015A7D0C) on GOG 2024).

## Fix
Session setting `cell.lineage_cells` (Cell, off: settings::kLineageCellsDefault) and fix switch `cell.lineage_cells` (lineage_cells.{h,cpp}, rules in lineage_cells_logic.h, unit-tested): each player's My Creations cells (newest 16, not the one it plays, no premade) go to every game as line bundles (creations::line_pack / line_register, shared key 0x7B + content hash) in MSG_CREATION kind 12 (creations 1.2; total 0 = withdrawn); a guest offers only after ticking its co-op window box "[ ] My old cells as wild species" (never by default, reset every session); the host relays to peers of creations 1.2 and to late joiners; in the population (cellsync_kin.cpp, one chain with the kin filter) a native creature spawn (cell type 2 with an AI) at the host's level or below, not the player species, takes a pool cell of a fitting diet in 17 % of the spawns (`coop.lineage share`), only while every peer reads creations 1.2 and has had the cell 5 s; every registered cell is preloaded in Cell (PreloadModel, PreloadCreature). `coop.lineage [share n] [consent on|off]`; `coop.pool kin 0` prints `kin lineage ... live=`. Credit IltonBJSilva (PR #211).

## Verification
Open, waiting for the lab run: pending night run: lineage_cells (PASS expected: both players' old cells in the host's pool, the guest registered them, after a rebuild >= 3 wild old cells live on each game, consent off withdraws the guest's, a fresh host save holds no 0x7B key; key frame), lineage_cells_ui (the box in the co-op window), lineage_cells_off (FAIL expected). Old peer: unit test lineage_rules (host_swaps, offers); no lab lane has a 0.7.16.1 build.
0.7.17 release lab: PASS: lineage_cells (lab 20261124-020506), lineage_cells_ui (lab 20261124-020506); FAIL (fix off): lineage_cells_off (lab 20261124-013101).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 9 (WP-L2), branch feat/w81-cell.
- 2026-11-23: implemented on feat/w81-cell with its lab test and twin (switch cell.lineage_cells), waiting for the night run.
- 2026-11-24: released in 0.7.17.
