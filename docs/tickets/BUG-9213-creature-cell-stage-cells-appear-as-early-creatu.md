---
id: BUG-9213
title: Creature: cell-stage cells appear as early creatures in a new Creature world
status: open
kind: bug
stage: Creature
reported: 2026-11-15
source: owner play test 2026-11-15 (night)
role: 
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests:
signature:
docs:
related: BUG-9207
---

## Report
Owner: "It looks like Cells are spawning as early creatures in creature stage. That's an issue."

## Root cause
Not analysed yet; no screenshot. No creation of the cell type (0x40616000) was registered in the host's log. To check: which species the new world's NPCs are (coop.npc / the herds) and whether they come from the players' cell creations (the Lineages library, the start editor's parts list) or are the game's own early creatures.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
- 2026-11-15 (night): opened from the owner's play test (build 87d69a05 on both PCs).
- 2026-11-15: owner: "It was Wilson as a Cell. Even the discovery image on the top right of the screen showed the cell stage 2D / Top Down image." A cell creation is used as a creature species of the new world.
- 2026-11-15: "Wilson" is in no log. Lead to check: the Lineages Cache (library.cpp) imports other players' creations into the profile; a cell registered under a creature group could be picked by the game's population of a new Creature world. Dorger.spo copied to the test host's profile (Game7) for the repro.
