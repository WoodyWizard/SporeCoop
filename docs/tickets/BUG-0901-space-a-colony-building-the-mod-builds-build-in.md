---
id: BUG-0901
title: Space: a colony building the mod builds (build_in_city: a partner's planner building) may not be kept when the planet is left and made again
status: fixed
kind: desync
stage: Space
reported: 2026-11-10
source: fix/w47-b2 (BUG-0766 test, 2026-11-10)
role: both
versions: 0.7.11 and earlier
fixed_in: 0.7.12
branch: fix/w47-colony
commits: 53efd4bc
tests: space_city_keep
signature:
docs: docs/re/space.md#space-city-data
related: BUG-0751, BUG-0766, BUG-0884
---

## Report
Found while testing BUG-0766 (fix/w47-b2, lane 4, `space_empire`, planet 00460035). When a planet becomes the active
planet again the game makes its colony cities again from what it keeps for the planet: a house destroyed at run time
(`coop.civ unbuild`, cCity::DestroyBuilding) was back after the host's ship left the planet and landed again (lab
20261110-185810). Not checked yet: whether a building the mod makes with build_in_city (the host's copy of a guest's
planner building, BUG-0751; the guests' copies of the host's, BUG-0884; a kept one, BUG-0766) is in that data, i.e.
still there after its game's ship leaves and lands again, and in the next save. If not, the host's world loses a
guest's colony building as soon as the host flies away (the guest's own planner purchase is the game's own path and
presumably kept).

Reproduce: coop_space_planner_build.py (guest buys, host on the planet: the host has 6 houses), then the host takes off
and lands again: count the houses (`coop.civ buildings s<i>`) and compare their positions with the guest's; the test's
own freed plot comes back too, so free the plot with the planner's own demolish (real input) instead of `coop.civ
unbuild`, or find the planet's city data (planet record) and read it.

## Root cause
* Observed (lab 20261110-201410, the code before the fix, lane 5, `space_empire`): in each game a house of a home
  planet city destroyed (`coop.civ unbuild`) and a factory built there with the mod's build_in_city (`coop.civ
  sbuild`, the function of every Space relay path: space_build_here); both ships took off and landed again: both
  cities were back at 4 factories / 6 houses (the mod's factory gone, the destroyed house back).
* Observed (Ghidra GOG 2024): the planet's city maker 0xFF6900 builds a city from its cCityData (city +0x2F0, building
  entries +0x7C of 0x38 bytes). The live city is written into it only by City_WriteCityData 0xBD9FA0 (-> 0xFF26C0),
  which the planner's exit (0xD100B0, every city of the civilization), building damage and turret removal call;
  cCity::AddBuilding (used by build_in_city) and DestroyBuilding do not. So the game's own planner purchase is kept
  (written at the planner's exit), a building the mod adds is not.
* Inferred: the host's copy of a guest's building that the host's own planner never touched was lost whenever the
  host's ship left that planet, and from every save made after that (the save keeps the planet record's city data).

## Fix
civ.cpp space_build_here (host_space_build, the kept buildings of BUG-0766 and the guests' relayed ones of BUG-0884)
calls the game's City_WriteCityData after build_in_city when the city has its data (+0x2F0), as the planner's exit
does. Counter `space_planner: city_written=` (coop.civ). Twin switch `coop.fix civ.space_city_data off` (0.7.11: not
written). Symbol `SporeCoop::City_WriteCityData` (GOG 2024 only; off on the frozen GOG 2016).

## Verification
`coop_space_city_keep.py` (suite `space_city_keep`): in both games a factory built into a city by build_in_city (the
test command `coop.civ sbuild s<i> factory`, which now also writes the city as space_build_here does), both ships
leave the planet and land again; each city must still have the factory (a factory, so the house the game brings back
in that plot cannot pass for it).
* FAIL on the code before the fix: lab 20261110-201410; with `SPORE_FIX_OFF=civ.space_city_data`: lab
  20261110-202628 (both games 4 -> 4 factories, 6 -> 6 houses).
* PASS: lab 20261110-202314 (both games 4 -> 5 factories, 6 -> 5 houses after the landing), 20261110-203201 (with
  space_planner_build / away / miss, space_sculpt_tool / away: 6 of 6).
* Not run: the real relay paths end to end with a leave and a landing after the build (they call the same
  space_build_here); the save after it.

## History
- 2026-11-10: opened.
- 2026-11-10: fixed on fix/w47-colony (the city's data written after the mod's colony building; switch civ.space_city_data; test space_city_keep).
- 2026-11-11: released in 0.7.12.
