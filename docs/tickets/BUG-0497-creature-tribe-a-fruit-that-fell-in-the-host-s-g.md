---
id: BUG-0497
title: Creature / Tribe: a fruit that fell in the host's game where the guest's tree slot was already empty is not shown on the guest (split from BUG-0472)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-03
source: fix/w40-world-objects (coop.wobj missing counter, lane 9)
role: 
versions: 
fixed_in: unreleased
branch: fix/w45-world-objects
commits: 250fda1a, 9ec9fc1e
tests: host_fruit (tools/tests/coop_world_host.py --case fruit)
signature:
docs: docs/re/creature.md#creature-host-places
related:
switch: wobj.fruit_place
---

## Report
worldobj.cpp binds the host's loose fruit to the guest's by position; when the guest's own tree had already dropped that slot (its own fall, before the host's), the host's fruit has no copy on the guest (`coop.wobj` guest `missing=` counter: 124 over 3 minutes of walking, lane 9). The mechanism does not make fruit (no spawn path is known: cFruit is made by the flora, 0xB09790 from a tree slot). Trees that grow fruit again (not seen) would not be copied either. Detector: `Creature:unmatched:fruit` 46% of comparisons with the fix (96% without). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (runtime, lanes 6 / 7): each game's fruit roll their own way (the same drop from the same tree came to rest
13 m apart, play_tribe), creatures knock their own down, and the flora recycles the loose fruit farthest from its own
camera when its free fruit run out (Flora_TakeFruitForDrop 0xB08DF0, only caller 0xB09790): so a host fruit had no
copy within the 3 m bind radius on the guest (`missing`), or the guest's copy lay elsewhere and was removed as its own
(`dropped`): missing=62 dropped=59 in play_tribe. The mechanism could not make a fruit.

## Fix
worldobj.cpp, the host's places (Kind `place` / `relocate` / `place_eps` / `make`, docs/re/creature.md#creature-host-places):
a host fruit without a copy within 3 m takes the guest's nearest unbound fruit within 25 m and it is put at the host's
place (SetPosition, only falling or on the ground on both sides); a bound one more than 1 m off is put back; a fruit
still missing is made the game's way: slot 0 of the nearest tree (within 60 m) gets its mask bit for one
Flora_DropFruit 0xB09AE0 and the mask is restored exactly, the falling fruit is put at the host's place. (L2's first
version took the first free slot: on a full tree that is the slot past its count and made nothing, lab
20261106-041736 missing=30 made=0.) Switch `coop.fix wobj.fruit_place off`.

## Verification
host_fruit: a host fruit whose copy the guest's game lost (`coop.wobj eat <p> local`), and a guest fruit moved 8.5 m
(`coop.wobj move`); both checked in the guest's own fruit list; then every tree near the players has the same mask on
both games.
- FAIL with `SPORE_FIX_OFF=wobj.fruit_place`: lab 20261106-042258 (all three fruit checks fail).
- PASS: lab 20261106-042151, 20261106-042440 (with the tree check: 21 trees, none different; 3 fruit made).
- Detector (SPORE_DETECTOR=20): Tribe `unmatched:fruit` 84 % of the comparisons before (lab 20261106-035845,
  0.7.9 base) -> 52 % (lab 20261106-051335); Creature `unmatched:fruit` 47 % (L1 runs) -> 11 % (lab 20261106-051335,
  "0 host / 1 guest without a partner": a guest's own fruit within the grace period). Not gone: fruit still falling
  are compared at 3 m while they roll.

## History
- 2026-11-03: opened.
- 2026-11-06 (fix/w45-world-objects): fixed (place / relocate / make from a tree slot), test host_fruit.
