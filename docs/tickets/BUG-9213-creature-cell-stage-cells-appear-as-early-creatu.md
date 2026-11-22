---
id: BUG-9213
title: Creature: cell-stage cells appear as early creatures in a new Creature world
status: fixed
kind: bug
stage: Creature
reported: 2026-11-15
source: owner play test 2026-11-15 (night)
role: 
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 6e4a9e44
tests: tools/qa_win (manual, 2026-11-15): coop.library scan with library.skip_provisional off/on (TITAN, DuoQAJiffStand)
signature:
docs:
related: BUG-9207
---

## Report
Owner: "It looks like Cells are spawning as early creatures in creature stage. That's an issue."

## Root cause
Not analysed yet; no screenshot. No creation of the cell type (0x40616000) was registered in the host's log. To check: which species the new world's NPCs are (coop.npc / the herds) and whether they come from the players' cell creations (the Lineages library, the start editor's parts list) or are the game's own early creatures.

## Fix
- Root cause: Spore's Cell-to-Creature landfall creature keeps the cell's picture; BUG-9095 re-encoded only its model into that picture, so partners received a creature whose icon and Sporepedia card were the cell (Wilson).
- That provisional creation is no longer shared (library.skip_provisional); the creature saved in the editor is shared as before. Creations a partner already received stay in their Sporepedia (they can delete them there).

## Verification
FAIL (switch off): coop.library scan repaired=3 (three provisional cell-picture creatures offered).
PASS (switch on): the log says "a provisional creation (its PNG holds group 40616200) is not shared (BUG-9213)"; provisional counted, none offered.
no-fail-run: Windows QA by hand (no lab on this PC).

## History
- 2026-11-15: opened.
- 2026-11-15 (night): opened from the owner's play test (build 87d69a05 on both PCs).
- 2026-11-15: owner: "It was Wilson as a Cell. Even the discovery image on the top right of the screen showed the cell stage 2D / Top Down image." A cell creation is used as a creature species of the new world.
- 2026-11-15: "Wilson" is in no log. Lead to check: the Lineages Cache (library.cpp) imports other players' creations into the profile; a cell registered under a creature group could be picked by the game's population of a new Creature world. Dorger.spo copied to the test host's profile (Game7) for the repro.
- 2026-11-15 (overnight): root cause found (BUG-9095's repaired PNG keeps the cell picture); provisional creations not shared (6e4a9e44).
- 2026-11-21: six creatures shared before the fix (2026-11-08 to 11-11, from three players' landfall) still carried the cell picture in the owner's Lineages Cache. Each was checked by its picture (mean difference 0.1 to 2.0 against that player's cell picture; the same players' three real creatures 20.7 to 34.9) and added to data/lineages/banned.txt as creation lines, so the ban list (BUG-9329) moves them out of every cache. A simulation of the new list on that cache catches exactly these six.
