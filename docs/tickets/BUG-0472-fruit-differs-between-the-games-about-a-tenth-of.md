---
id: BUG-0472
title: fruit differs between the games: about a tenth of the fruit near a player lies elsewhere or exists on one game only (Creature and Tribe)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, report out/detector/20261103-0236.md)
role: 
versions: 
fixed_in: 0.7.4
branch: fix/w40-world-objects
commits: 9445045a 5ec41dc0 ffed3f29 0209a1c2
tests: wobj_fruit, wobj_tree (tools/tests/coop_world_objects.py), detect_creature, detect_tribe
signature:
docs: docs/re/creature.md#creature-world-objects, docs/06-testing.md#testing-world-objects
related:
switch: wobj.fruit wobj.tree
---

## Report
Detector: `Creature:unmatched:fruit` in 91% of 343 Creature comparisons (3 host / 8 guest fruit without a partner within 3 m of 25), `Tribe:unmatched:fruit` 73%, `guest_only:fruit` up to 27 at once. Fruit falls from trees and is eaten on each game separately (no fruit sync): a fruit one player eats is still there for the other. Class G4 (BUG-0421); tribe food bundles (`Tribe:unmatched:game_bundle` 15%) are the same kind of local drop.
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lane 9, Ghidra + runtime): fruit is cFruit (noun 0x2C9CC91); hanging fruit are drawn from the tree's
mask (cTreeData +0x48) and made by each game's flora near its own camera; a fruit falls with the flora's
DropFruit (0xB09AE0) and lies on the ground (state 2) until eaten (state 5, the eat node 0xD87590). Every game ran
its own falls and meals: a fruit one player ate stayed in the other's game, a fruit that fell in one game hung on
in the other's. Observed too: most of the detector's `unmatched:fruit` in Creature were hanging fruit objects one
game had made near its camera and the other had not (the trees' masks equal on both games), not a desync.

## Fix
mods/coop/worldobj.cpp (one table of host-owned world object kinds): kind `fruit` (loose fruit: the host's are
bound to the guest's by position, a fruit eaten in one game is eaten (cFruit_SetState 5) in the others, the
guest's own loose fruit without a host partner are removed) and kind `tree` (a slot that falls in one game falls
with DropFruit in the others). The census leaves hanging fruit out of the detector's rows (their state is the
tree's). Switches `wobj.fruit`, `wobj.tree`. Not done: fruit that fell in the host's game where the guest's slot
was already empty is not made on the guest; Tribe food bundles (`game_bundle`) have no kind yet.

## Verification
`coop_world_objects.py --kind fruit` / `--kind tree` (suites wobj_fruit, wobj_tree, creature fixture).
* fruit: FAIL with `SPORE_FIX_OFF=wobj.fruit`: out/lab/20261103-123122 (both eaten fruit stay in the other game);
  PASS out/lab/20261103-122753.
* tree: FAIL with `SPORE_FIX_OFF=wobj.tree`: out/lab/20261103-123223; PASS out/lab/20261103-122753.
* Detector (SPORE_DETECTOR=20, 24 comparisons per stage, the census without hanging fruit): off (all switches of
  this branch off) out/lab/20261103-130419: `Creature:unmatched:fruit` 96% (116 objects), `Tribe:unmatched:fruit`
  92% (63); on out/lab/20261103-131403: 46% (12 objects), 8% (2). What is left: BUG-0497. Tribe food bundles
  (`game_bundle` 17% / 4%) have no kind yet.

## History
- 2026-11-03: opened.
- 2026-11-03: fix/w40-world-objects: fixed (worldobj.cpp / nest.cpp).
- 2026-11-03: released in 0.7.4.
