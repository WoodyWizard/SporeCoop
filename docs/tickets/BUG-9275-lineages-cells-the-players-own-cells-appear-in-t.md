---
id: BUG-9275
title: Lineages cells: the players' own cells appear in the Cell stage as NPC species
status: open
kind: feature
stage: Cell
reported: 2026-11-20
source: Lineages port (PR #196)
role: 
versions: 
fixed_in:
branch: feat/lineage-cells
commits:
tests: tools/tests/coop_lineage_cells.py (suite lineage_cells)
signature:
docs: docs/01-architecture.md#architecture-lineage-cells, docs/re/cell.md#cell-lineage-cells, docs/re/creations.md#creations-lineage-cells
related: BUG-9261, BUG-9265
---

## Report
The owner (2026-11-20): "when you go up in the Cell stage, others like you start to appear": the players' own cells
should appear in the Cell stage as NPC species, as the game's own species do. Lineages has it (PR #196, BUG-9111,
roadmap 43, mods/coop/lineage_cells.{h,cpp}).

## Root cause
A feature: the Cell pool only spawned the game's native species.

## Fix
Ported from Lineages onto our Cell split files and creations pipeline:
* `lineage_cells.{h,cpp}` (host): the candidates every 15 s outside the frame: the host's own My Creations cells and
  the cells each guest sent from its My Creations (new: `creations::guest_cell_out`, MSG_CREATION kind 9, creations
  family 1.1; Lineages took them from its Cache library, which is not ported); a cell is known by the signature of its
  bem record, so a player's current cell (host S+0x10, a guest's creation of the stream) is never one; the diet of each
  model (+0x1250 / +0x1251). Every guest gets each candidate's records (`creations::host_cell_out`, kind 5 with a cll
  key, remapped to 0x7D + hash on the guest, `cell.lineage_remap`); a cell is used 8 s after every guest got it.
* `cellsync_lineage.cpp`: the chooser on GetModelKeyForCellResource (modes.cpp): inside PopulateMarker on the host, a
  creature .cell (not the player's species, not a part carrier) takes a fitting player cell in 17 % of its spawns; the
  guest's mirrors use its own key of the model; preloads (PreloadModel, cCellGFX::PreloadCreature, sym entry added).
* The session setting `cell.lineage_cells` (Live, default on, coop.ini lineage_cells), `coop.fix cell.lineage_cells`,
  `coop.lineage [share <n>]` (status; tests).
Changes from PR #196: no Cache / Foreign Creations; guest cells reach the host by kind 9; the signature hashes the bem
record only (Lineages hashed every record but the metadata, so a cell with pictures never matched its stream copy and
Lineages needed an author / name match); the remap of cll keys behind its own switch.

## Verification
Windows, lane A (instances 1/2, Steam build), a new herbivore Cell game "Talviorus" made from the galaxy menu by the
host (dev1, given 4 of dev2's 8 cells in its My Creations); the guest (dev2) chose its newest own cell 29284693:
* tools/tests/coop_lineage_cells.py through a Windows runner (agentH/run_test.py: the lab's twoinst.start is Linux
  only): every check passed: 7 candidates (4 from=host, 3 from=guest:1; the guest's current 20219D41 = 7B08F5A9 left
  out), the host's pool held cells of host and guest models, the guest drew the same cells from its own 0x7D keys,
  no NPC of a current cell, carnivore models chased meat only, omnivore models meat and plants, setting off: no swap
  (79 -> 79), on again: swaps; no crash report on either game.
* Screenshots: the guest's game with an NPC of one of the guest's other cells next to its own cell; the host's game with
  an NPC of a host cell eating meat.
* A cold host game loading the planet saved with 22 player-model cells in its pool: only native species, no fault (the
  pool is not saved).
* Twin: `coop.fix cell.lineage_cells off` on both games (runner SPORE_FIX_OFF): FAIL (the pools held only the native B0B9BAFF, no swaps); on: PASS (agentH/run1.log, run3.log; run2_fixoff.log).

## History
- 2026-11-20: opened, ported and verified on lane A (feat/lineage-cells).
