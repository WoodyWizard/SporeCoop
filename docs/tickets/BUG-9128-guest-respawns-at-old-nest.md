---
id: BUG-9128
title: Creature: after a migration the guest respawns at the original nest
status: fixed
kind: bug
stage: Creature
reported: 2026-11-11
source: owner report on 0.7.8-lineages49-dev
role: guest
versions: all
fixed_in: 0.7.8-lineages50-dev
branch: feat/lineages-47
commits:
tests: QA Samuth.spo host 9 + guest 8: host `coop.nest home 453.12 46.02 -223.02` (joins that nest's herd, as a claim does), guest `coop.nest home`, guest `coop.giveup down` + `coop.giveup`, position after the respawn; nest.follow_home off / on
signature: guest `coop.nest home` differs from the host's after a migration
docs: docs/re/creature.md#creature-nest-migration-follow
related: BUG-0018 BUG-0019
switch: nest.follow_home
---

## Cause
A game moves its avatar into the new nest's herd only when its own avatar reaches the new nest with the migration
under way (NestTick 0xD41510 -> CreatureGame_JoinNestHerd 0xD40C70, docs/re/creature.md migration notes). The
host leads the migration; the guest's game often never claims (its world arrived during CRG_SpeciesMigrate with
state 1, or its creature came later), so its avatar kept the old herd, and the native respawn uses the avatar
herd's nest.

## Fix
nest.cpp: the host sends its home nest (MSG_NEST kind 2, f32 x y z, 1 Hz with the egg snapshot). A guest whose own
home nest is more than 15 m from it (Creature, alive, no cinematic, at most every 10 s) calls JoinNestHerd on its
own copy of that nest (the nearest cNest within 15 m with a herd: the save's nests are at the same places), without
CRG_ClaimNest. Log "nest: our home nest follows the host's (...)". `coop.nest home [x y z]` shows / sets it (tests).

## Verification
Host home moved to (453 46 -223):
* off: guest home stays (443.1 -169.6 -183.5); after a give-up the guest respawned at (445.75 -164.74 -181.49),
  the old nest (the report).
* on: guest home (453.1 46.0 -223.0) within 15 s, joins=1; after a give-up it respawned at (453.47 52.17 -221.05),
  the new nest. 0 faults on both, no crash file.
Not covered: a real migration (CRG_SpeciesMigrate, the goal, the claim on the host).
