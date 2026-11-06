---
id: BUG-0548
title: Civ: dropped bundles (game_bundle) of fights and captures differ by one or two between the games
status: fixed
kind: desync
stage: Civ
reported: 2026-11-04
source: lab, coop_civ_tribe_left (fix/w42-civ-left, out/lab/20261104-131223)
role: guest
versions: 
fixed_in: unreleased
branch: fix/w45-world-objects
commits: 6a642757, c3097c62, 03c81177
tests: host_bundle, host_bundle_civ (tools/tests/coop_world_host.py --case bundle / bundle_civ)
signature:
docs: docs/re/tribe.md#tribe-host-bundles
related: BUG-0539, BUG-0484, BUG-0472, BUG-0617
switch: wobj.bundle
---

## Report
coop_civ_tribe_left (fix/w42-civ-left): the bundles (game_bundle, noun 0x18C431C) differ by one or two between the
games after a capture (host 36 / guest 37, out/lab/20261104-130500) and in a warm game after other fights (34 / 35,
out/lab/20261104-131223), while the tribes, huts, tools and food mats match. The detector's `Civilization:unmatched:
game_bundle` (BUG-0539's raids aside) is this.

## Root cause
Observed (runtime, lanes 6 / 7, play_tribe / play_civ): bundles that are no tribe's mat food are each game's own: the
host's members carry food (creature +0x34 container, 5.0 each) from kills and gathering and the host's hunts drop food
on the ground (cBundleManager +0x1C ground container); a guest's copies of those creatures run no behaviour, so the
guest had none of them (Tribe `unmatched:game_bundle` 77 %, `host_only` rows). In Civ the fights' and captures' loot
is dropped by each game on its own (inferred from the 36 / 37 counts; the command capture of host_bundle_civ dropped
none).

## Fix
Kind `game_bundle` (Tribe and Civ, worldobj.cpp, docs/re/tribe.md#tribe-host-bundles): the host sends its ground and
carried bundles with the holder's place; a guest makes a missing ground one in its ground container
(CreateBundles 0xAC79F0) at the host's place, gives a carried one to its nearest creature with an empty hand within
4 m of the host's carrier, removes its own ground bundle the host has not (RemoveBundles 0xAC7C50 of that bundle),
and a ground bundle a guest's member takes is taken on the host too; the guest keeps its own members' carried food.
The carrier is looked up in the game's creature list (L2's version read noun_id at holder - 0x34 of a mat's container
and faulted the host on a null vtable, lab 20261106-042445). Switch `coop.fix wobj.bundle off`.

## Verification
- Tribe, host_bundle: FAIL with `SPORE_FIX_OFF=wobj.bundle` lab 20261106-044559 (no ground copy, no carried copy);
  PASS lab 20261106-044500 (ground copy at the host's place, the member's copy carries it 1-3.5 m behind, the food the
  guest's member took is gone on the host).
- Civ, host_bundle_civ: FAIL with the switch off lab 20261106-053931; PASS lab 20261106-053933 (the host's ground food
  by its city on the guest, a guest-only one gone).
- Detector: Tribe `unmatched:game_bundle` 77 % -> 68 % of the comparisons and `host_only` gone (lab 20261106-035845 ->
  20261106-051335); the counts now match within one (5 / 5, 6 / 5): the rows left are carried food compared at the
  static 3 m while its member walks, and a guest member's own carried food (BUG-0617). Civ `unmatched:game_bundle`
  3 % -> 7 % and one `guest_only` (lab 20261106-051337, one run, not compared further: see BUG-0617).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w42-civ-left): opened.
- 2026-11-06 (fix/w45-world-objects): fixed for ground and carried food (Tribe, Civ), tests host_bundle and host_bundle_civ; rows left in BUG-0617.
