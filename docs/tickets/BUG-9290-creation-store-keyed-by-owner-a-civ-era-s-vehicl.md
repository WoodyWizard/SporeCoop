---
id: BUG-9290
title: Creation store keyed by owner; a Civ era's vehicles and buildings for the other eras
status: fixed
kind: feature
stage: creature
reported: 2026-11-20
source: step 4 part 5 (Civ era visitors)
role: host
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: e2e7c598
tests: tools/tests/galaxy_server_check.py; in game: TITAN QA runs
signature:
docs: docs/12-server-and-players.md#server-era-visitors-more
related: BUG-9287, BUG-9291, BUG-0647
---

## Report
Two players' creations can share a key (ids count up per profile; vehicle and building designs above all),
so a store keyed by the key alone could hand out the wrong creation. And the Civ era's vehicles and buildings did not
reach the other eras.

## Root cause
Design flaw of BUG-9287 (the key alone); feature for the Civ stream.

## Fix
The store's key is {owner identity, instance, type, group}; any creation type; installed under a key of ours
(creations::creation_from_bundle: as a host's own species or design on a guest). VISITOR carries the sender's identity.
The Civ era's host sends its buildings and vehicles near the other eras' players (VISITOBJ, at most 8 a second) and
puts their designs in the store; a Creature / Tribe game makes a ground prop per object (BUG-9291: not reliably drawn).
Before a prop the game is asked to bake every level of detail of the model (IBakeManager vt 0x1C(key, level) /
vt 0x4C(key, &level)).

## Verification
- tools/tests/galaxy_server_check.py: FAIL not run on the BUG-9287 server (by its design "another player's creation with the same key
  is its own" cannot pass: one key, one file); PASS now, with the VISITOBJ checks.
- In game (TITAN, QA 8 hosting Abber (Civ), QA 9 on Abber (Creature)): the Civ host sent 441 objects in 129
  messages near the Creature player; the Creature game got them, fetched the 6 designs from the store and made 9
  props at the city's places. The bake: a vehicle and a building had level 0 only (mask 1), a creature all four (F);
  20 s after asking, the vehicle had all four.

## History
- 2026-11-20: opened.
- 2026-11-20: built and seen in game / in the server checks (TITAN).
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
