---
id: BUG-1476
title: Cell: a guest without own cells gets the three diet premades in the "My cell" picker (#210 small part by IltonBJSilva)
status: open
kind: feature
stage: Cell
reported: 2026-11-23
source: plan, PR #210
role: guest
versions: 0.7.17
fixed_in:
branch: feat/w81-cell
commits: e89fe984
tests: cell_diet_premade (coop_cell_diet_premade.py); twin cell_diet_premade_off
switch: cell.diet_premades
signature:
docs: docs/re/cell.md#cell-diet-premades
related: 
---

## Report
A guest with no cells in My Creations gets the three diet premades (E219E5D2 / 252A132F / 0C176D89) in the picker; take #210's RE (diet flags of 44 cell models, Cell_GetConsequenceTrait 0xE5C9D0). Test: an empty test profile picks a carnivore premade, both games show it. docs/plan-0.7.17.md item 9. Credit IltonBJSilva (PR #210).

## Root cause
Feature (PR #210 by IltonBJSilva, its small part): a guest without cells in My Creations saw "My cell: the host's species" with no arrows (BUG-0598's hint) and could only play the host's cell. #210's RE (diet flags of the game's 44 cell models, Cell_GetConsequenceTrait 0xE5C9D0 already in the sym file from feat/w70-history): the Cell wizard's herbivore and carnivore premades E219E5D2 / 252A132F and the game's cell with both mouths 0C176D89 (no omnivore card).

## Fix
`cell.diet_premades` (creations.cpp diet_premades): for a guest whose My Creations has no cell, options(kCell) are the three premades this game has (names "Herbivore / Carnivore / Omnivore (game cell)"); own_option and mine() know them as the guest's own choice; the bundle goes without records (every game has the model), as any premade. The co-op window's row says "game cells: 3" (adapter creation_premades). Test knob `coop.creations hideown <kind> on|off` (My Creations read as empty, as an empty test profile). Full own start (name, body colour): Rivals 0.8. Off: 0.7.16.1. Credit IltonBJSilva (PR #210).

## Verification
Open, waiting for the lab run: pending night run: cell_diet_premade (PASS expected: the guest's own cells hidden; its row offers game cells with arrows; ">" picks 252A132F; the guest's avatar model and the host's puppet of it are 252A132F; key frame), cell_diet_premade_off (FAIL expected: no arrows, no choice).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 9 (WP-L1), branch feat/w81-cell.
- 2026-11-23: implemented on feat/w81-cell with its lab test and twin (switch cell.diet_premades), waiting for the night run.
