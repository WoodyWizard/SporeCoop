---
id: BUG-0872
title: Feature idea: the players' cells swim in the Cell stage as native NPC species
status: open
kind: feature
stage: Cell
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 lineages43-dev, BUG-9111 (lineage_cells.cpp)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0802
---

## Report
Player request in the fork: cells the players made (older versions, partners' cells) swim in the Cell stage as
native NPC species with Maxis AI. The fork swaps about one in four native creature spawns for a player cell.

## Root cause
Not a bug, a new feature.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages lineages43-dev, BUG-9111, mods/coop/lineage_cells.cpp, creations.cpp host_cell_out / model_signature, modes.cpp model chooser); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* RE (fork): the model of a native spawn is decided once inside CreateCellObject by GetModelKeyForCellResource
  (hooked); the look and diet come from the cell's +0xFC. The cell resource ref (+0x108) is shared per species.
  AI 0x1006 (prey and threat) picks prey with the model's own diet (IsPrey -> CanEat): a carnivore mouth hunts,
  a herbivore grazes. cCellGFX::PreloadCreature 0xE64980 (cdecl void(u32 instance)) preloads a cell model.
* Host pool rebuilt every 15 s outside the frame: own My Creations cells + cells of lobby players (and with an opt-in,
  received ones); never a player's current cell (compared by FNV of the model record bytes, not the key).
* Every guest gets the records first (an own-species bundle, registered under its own key) and a cell is used only
  8 s after all guests have it; a guest never builds a model it lacks (0xE83EC8, BUG-0004 class).
* 25 % of native creature spawns inside a population marker (host or offline only) take a pool cell's model key;
  not the player's own species, not species seen dropping a part token (learned in SpawnLoot, so part unlocks
  keep working). Limits: the cell keeps the native species' identity (.cell), an unpainted cell is a pale membrane.
* Lobby setting "Player Cells: On/Off" (host). Overlap: plan-0.7.12 F3 (a session setting).

## How to test
Lab, 2 instances, Cell: host with 2 saved older cells; after 3 minutes `coop.pool`-style count: about 25 % of new
native spawns use pool models, on the guest the same cells render with the same model (no fault), the current
cells never appear, a part carrier species is never swapped.

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (feature idea, fork evidence only).
