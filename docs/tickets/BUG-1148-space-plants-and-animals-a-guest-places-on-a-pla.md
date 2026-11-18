---
id: BUG-1148
title: Space: plants and animals a guest places on a planet it terraformed do not reach the host (the planet is not stabilized there)
status: fixed
kind: desync
stage: Space
reported: 2026-11-15
source: discord
role: 
versions: 
fixed_in: 0.7.15
branch: fix/w51-eco
commits: 193ff437 4a28e113
tests: space_eco, space_eco_live
signature:
docs: docs/re/space.md#space-ecosystem, docs/06-testing.md#testing-space-ecosystem
related: BUG-1155, BUG-0883, BUG-0749, BUG-1042
switch: space.eco_sync
---

## Report
Discord (bug question, 2026-11-15): in one session a guest terraformed a planet to T1 and placed the plants and
animals that stabilize it; when the host flew to that planet in the same session it was not stabilized and had no plant
or animal life.

## Root cause
Inferred from the code first: only the atmosphere / temperature deltas and the planet record (+0xB0 / +0xB4 / +0xB8)
were synced (spacecoop.cpp K_TERRA, K_TERRA_REC; BUG-0749, BUG-0883). Then RE (docs/re/space.md#space-ecosystem):
* observed (decompile + runtime lane 6): a planet's species are the record's lists +0xBC (plants) and +0xD0 (animals);
  the food web is read from their order, and the "stabilized" level is the game's `PlanetRecord_EcoLevel` 0xBBCFB0
  (the highest n up to the T score with 3n animal species; it also sets the colony city count). A landed cargo drop
  adds its species through `PlanetEco_AddPlant` 0xC81C50 / `PlanetEco_AddAnimal` 0xC82590 (player flag set). Nothing
  sent these lists: in the guest's game 02460035 (T1) went to 3 plants / 3 animals / level 1 while the host's record
  stayed at 2 plants / 0 animals / level 0 (lab, switch off).
* observed: the planet entry (0xFF96B0) makes herds only of the record's planet objects of flag 4 (a dropped animal is
  none) plus, with 2 % chance, one random species: even with the lists, a game that did not see the drop shows no
  animals of it (runtime: the host landed with the guest's 3 species in its record and saw none in 15 s).
* inferred: where the game keeps a player's own dropped herds between visits was not found (not in the record).

## Fix
space_eco.cpp (new Space feature "eco", kind K_ECO 90, wire/space_eco.h; the space family 1.1): hooks on the two
placement functions; after the own placement the planet's lists (with each species' drop position) go to every player
while they change (at most once a second, 30 s after the last placement). A game on that planet adds the missing
species through the same game functions (player 0) and a herd of each new animal species (`PlanetEco_SpawnHerd`
0xACF320, the planet entry's own spawn); a game elsewhere sets its record's lists (the game's push_back way) and owes
a herd of each new animal species for its next landing there; a planet not generated here waits (BUG-1155's path).
The host keeps the latest lists per planet and sends a star system's to a partner who enters it, all to one who joins.
Switch `space.eco_sync` (off: 0.7.14, nothing sent). A 0.7.14 peer ignores kind 90 (unknown kinds pass and are not
read, net/validate.h). Test seam `coop.space eco [PlanetID] | place <plant|animal> <i> <PlanetID> | spawn 0|1`;
`coop.space trade cargo` takes a full species key now.

## Verification
Lab, lanes 6 / 7, fixture space_free (runs in out/lab/ of the fix/w51-eco worktree):
* space_eco FAIL with SPORE_FIX_OFF=space.eco_sync: 20261115-131139 (final build), 20261115-130529-2 (first build):
  the host's record keeps 2 plants / 0 animals, level 0; no creature after landing.
* space_eco PASS: 20261115-130846, 20261115-131636 (final build), 20261115-130529 (first build).
* space_eco_live (both on the planet) PASS: 20261115-131105, 20261115-131617; FAIL with the switch off:
  20261115-131800.

## History
- 2026-11-15: opened.
- 2026-11-15: RE (docs/re/space.md#space-ecosystem), K_ECO sync, tests space_eco / space_eco_live (fix/w51-eco).
- 2026-11-18: final check 20261117-154911: space_eco_live ZeroDivisionError in its key-frame camera (a census creature at the origin, made but not placed yet); test fix 61a50d39, PASS in 20261117-213354.
- 2026-11-18: released in 0.7.15.
