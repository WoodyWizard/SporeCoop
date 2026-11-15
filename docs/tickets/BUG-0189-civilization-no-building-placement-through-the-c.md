---
id: BUG-0189
title: Civilization: no building placement through the city planner in tests
status: fixed
kind: test
stage: Civ
reported: 2026-10-27
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: 
versions: 0.7.2.4 (main) and earlier
fixed_in: 0.7.11
branch: fix/w46-g4b-coverage
commits: 048f71e3
tests: civ_planner_build
signature:
docs: docs/re/civ.md#civ-planner-palette, docs/re/civ.md#civ-planner-palette-runtime, docs/06-testing.md#testing-g4b-coverage
related: BUG-0199, BUG-0493
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* In `civ_npc` (a new game) the planner's house / factory / entertainment have no design yet; a
  click on a tile opens the building editor (co-op vote). The bots only open and close the planner;
  guest buildings are tested through `coop.civ build` (coop_civ.py). A fixture with designs (or a
  bot that makes one in the editor) is needed.

## Root cause
A test gap, no bug of the mod. In the planner a building is placed by a mouse-down on a palette tile and a drag
onto a plot (docs/re/civ.md#civ-planner-palette); a click opens the Sporepedia shopper / editor, which is what the
old bots saw. At runtime (docs/re/civ.md#civ-planner-palette-runtime) the player civ of `civ_npc` has no building
design at all (`coop.civ design 0 house`: key 0, CORRECTION of the static "a tile always has a design"): the tile says
"Design a new house!" and a drag does nothing. With the culture set entry set to the house template the NPC cities of
the save use (`coop.civ bdesign 800021CA house 067CEDDC`) the tile shows the model and the drag places the house.

## Fix
Test `coop_civ_planner_build.py` (suite `civ_planner_build`, civ_npc, ~65 s): both games get the house design (test
setup), the guest opens its city planner with real clicks (city menu, planner button), hovers, holds the house tile
(123, 385), drags it in 30 steps onto a free plot's screen point (`coop.civ slots`), lets go, closes with the green
check. Checks in the host's game: the city's building list has the new house at the guest's position; the shared
money fell by the house's price on both games ($1,600); the guest's building list equals the host's.
Twin: new switch `civ.planner_send` (civ.cpp h_AddBuilding: the guest's planner placement is sent to the host as
CMD_BUILD); off = the guest's placement stays in its own game.

## Verification
* `SPORE_FIX_OFF=civ.planner_send`: FAIL, lab 20261108-061749 lane 3: the host's game has no new house after 25 s, the
  guest's own house was removed by its reconcile (the price stayed paid on both games).
* PASS 3 of 3, lab 20261108-061925 lane 3 (62-66 s each).

## History
- 2026-10-27: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w4-tests (wave 4): not reached: needs a civ fixture with building designs (or a bot that makes one in the building editor). A fixture task. Left open.
- 2026-11-08 (fix/w46-g4b-coverage): the planner drag recipe found at runtime (a design is needed: CORRECTION in docs/re/civ.md), test civ_planner_build with the twin switch civ.planner_send; off FAILS (20261108-061749), 3 of 3 PASS (20261108-061925). Fixed.
- 2026-11-08: released in 0.7.11.
