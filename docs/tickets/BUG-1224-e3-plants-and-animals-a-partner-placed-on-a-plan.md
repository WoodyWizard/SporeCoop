---
id: BUG-1224
title: E3: plants and animals a partner placed on a planet are not where it placed them in the other game (plants only in the placing game)
status: fixed
kind: desync
stage: Space
reported: 2026-11-17
source: plan
role: 
versions: 
fixed_in: unreleased
branch: fix/w51-space3
commits: ed2043b6
tests: space_eco, space_eco_live
signature:
docs: docs/re/space.md#space-eco-spots, docs/re/space.md#space-ecosystem
related: BUG-1148, BUG-1155, BUG-1220
---

## Report
BUG-1220's key frames of the ecosystem sync (E3, BUG-1148), fix/w51-visual runs 20261116-095532 / -101029 / -102211
(frames space_eco/01_eco_landed_*): from the same camera, a big tree the guest planted stood only in the guest's game;
the herd the host saw was not where the guest's was; `eco place` on the guest often showed no animal of the species
(#0 in 2 of 3 runs). The species lists and the stabilized level matched.

## Root cause
Observed (Ghidra + runtime lane 7, docs/re/space.md#space-eco-spots):
* Plants: a planet's flora is drawn per terrain grid cell (96 cells, one plant species per size class in each);
  PlanetEco_AddPlant puts the species into the cell of its point. A game away from the planet got only the record's
  lists (K_ECO), so on its landing its cell at the guest's spot had no such plant: the trees stood only in the
  guest's game. Given the guest's plant through AddPlant at the guest's point, the host's game shows the same trees.
* Animals: the herd spawn the sync used (PlanetEco_SpawnHerd 0xACF320) makes one EPIC-size creature (hp 3500, a
  giant), not a normal animal; and each game's planet entry makes the herds of a planet's species at spots of its own
  choosing on every landing (also the placing game: its herd is elsewhere on its next visit), so a herd cannot stand
  at the drop spot in another game beyond the moment of the drop.
* `#0` on the guest: the test seam (`coop.space eco place`) called the placement function only; a real drop also
  makes the creature at the beam point (PlanetEco_DropAnimal 0xAD0450). Test-only.

## Fix
space_eco.cpp: each species keeps the sender's spot (already in K_ECO, wire unchanged; a known species placed at a new
spot is sent too). A game on that planet puts each spot's species into the cell of the spot (AddPlant / AddAnimal,
player 0), also a game that lands later (3 s after the landing, once per spot and terrain), and makes the cargo drop's
own creature (PlanetEco_DropAnimal, on the ground under the point) where it has none of that species. The test seam
makes the drop's creature too. Switch `space.eco_spots` (off: lists only, the epic spawn, as BUG-1148 shipped).
Not possible (documented): a herd at the drop spot on later visits (the game itself chooses its herd's place per game
and landing); a plant's spot across a save and load was not checked.

## Verification
* FAIL with `SPORE_FIX_OFF=space.eco_spots` (SPORE_LAB_FIXOFF): 20261117-125031-2 (lane 7): space_eco `5304849A cell
  6/6 host [] guest ['5304849A']` and the host's creatures of the 3 species hp 3500 (epics); space_eco_live: the
  epics.
* PASS: 20261117-124504 space_eco (lane 7, before the size check: cells equal, frame
  space_eco/02_eco_landed_spot_{host,guest}.jpg the same trees).

## History
- 2026-11-17: opened.
- 2026-11-17: opened and fixed on fix/w51-space3 (cells of the spots, the drop's creature instead of the epic spawn).
