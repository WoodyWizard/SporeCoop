---
id: BUG-0485
title: spice / commodity nodes are drawn at another scale on the guest (10.0 host, 1.0 guest)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, out/detector/20261103-final.md)
role: 
versions: 
fixed_in: 0.7.9
branch: fix/w45-placement
commits: 9b993da9, a42f6e5f
tests: node_scale (coop_node_scale.py)
signature:
docs: docs/re/creature.md#creature-node-scale
related:
---

## Report
Detector: `Creature:scale:node` 2% of Creature comparisons, `Tribe:scale:node` 2%, `Creature:scale:node` 10.00 host / 1.00 guest. A commodity node (spice geyser) the host's game scaled is at the default scale on the guest. Check what the player sees (screenshot pair) before fixing. Class G2.
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (runtime + objdump, 2026-11-06, fix/w45-placement): the SetScale diagnostic names the setter of the node's 10:
return address 0xBC0641 in 0xBC02F0 (Planet_PlaceCommodityNodes: CreateInstance of noun 0x0403DF5C, SetScale with the
placement record's scale, SetPosition), loco vtable 0x14699E8. It runs on the host at the live Cell -> Creature change
(campaign_ct, rel/0.7.8 lab 20261105-155603, the host's log only) and in space_play on both games. A node read from a
save has 1.0 (all 16 nodes of the creature / tribe fixtures on both games): the scale is not saved. So the host's
live-made node is 10, the guest's copy from the shared world (a load) 1.0. Not a mod bug at its origin, a co-op desync.
What the player sees: the node at another size on each game (not compared on a screenshot pair).

## Fix
`wobj.node_scale` (worldobj.cpp, world-object kind `node`, Creature / Tribe / Civ, the whole planet): the state is the
host's node scale (float bits); a guest's node bound to it (3 m) that differs by more than 1 % takes it (cSpatialObject
vt 0x40 SetScale). `coop.fix wobj.node_scale off`: each game keeps its own (0.7.8).

## Verification
node_scale (creature fixture; the host's node nearest the guest gets the live-made scale 10 with `coop.fields scale`,
the guest's node at that place is read with `coop.wobj list node`): FAIL with `SPORE_FIX_OFF=wobj.node_scale`
out/lab/20261106-061822 (guest 1.0 for 15 s), PASS out/lab/20261106-062301 (10 within 2 s).

## History
- 2026-11-03: opened.
- 2026-11-03: investigated on fix/w40-scale: on the creature / tribe fixtures all 16 nodes are 1.0 on both games (the 10 is set at runtime after the live Cell -> Creature start, not saved); the node loco vtable 0x1469A28 uses cModelObject_SetScale, hooked now, which logs `scale 10 set on an object with vtable ... from ...` once per caller: the next campaign_short run names the setter. Not fixed (what the player sees and who sets it unknown); docs/known-issues.md#issue-node-scale.
- 2026-11-06 (fix/w45-one-game-objects, L1): the BUG-0485 diagnostic in lab logs of 2026-11-04..06 named only other objects: `scale 10 set on an object with vtable 014699E8 from 00BC0641` (space_play host and guest, 12 times) and `vtable 01474BE8 from 00CA41DA` (3 times); never the node vtable 0x1469A28. Not analysed further (time).
- 2026-11-06 (fix/w45-placement): setter found (0xBC0641, the live node placement; the scale is not saved), fixed with wobj.node_scale; runs above.
- 2026-11-06 (fix/w45-placement): regression with SPORE_DETECTOR=20: smoke out/lab/20261106-074134 (14/14), play_tribe out/lab/20261106-073013, play_civ / play_space and the touched tests out/lab/20261106-080336 (9/9; play_civ: no planet:building or scale:vehicle row, the guest's buildings_fixed=+0/-8 off_slot=0).
- 2026-11-07: released in 0.7.9.
