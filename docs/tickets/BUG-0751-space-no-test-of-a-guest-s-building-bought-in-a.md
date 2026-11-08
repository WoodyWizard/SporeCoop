---
id: BUG-0751
title: Space: no test of a guest's building bought in a colony's city planner
status: fixed
kind: desync
stage: Space
reported: 2026-11-08
source: BUG-0199 (fix/w46-g4b-coverage, 2026-11-08)
role: 
versions: 0.7.10 and earlier
fixed_in: unreleased
branch: fix/w46-b1-space
commits: 3fe2518f
tests: space_planner_build
signature:
docs: docs/re/space.md#space-colony-planner
related: BUG-0199, BUG-0189, BUG-0766
---

## Report
Child of BUG-0199 (the Space part of BUG-0189). A building a guest buys in its colony's city planner in Space.

Attempt (fix/w46-g4b-coverage, lane 3, out/exp_cplan.py on `space_empire`, 2026-11-08): the guest landed on the home
planet Madas (00460035), `coop.space allies cities` lists the colony cities with screen points; a click on the
nearest own city (1270, 690) opened the city planner (the same cCommunityEditor; the tiles have designs and prices:
house $1,600, factory $800, fun $1,200, turret $1,000). The planner camera stayed at ground level and the city had
no free plot ("9/9" at the top right); a drag of the house tile onto the city did nothing (money unchanged).
`coop.civ planner` on the guest said `host_index=-1`: the civ module does not map Space colony cities, and
civ.cpp h_AddBuilding sends a guest's planner building only for a city with a host index (inferred from the code:
a guest's Space planner building is not sent to the host at all; to be confirmed by the test).

Recipe for the test: a colony city with a free plot (a new colony through `coop.space empire colony`, or the colony
P01460035), open its planner by a click, drag a tile onto a free plot (plot screen points: `coop.civ slots` does not
work in Space, a Space variant is needed), close with the check; assert the host's game has the building in that
city and the money fell on both.

## Root cause
A test gap and a co-op gap. Observed (fix/w46-b1-space, lane 6, `space_empire`, home planet 00460035): with a plot
freed in both games the guest's planner (opened by a click on the city once the ship is over it) built and paid a
house ($1,600 off the shared money on both games), and the host's city kept 5 houses (lab 20261108-154201 with the
switch off; the earlier attempt's planner had no free plot: all 9 cities have 12 of 12). Observed in the code and
log: civ.cpp h_AddBuilding returns at once outside Civ (`active()`), and a Space city has no host index; at the
planner's AddBuilding the new building's position reads (0 0 0).

## Fix
civ.cpp (switch `civ.space_planner_send`): in Space the guest sends `CMD_SPACE_BUILD {place, noun, city position,
position}`; the host, when its ship is on the same planet, builds the noun into a free slot of its city within 60 m
of that position (build_in_city, not paid again). Counters `space_planner: sent= built= away= failed=` (coop.civ).
Test helpers `coop.civ scities`, `unbuild s<i> <k>`, `buildings|slots s<i>`. Limit: the host on another planet does
not get it (BUG-0766).

## Verification
`coop_space_planner_build.py` (suite `space_planner_build`):
* FAIL with `SPORE_FIX_OFF=civ.space_planner_send`: lab 20261108-154201 (the guest has 6 houses, the host 5; the
  flight check of that version also failed, later only printed).
* PASS: lab 20261108-154848 and 20261108-155156 (the host's city 5 -> 6 houses, money -1,600 on both). Run
  20261108-154529 had the house on the host too and failed only the flight check that is now printed.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-g4b-coverage): opened as a child of BUG-0199 after an attempt with a run (recipe and blocker above).
- 2026-11-08 (fix/w46-b1-space): test written (plot freed by the game's DestroyBuilding); it showed the host never got the building; fixed behind civ.space_planner_send; limit BUG-0766.
