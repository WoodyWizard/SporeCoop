---
id: BUG-9285
title: Lineages cells: giant player cells in the Cell pool (My Creations cells on every level); only the players' current cells, at their own level
status: open
kind: bug
stage: Cell
reported: 2026-11-20
source: owner 2026-11-20 (friends build test)
role: host
versions: 0.7.15.94
fixed_in:
branch: feat/lineage-cells
commits:
tests: lineage_current
signature:
docs: docs/re/cell.md#cell-lineage-current
related: BUG-9275, BUG-9284, BUG-9287
---

## Report
The owner's test of the friends build 0.7.15.94 (host, new Cell game): "many giants in the Cell stage, review it
completely". The host's log: "Lineages cells: 8 player cell(s) for the Cell pool, 1 left out as a player's current
cell", later 9 and 10 (each own evolution added the previous version of the host's cell). Intended (the owner): only
the PLAYERS' current cells (each player's own species) appear as NPC species, at normal cell sizes and amounts, never
giant.

## Root cause
BUG-9275 (Lineages BUG-9111) gave about one in six spawns of EVERY native creature species (PopulateMarker, any AI) the
model of one of the host's My Creations cells or of the cells the guests sent from their My Creations; a current cell
was explicitly left out. The pool holds the species of three levels around the avatar (docs/re/cell.md#cell-own-species)
and the big species of the levels above, so player cells were drawn at those species' sizes: giants. Every evolution
of the host added its old cell (the Cell editor saves each version to My Creations).

## Fix
`cell.lineage_current` (on; lineage_cells.h `current_only`): the pool's candidates are only the partners' CURRENT cells
(creations::player_key of each player, the creations stream that already gives every game every player's cell), and
only spawns of the player species' native spread are swapped (the game puts 6-8 cells of the avatar's own cell at the
avatar's level around it): in a partner's own tile pass (cellsync_tiles.cpp, the host populates every partner's area)
that partner's cell, in the host's pass any player's (the host's own stays native, one share per player). No other
species, no other size, no My Creations cells; a guest no longer sends its My Creations cells (kind 9). `coop.lineage`
prints `current_only`, `player_picks` / `player_native` and each player-model cell's level (+0x358) and the avatar's
food level. Off (`coop.fix cell.lineage_current off`): 0.7.15.94.

## Verification
Built (WSL, make all). In-game test on lane A: see History.
Test: tools/tests/coop_lineage_current.py (suite lineage_current; twin lineage_current_off).

## History
- 2026-11-20: opened.
- 2026-11-20: fix on feat/lineage-cells.
