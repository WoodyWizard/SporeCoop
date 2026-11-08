---
id: BUG-0751
title: Space: no test of a guest's building bought in a colony's city planner
status: open
kind: test
stage: Space
reported: 2026-11-08
source: BUG-0199 (fix/w46-g4b-coverage, 2026-11-08)
role: 
versions: 0.7.10 and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0199
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
A test gap, and probably a co-op gap (no Space mapping of a guest's planner building, see above).

## Fix
Not done yet.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-g4b-coverage): opened as a child of BUG-0199 after an attempt with a run (recipe and blocker above).
