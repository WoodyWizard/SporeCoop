---
id: BUG-1257
title: Cell: the wild cells of the players' species are all the host's design (no second player species in the world)
status: fixed
kind: feature
stage: Cell
reported: 2026-11-18
source: owner: players' video, second analysis
role: guest
versions: <0.7.15
fixed_in: unreleased
branch: feat/w54-cellkin, feat/w54-shims, fix/w57-cellkin
commits: 1959f57a, eedaf53f, 28ef93c1 696d51e2 48dd3e02 dfba6556 0438065a 5c4a8b06
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
cell_kin_species (coop_cell_kin_species.py --editor, Cell fixture, food 924, lane 10):
* First night run (rel/0.7.16, out/lab/20261119-095414): FAIL, a wrong test, not the mod. The host's pool held kin of
  both cells (own=19 partner=90), but the test took one view width as `5 / world scale`, 0.0003 units at level 10
  (scale 15000): "apart" was 0.7 units and "near" 0.0006 units, so every "kin near a player" was empty; the AI sample
  ("0 of 4 moved") was taken with the players together; the guest's own cell editor never closed (the premade
  BAF1AD40 is not saved by the Cell editor's Done). Kin are spawned (docs/re/cell.md#cell-kin-test-units).
* The test now measures in the game's own visible box (`coop.pool kin` prints `box=`, 20 units at every level), uses
  the wizard premade E219E5D2, gives invulnerability again after the guest's rebuild, samples each check (the kin are
  few and swim off), and exempts near the host only the kin made while the players were together (owner decision
  2026-11-18: a mix where they are together; `key=` names each kin by its host pool index on both games). Reasons in
  the test's comments and docs/06-testing.md#testing-cell-kin-units. Runs on the way (each found one of these):
  20261119-111537, -113757, -114512, -115019, -115626, -120210.
* PASS: out/lab/20261119-121933 (fix/w57-cellkin 5c4a8b06): the guest's kin near the guest on both games, the host's
  near the host, the guest's kin swim as the host's (same resource), both places, after the editor the new cell's kin
  (7CB39B69), no guest key in the host's save. Key frames `apart_kin_close` / `after_editor_kin_close` show a kin of
  each player's look beside it (looked at).
* FAIL twin (SPORE_FIX_OFF=cell.kin_species): out/lab/20261119-122435 (5c4a8b06, the same test as the PASS): every
  kin near the guest is the host's 4DA56CA5 in the host's pool and on the guest's screen, no guest kin for the AI
  check, none at the guest's place, after the editor again only the host's (7 FAIL lines). Also 20261119-121254 (the
  test one commit earlier): the same.
## History
- 2026-11-18: opened.
- 2026-11-18: researched on plan/0.7.16 (docs/re/cell.md#cell-kin-species); planned as WP-L1 of docs/plan-0.7.16.md.
- 2026-11-18: moved from 0.8 into 0.7.16 (WP-L1); work on feat/w54-cellkin.
- 2026-11-18: feat/w54-shims: the WP-0 shim `own_cell_key` reads the species line (species::key(id, Stage::Cell); a premade cell counts, species_line_core.h creation_counts); `coop.pool kin` prints `line=`; the test also checks the guest's Cell line on both games. Still open: the night run (cell_kin_species lane 2, twin lane 3).
- 2026-11-19 (fix/w57-cellkin): the first night run failed on the test (view width from the world scale, the premade BAF1AD40 whose editor does not close); test fixed, PASS 20261119-121933 and FAIL twin; fixed.
- 2026-11-19 (fix/w57-cellkin): PASS again on the tree merged with rel/0.7.16 (e10cac3d): out/lab/20261119-123540.
