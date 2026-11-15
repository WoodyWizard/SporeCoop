---
id: BUG-0766
title: Space: a guest's colony planner building is lost when the host's ship is on another planet
status: fixed
kind: desync
stage: Space
reported: 2026-11-08
source: fix/w46-b1-space (BUG-0751, 2026-11-08)
role: 
versions: 0.7.11 and earlier
fixed_in: 0.7.12
branch: fix/w47-b2
commits: 8b6a60dd aca22a9c
tests: space_planner_away
signature:
docs: docs/re/space.md#space-colony-build-hold
related: BUG-0751, BUG-0884, BUG-0901
---

## Report
The limit of BUG-0751's fix (fix/w46-b1-space, `civ.space_planner_send`): a guest's building bought in a Space
colony's city planner is built in the host's game only when the host's ship is on the same planet (the planet's
cities exist only while it is the active planet). Otherwise the host logs `civ: space planner: <name>'s building ...
not built: this game is at <place>` and counts `space_planner: away=` (coop.civ); the guest keeps its building, the
host's world (and so every later share) does not have it, the money was spent for both.

Reproduce: coop_space_planner_build.py with the host's ship in the system or on another planet before the guest's
drag. Next step: store the building in the planet record's city data (cCityData) on the host, or hold it until the
host lands there.

## Root cause
Observed (code and lab): civ.cpp host_space_build dropped a guest's CMD_SPACE_BUILD whose planet was not the host's
place (`away=` counter, "not built: this game is at ..."); a planet's cities exist only while it is the active planet.

## Fix
The host keeps such a building (`g_space_held`, with its planet, world, city position; session state, at most 128)
and builds it in `space_tick` 5 s after its ship is on that planet (not paid: the buyer paid). The game makes a
planet's cities again from its record when it becomes active, so the plot may be taken at first: a kept building that
finds no free plot is tried again every 2 s for 60 s. Then it is relayed to the other guests like any building the
host gets (BUG-0884). Counters `space_planner: kept= kept_built=` in `coop.civ`. Twin switch
`coop.fix civ.space_build_hold off` (dropped, 0.7.11).

## Verification
`coop_space_planner_build.py --away` (suite `space_planner_away`): the host's ship leaves the planet, the guest buys
a house with real input, the host lands again; the game had made the city again with the house the test destroyed
before, so the test frees a plot on the host again; the host's city must get the guest's house.
* FAIL with `SPORE_FIX_OFF=civ.space_build_hold`: lab 20261110-190843 (the host's city 5 -> 5 houses).
* PASS: lab 20261110-191235 (two runs: the host's city 5 -> 6 houses after its return).
* Runs 20261110-184846 and 20261110-185810 were before the test frees the plot again: both passed, the second with the
  switch off, because the game had brought the destroyed house back (that is how the city remaking was found);
  20261110-190430 and 20261110-190616 overlapped on lane 4 (two lab runs at once) and do not count.

## History
- 2026-11-08: opened.
- 2026-11-10: fixed on fix/w47-b2 (the host keeps the building until its ship is there; test space_planner_away). Found on the way: BUG-0901.
- 2026-11-11: released in 0.7.12.
