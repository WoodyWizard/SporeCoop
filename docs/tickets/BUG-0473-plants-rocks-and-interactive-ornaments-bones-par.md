---
id: BUG-0473
title: plants, rocks and interactive ornaments (bones / part skeletons) near the players differ between the games
status: fixed
kind: desync
stage: Creature
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, report out/detector/20261103-0236.md)
role: 
versions: 
fixed_in: 0.7.4
branch: fix/w40-world-objects, fix/w40-cell-evo
commits: eca11f6d, e136676f
tests: wobj_ornament, detect_creature, world_lod
signature:
docs: docs/re/creature.md#creature-world-objects, docs/re/creature.md#creature-plants-rocks-lod, docs/06-testing.md#testing-world-lod
related:
switch: wobj.ornament
---

## Report
Detector: `game_plant` unmatched / on one game only in 20-30% of Creature comparisons; `interactive_ornament` (bones, skeletons that give parts) only on the host in 6% (up to 12 in the host's game, 0 in the guest's), `rock` on one game only in 11%. The part pickups match BUG-0459 (a bone picked up by one player is still there for the other): the detector sees the pickups themselves differ. Class G4.
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed: the interactive ornaments are the same on both games after a load (lane 9: 12 of them, same serials);
the detector's `host_only:interactive_ornament` comes from the guest's area being outside the host's (seen 6%),
not checked further. The bone half of this ticket is BUG-0459. Plants (`game_plant` 0x18C84A9, next to the big
fruit trees, often on the guest only) and rocks were not analysed.
Plants and rocks (fix/w40-cell-evo, observed, coop_world_lod.py out/lab/20261103-161705): not shared world
objects. Rocks: 1 in the save, in both games; 28 more appear on the walking guest only and never on the host that
stood 1 m from it at the start. Plants (`game_plant`): 0-2 per whole list, always 8-14 m from that game's own
player, never in the other game. Each game makes them around its own camera (who places them in the code: not
looked up). Not a desync a player can use (nothing to eat or pick up was seen in them; ~ inferred).

## Fix
The bones: worldobj.cpp kind `interactive_ornament` (see BUG-0459). Plants and rocks: no kind yet
(docs/known-issues.md#issue-world-objects-left).
Plants and rocks: no worldobj kind; the detector leaves `rock` and `game_plant` out (worlddiff.IGNORE, with the
evidence) (fix/w40-cell-evo).

## Verification
The bones: see BUG-0459 (wobj_ornament). Plants (`game_plant` 71-96%) and rocks (BUG-0498) not done.
Plants and rocks: world_lod PASS out/lab/20261103-161705 (the evidence run; it fails when one-game-only objects
lie near the other player).
no-fail-run: a detector compare rule (worlddiff.IGNORE), not game code; the evidence run is world_lod above.

## History
- 2026-11-03: opened.
- 2026-11-03: fix/w40-world-objects: partly fixed (worldobj.cpp / nest.cpp).
- 2026-11-03: fix/w40-cell-evo: plants and rocks shown to be per camera; left out of the detector.
- 2026-11-03: released in 0.7.4.
