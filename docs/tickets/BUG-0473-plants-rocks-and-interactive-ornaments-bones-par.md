---
id: BUG-0473
title: plants, rocks and interactive ornaments (bones / part skeletons) near the players differ between the games
status: open
kind: desync
stage: Creature
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, report out/detector/20261103-0236.md)
role: 
versions: 
fixed_in:
branch: fix/w40-world-objects
commits:
tests: wobj_ornament, detect_creature
signature:
docs: docs/re/creature.md#creature-world-objects, docs/06-testing.md#testing-world-objects
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

## Fix
The bones: worldobj.cpp kind `interactive_ornament` (see BUG-0459). Plants and rocks: no kind yet
(docs/known-issues.md#issue-world-objects-left).

## Verification
The bones: see BUG-0459 (wobj_ornament). Plants (`game_plant` 71-96%) and rocks (BUG-0498) not done.

## History
- 2026-11-03: opened.
- 2026-11-03: fix/w40-world-objects: partly fixed (worldobj.cpp / nest.cpp).
