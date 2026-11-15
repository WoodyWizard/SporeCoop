---
id: BUG-0747
title: Space: an NPC empire's store pays a different price for the same spice in each game
status: fixed
kind: desync
stage: Space
reported: 2026-11-08
source: fix/w46-g4b-coverage, coop_space_store.py lane 3 2026-11-08
role: 
versions: 0.7.10 and earlier
fixed_in: 0.7.12
branch: fix/w46-b1-space
commits: a741fdc9
tests: space_store
signature:
docs: docs/re/space.md#space-npc-store-runtime, docs/re/space.md#space-npc-store-seed
related: BUG-0199
---

## Report
Seen by `coop_space_store.py` (lane 3, 2026-11-08, `space_empire`): in the same session the guest sold one Red Spice
to the store of the empire 80001FCC (home star 0046102D) for 281, the host one minute later to the same empire's store
for 4,779; in an earlier session the host's store showed 6,747 for it. Each game makes its own store
(GenerateNPCStore 0x103E8F0 picks the 3 wanted spices and a price factor at random, docs/re/space.md 14.2), so two
players at the same empire see different wants and prices: one sells for the full price, the other for 5%. The shared
money hides it (both earnings land in one budget) but the trade screens disagree.

Reproduce: `SPORE_LANE=N python3 tools/tests/coop_space_store.py` and compare the two "the store paid" lines.

## Root cause
Inferred: the store is generated in each game separately, from the game's own RNG, when that game first needs it
(not traced at runtime which call). Not analysed further on fix/w46-g4b-coverage (a coverage task).

Observed (Ghidra GOG 2024, fix/w46-b1-space): GenerateNPCStore 0x103E8F0 makes one store per planet (store +0xC
PlanetID) and fills it in 0x103CD10 (`SporeCoop::cSpaceTrading_FillNPCStore`): an in-place Fisher-Yates shuffle of the
trading manager's spice list (+0x20) and RandomInt(10) per wanted spice for the price factor, both on the global
generator 0x1601760. The shuffle's result depends on the list order every earlier store left and on the global
generator's state, so each game made a different store. Observed at runtime: with the fix off the two games' stores of
planet 0046102D wanted `1CA01565 48730, 1CA01561 21783, 1CA01562 11246` (host) and `1CA01562 7872, 1CA01565 24365,
1CA01567 20242` (guest), lab 20261108-150833. docs/re/space.md#space-npc-store-seed.

## Fix
spacecoop.cpp h_StoreFill (switch `space.store_seed`): in a session the fill sorts the spice list by key and draws
from the private generator of the planet seeding (BUG-0123's RandomInt hook) seeded from the PlanetID, so every game
makes the same store. Log `space: the NPC store of planet <id> seeded ...`, counter `stores_seeded=`. Test command
`coop.space stores` (every store: planet, wanted keys, prices). Limit: a store made again after 30 min of game time is
the same store; a store made before the session is not covered.

## Verification
`coop_space_store.py` (suite `space_store`) compares both games' stores (`coop.space stores`) and the two prices paid.
* FAIL with `SPORE_FIX_OFF=space.store_seed`: lab 20261108-150833 (the stores differ, host paid 11,246, guest 7,872).
  (Run 20261108-145056 with the fix off passed on the prices alone: both stores did not want Red Spice, 281 each;
  that is why the test now compares the stores.)
* PASS: lab 20261108-145259 and 20261108-150234 (fix/w46-b1-space, lane 6): the same store in both games
  (`1CA01562 7872, 1CA01565 20710, 1CA01561 10456`), both paid 7,872.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-g4b-coverage): seen by space_store (guest 281, host 4779 for the same spice and empire). Open.
- 2026-11-08 (fix/w46-b1-space): cause found (the fill 0x103CD10 on the global generator), fixed behind space.store_seed; FAIL 20261108-150833, PASS 20261108-145259 / 20261108-150234.
- 2026-11-11: released in 0.7.12.
