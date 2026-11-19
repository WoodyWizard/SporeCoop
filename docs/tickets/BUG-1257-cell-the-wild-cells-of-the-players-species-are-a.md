---
id: BUG-1257
title: Cell: the wild cells of the players' species are all the host's design (no second player species in the world)
status: open
kind: feature
stage: Cell
reported: 2026-11-18
source: owner: players' video, second analysis
role: guest
versions: <0.7.15
fixed_in:
branch: feat/w54-cellkin, feat/w54-shims
commits: 1959f57a, eedaf53f, 28ef93c1 696d51e2 48dd3e02
tests: cell_kin_species (coop_cell_kin_species.py)
signature:
docs: docs/re/cell.md#cell-kin-species, docs/re/cell.md#cell-kin-per-player
related: BUG-1240
---

## Report
A players' video: dozens of wild copies of the host's cell swim about, none of the guest's design. The host's game
fills the world (Simulator::Cell::PopulateTiles) from the world's one player species and the guest shows the host's
pool. Filed as a feature for the own species per player (0.8 Rivals), docs/known-issues.md#issue-cell-kin-host-species; moved
into 0.7.16 (WP-L1 of docs/plan-0.7.16.md, owner decision of 2026-11-18: each player's kin swim near that player, a mix
where the players are together).

## Root cause
The host's population (PopulateMarker -> CreateCellObject -> GetModelKeyForCellResource) answers a kind-5 model entry
with the world's one player cell `S+0x10` (Ghidra, docs/re/cell.md#cell-kin-species); the guests mirror the host's pool,
so every kin was the host's cell.

## Fix
feat/w54-cellkin, mods/coop/cellsync_kin.cpp (docs/re/cell.md#cell-kin-per-player): a depth counter around
PopulateMarker (also covers the level change's repopulation), a CreateCellObject hook that keeps the new gameplay
cell's position, and a filter in the modes.cpp model-key hook: a kind-5 answer becomes the cell of the player whose view
centre is nearest the new cell when that is a partner with a cell of its own known here (`creations::key_known`). Also
in a guest's own pool while the host is away (BUG-0205). Nothing in creature.mode host. The player's cell key comes from
`creations::player_key` through a `// WP-0 shim` until species_line (BUG-1270) is merged. `coop.pool kin [r]` for the
tests. Switch `cell.kin_species` (off: 0.7.15, every kin the host's cell).

## Verification
cell_kin_species (coop_cell_kin_species.py --editor, Cell fixture, food 924): PASS run and the FAIL twin
(SPORE_FIX_OFF=cell.kin_species) wait for the night lab (owner's rule of 2026-11-18: no lab runs by agents). A first
run at food 0 (lab 20261118-180414, before the test raised the food) showed that level 0 has no kin at all.

## History
- 2026-11-18: opened.
- 2026-11-18: researched on plan/0.7.16 (docs/re/cell.md#cell-kin-species); planned as WP-L1 of docs/plan-0.7.16.md.
- 2026-11-18: moved from 0.8 into 0.7.16 (WP-L1); work on feat/w54-cellkin.
- 2026-11-18: feat/w54-shims: the WP-0 shim `own_cell_key` reads the species line (species::key(id, Stage::Cell); a premade cell counts, species_line_core.h creation_counts); `coop.pool kin` prints `line=`; the test also checks the guest's Cell line on both games. Still open: the night run (cell_kin_species lane 2, twin lane 3).
