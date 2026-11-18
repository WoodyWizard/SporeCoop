---
id: BUG-9111
title: Player cells (Lineages cells) appear in the Cell stage as native NPC species
status: fixed
kind: feature
stage: Cell
reported: 2026-11-10
source: player request (roadmap 43)
role: host
versions: 0.7.3.6-lineages42-dev
fixed_in: 0.7.3.6-lineages43-dev
branch: feat/lineages-43
commits: 0221025e
tests: QA solo host + local guest (coop.pool lineage, coop.lineagecells, screenshots), see Verification
signature: -
docs: docs/re/cell.md#cell-lineage-cells
related: BUG-9104 BUG-9105 BUG-9109 BUG-9110 BUG-9112
---

## Request

"Cells made in the Cell editor, including partners' cells, spawn natively as their own species with
Maxis AI behaviour, alongside Maxis species. Species that carry cell parts stay Maxis so part unlocks
keep working." Player decisions (2026-11-10): cells of the players in the lobby; Lineages Cache cells
only when their author allows passing on (Share Onward); about one in four spawns of a native species
(2A); behaviour follows the mouth; a player's current cell never appears (the native spread of the
own species stays untouched); a host on/off setting, on by default; existing saves from the next spawn.

## Implementation

- `lineage_cells.cpp`: the host's pool, rebuilt every 15 s in the Cell stage (outside the frame): its
  own My Creations cells and `library::pool_cells()` (Cache cells authored by the host, by a player in
  this session, or by a Share Onward author). A cell is left out when its model bytes
  (`creations::model_signature`, FNV-1 of the model record) equal a player's current cell (the host's
  S+0x10, each guest's `player_key`), or, for a Cache cell, when its author and name are a partner's
  current cell's: the Cache's PNG import keeps a cll record (13 KB) where the editor and the creations
  stream keep a bem record (1.9 KB), so the bytes of one cell never match across the two. `ensure_model` vets the parts; `cellsync::model_diet` reads its
  carnivore / herbivore flags; `cellsync::preload_model` loads it for the graphics.
- `creations::host_cell_out`: every guest gets each pool cell's records (kOwnSpecies bundle) and
  registers them under a key of its own (0x7D......); a cell is used only 8 s after every guest got it.
- `modes::set_cell_model_chooser` / cellsync `choose_model`: for a native creature spawn inside a
  population marker (host or offline, never a guest) 25% take a pool cell's model key. Not touched:
  the player's own species (native model = S+0x10) and species seen dropping a part token
  (`h_SpawnLoot` learns them, kept in `mods/coop-cell-part-carriers.txt`). The prey-and-threat AI
  (0x1006) picks prey with the model's own diet, so any cell fits it; other AI types need the native
  model's diet.
- Guest mirrors: `local_species` turns the host's key into the guest's; a model the guest does not have
  is never built (`unknown_mirror`, it faulted at 0xE83EC8, BUG-0004) and the guest preloads the model.
- Host setting: lobby "Player Cells: On/Off" under Send World (tooltip; the host's difficulty line now
  also names the transfer: "Difficulty 100%, Fast transfer"), coop.ini `[cell]
  lineage_cells`, console `coop.lineagecells [on|off]`, kill switches `coop.fix cell.lineage_cells`
  and `cell.mirror_model`.

## Limits

- A custom cell keeps the native species' identity (.cell): it does not attack its native kin, and
  the species alert counts it as one of them. A cell species of its own needs a cloned .cell (not done).
- A part carrier is learned when one of its kind drops a part token; before that, a carrier may wear a
  player cell's look (the part still drops: loot follows the .cell, not the model).
- An unpainted cell is drawn as a pale membrane in the pool (the game's look, the player's own too).
- A partner's older cell with the same name as its current one is left out (author and name match).

## Verification

Windows QA copy, profile LineagesCellQA, 4 reserved seats, muted:
- three versions of a cell "Lumio" saved in the editor; the pool held the two older ones, the current
  one was left out (`current_skipped`).
- spawns: 11 of 43 considered, then 20 of 134 swapped (about 25%); a learned part carrier skipped 32
  times; Lumio NPCs chased prey and fled threats (`coop.pool lineage find`).
- screenshots: the pink striped Lumio drawn with its paint on the host and on the guest
  (out/qa43/lineage-cell-npc.png).
- guest (instance 8, LineagesDiff41Guest) joined over 127.0.0.1: both cells arrived ("the host's
  Lineages cell 291D4CB8 is 7DDB9676 here"), the guest mirrored the same Lumio NPCs as the host (2 and
  4), `unknown_mirror=0`, no [ERR] on either game.
- soak, both games, host swimming ~3 min: 504 of 2186 native spawns swapped (23%), 525 skips of a
  learned part carrier, guest `unknown_mirror=0`, no [ERR].
- first run with the guest's library: the guest's current cell 'Spoop' came into the pool through the
  Cache (bytes differ, see above); after the author/name rule the pool held only the host's two older
  cells. The lobby toggle: a click turned it Off (`coop.ini lineage_cells=0`, tooltip shown), a second On.
no-fail-run: a new feature.

## History
- 2026-11-10: built and tested on feat/lineages-43.
- 2026-11-10: the Share Onward rule became BUG-9112's Foreign Creations: Cache cells of authors not in
  the session count only when the host's Foreign Creations is on.
