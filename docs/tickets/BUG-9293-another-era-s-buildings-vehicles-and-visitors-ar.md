---
id: BUG-9293
title: Another era's buildings, vehicles and visitors are solid (decision 7H A)
status: fixed
kind: feature
stage: creature
reported: 2026-11-20
source: decision 7H A (docs/ROADMAP.md)
role: any
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits:
tests: in game: TITAN QA runs (coop.visit walk / onstretch / status: avatar in each prop's frame)
signature:
docs: docs/12-server-and-players.md#server-era-visitors-solid
related: BUG-9290, BUG-9291, BUG-9287
---

## Report
Decision 7H A (the owner, 2026-11-20): another era's creatures, buildings and vehicles are solid: a player cannot
walk or drive through them. Plus the owner's "increase the size of the hitbox based on how fast it's going to adjust for
latency".

## Root cause
Feature. The streamed buildings and vehicles are look-only ornaments with no physics body (BUG-9290); a visitor's
creature is a creature of this game (Havok's own collision) but trails its player (5-6 m walking, 10-27 m for a running
Tribe chief at 10 FPS).

## Fix
era_visitors.cpp: once a frame this game's own creatures (Creature: the avatar; Tribe: the player tribe's members on
the game that simulates them) are pushed out of each streamed prop's box (spatial vt 0x68, the prop's own frame, turned
with it, on the ground's tangent plane, the height kept) by its nearest side, with game::place (SetPosition alone was
undone by the locomotion next frame). A creature up to 15 m below a prop's base is still blocked (a slope, or another
world's lower ground). A visitor's block reaches on from its creature to where its player last said it was (at most 30
m, where the visitor is put there anyway). The boxes are read once a frame. Switch `galaxy.visitor_solid`. Test commands
`coop.visit walk <n>` (the avatar walks into streamed object n), `coop.visit onstretch` (the avatar onto the next
visitor's stretch of 4 m or more), `coop.visit go <x y z> [run|tp]`; the status prints each prop's box and the avatar in
its frame, the pushes and the nearest stretch.

## Verification
TITAN QA 8 hosting Abber (Civ), QA 9 on Abber (Creature), QA 7 on Abber (Tribe), 2026-11-20 (in game, not the lab):
- Buildings, FAIL (switch off): `coop.visit walk 3`: the avatar reached the building's middle (-0.5, 0.0 in its frame).
  PASS (on): held at the wall (x -9.1..-9.7, the box edge -8.7 minus its radius) for 30 s; turned on again with the
  avatar inside: put out to -9.7 at once. Rerun on the final build (boxes read once a frame): the avatar wedged between
  two buildings, held at both boxes' edges (+9.7 and -9.7); that run's switch-off half does not count (the game had
  gone to the History screen, the avatar could not walk).
- The stretch, FAIL (off): `coop.visit onstretch` put the avatar on a 4.5 m stretch of the Tribe chief walking home: 0.00 m
  moved. PASS (on): a 4.2 m stretch: 5 pushes, 1.27 m moved off. (A walk across the chief's path by hand missed by
  3-4 m each time: the chief's own way home varies.)
- Not run: the Tribe stage's members against a prop (the code path is the same push; no member walked into one).

## History
- 2026-11-20: opened.
- 2026-11-20: built; seen in game (buildings, the stretch).
