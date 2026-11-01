---
id: BUG-0384
title: Creature: eggs at other species' nests differ between host and guest and some float in the air
status: open
kind: desync
stage: Creature
reported: 2026-11-01
source: player report 2026-11-01 (host F9 note at s 1907)
role: both
versions: 0.7.3.3
fixed_in:
branch: fix/w30-npc-walk
commits:
tests: tools/tests/coop_nest_objects.py (not in a suite)
signature:
docs:
related: BUG-0332
---

## Report
A 0.7.3.3 session (2 players, Creature): the host noted "in enemy nests eggs are desynced and bugged
(flying mid air)".

## Root cause
Partly observed (fix/w30-npc-walk, lane 5, by hand, creature fixture): `coop_nest_objects.py` takes both
avatars to the three nearest nests and lists every game object within 20 m on both games
(`coop.npctrack objects`: noun, position, height over the ground).
* The objects of noun 0x2C9CC91 (cGameData: the small food objects; at nests most likely the eggs, about
  0.5 m over the ground, ~) are each game's own: the mod does not sync them. Their numbers differed:
  14 / 13, 6 / 9 and 12 / 8 (host / guest) at the three nests; the objects both had were at the same places.
  So eggs eaten (or not yet laid) on one machine stay on the other: "desynced" fits (inferred).
* At the third nest 4 of them were 3.0-4.1 m over the ground on BOTH games (the same objects): floating
  is not a guest-only effect there; whether the game itself places them so (a slope, a nest on a changed
  ground) is not known.
Not checked: that 0x2C9CC91 objects at nests are really the eggs (a screenshot did not show the nest), the
player's world (its nests were not available).

## Fix
None yet. A fix would sync the nest food objects (host-authoritative eaten / laid, like the NPCs) or
hide the guest's own; not started.

## Verification
no-fail-run: not fixed.

## History
- 2026-11-01: opened.
