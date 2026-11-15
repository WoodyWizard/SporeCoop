---
id: BUG-1151
title: E1: Space: a restored guest colony comes back with one generated city (its cities, buildings and spice are lost), and a colony on a star of an NPC empire does not come back at all
status: fixed
kind: bug
stage: Space
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-store
commits: 8edf9682, 53f904d2
tests: space_session, space_session_npc, wire_tests (make test)
signature:
docs: docs/re/space.md#space-colony-cities
related: BUG-1149, BUG-0195, BUG-0197, BUG-0901
switch: space.colony_cities, space.npc_star_colonies
---

## Report
Playability audit (0.7.15, space.md items 1-4): a guest colony restored after a rejoin (and now after a new session) is a record-level colony with one generated city: its cities, buildings, turrets and spice are gone; a colony on a star an NPC empire owns in the host's world (bought or conquered in the guest's game) is refused at the restore ("the star belongs to another empire") and lost.

## Root cause
Observed in code: space_allies.cpp colonize(restore) made the colony with FillPlanetDataForTechLevel and refused a star of another empire; K_KEPT carries only PlanetIDs.

## Fix
* Cities: the guest sends its own colonies' cities as its planet record keeps them (K_COLONY 14, parts of 900 bytes,
  wire/space_map.h City: position, size, turrets, spice, income, name, building entries), the host keeps them with the
  colony (and on its disk: `space.cities`), sends them back before K_KEPT (K_KEPT_COLONY 15), and the guest writes them
  into the restored civilization (space_allies.cpp cities_apply: missing cities made the game's way: GameAlloc +
  cCityData ctor + AddCityData; building entries appended through the game's reserve). Not into the active planet.
  Space family minor 2 (two new kinds, older peers ignore them). `coop.fix space.colony_cities off`: the generated city.
* NPC stars: the restore takes the star in the guest's own copy with the game's TransferStarOwnership (0xC353A0);
  never on the host, never a partner's mirror. `coop.fix space.npc_star_colonies off`: refused.

## Verification
* space_session (citytest seam: two cities with spice 77 / 33, turrets, 3 real building entries, names):
  FAIL (SPORE_FIX_OFF=space.colony_cities): out/lab/20261115-133236; PASS: out/lab/20261115-133047, out/lab/20261115-134708
  (the same two cities, spice, turrets, buildings and names in the guest's record after the new session).
* space_session_npc (fixture space_empire; the guest's game takes an NPC's star with `allies npcstar`):
  FAIL (SPORE_FIX_OFF=space.npc_star_colonies): out/lab/20261115-131611; PASS: out/lab/20261115-131454,
  out/lab/20261115-131957, out/lab/20261115-134940.
* unit: wire_tests (golden bytes of K_COLONY, the cities blob round trip and ranges).
Not verified: a landing on the restored colony (the city maker building the written buildings; the record is checked);
ornaments and walls are not carried (docs/re/space.md#space-colony-cities).

* PASS post-merge of E3 / E4 (rel/0.7.15 at 94118b08): out/lab/20261115-141219, out/lab/20261115-141234 (space_session).

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-store: colony cities and NPC stars at the restore.
