---
id: BUG-9216
title: Party card: the host's picture is blank on a guest when the host's creature has no picture in its My Creations
status: fixed
kind: bug
stage: All
reported: 2026-11-15
source: overnight stage tour 2026-11-15
role: 
versions: 0.7.8-lineages54-dev
fixed_in: 0.7.14.1-lineages58-dev
branch: feat/lineages-47
commits: a0f0d121
tests: manual Windows QA: the MSI hosts a copied Creature save (Vandh.spo), TITAN joins; TITAN hosts Cellqa.spo, the MSI joins
signature:
docs:
related:
---

## Report
Overnight stage tour: on the MSI (guest) the host's picture in the Party card stayed blank in Cell, Tribe and Civ (shown in Creature); the guest's picture showed on the host everywhere.

## Root cause
CORRECTION (2026-11-15): not only a test-profile gap. creations.cpp portrait_tick sends the host's picture only when its creature (or the played species) has a PNG in its own EditorSaves; the QA host profiles were assembled from copied saves and have none. Probably a test-profile gap; a player's own creature normally has its picture. To check with a host who made its creature itself.

## Fix
- portrait_tick looked for the picture in My Creations only, and always for the creature. A creature the player did not make (Sporepedia, another player, a shared save) has its picture in Pollination or another package; in Cell the card wants the cell's. Now, when My Creations has none, the picture comes from the package the game loaded the creation from (records_from_game, once per creation), and in Cell the cell's picture is sent (switch creations.portrait_any_package).

## Verification
FAIL (before, 2026-11-15 15:55, the MSI hosting a copied Vandh.spo, TITAN guest): the host's card blank on TITAN (out/shots/r59/guest_names.png).
PASS (the same setup with the fix): "own creature 7C068C08: picture from the package the game loaded it from", the host's creature on TITAN's card (guest_pic.png); Cell (TITAN hosts Cellqa.spo, the MSI joins): both cells' pictures found the same way, the host's cell on the MSI's card.
no-fail-run: Windows QA by hand (no lab on this PC).

## History
- 2026-11-15: opened.
- 2026-11-15: seen again with the MSI hosting a copied save (no PNG in its My Creations). A host who made its creature in the editor has the PNG; kept open as a test-profile gap (a fallback picture would need the game's thumbnail renderer).
- 2026-11-15: fixed (the picture from any package the game reads, the cell's in Cell); checked on TITAN + the MSI in Creature and Cell.
