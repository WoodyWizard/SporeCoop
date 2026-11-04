---
id: BUG-0536
title: Creature: puppet_jump sees no hatch jump when the guest plays the world's species (BUG-0345 default)
status: wontfix
kind: test
stage: creature
reported: 2026-11-04
source: lab final 0.7.5
role: guest
versions: 0.7.5-dev
fixed_in:
branch: fix/w42-creature-left
commits:
tests: puppet_jump
signature:
docs: docs/re/creature.md#creature-world-species-hatch
related: BUG-0345, BUG-0376
---

## Report
With the BUG-0345 default (a guest plays the world's species in a world it never played its own creature in),
coop_puppet_jump saw no jump of either creature out of its egg after the respawn (out/lab/20261104-061516,
20261104-072946); with `creations.guest_world_species` off it passed (20261104-074723). The test now chooses the
guest's own creature (20261104-074957 PASS). Open: whether two players of one species hatch without a jump
or the puppet track misses it.

## Root cause
Observed (lab 20261104-114050, lane 6, `PUPPET_JUMP_WORLD=1`, the guest's world record cleared): with both players of the
world's species dying at the fixture's nest, the dying player's own game never moved its creature more than 1.5 m in
0.5 s in the 28 s after the death (sampled on that game with coop.avatar); the puppets were shown again 0.00 m off the
target. With the guest's own creature (lab 20261104-113744) its own game stepped it 6 m at 10.9 s (out of its egg) and
the puppet followed in 0.08 s. So it is the game: a creature of the nest's species hatches where it died, no step.
Inferred: the hatch step comes from an egg placed off the nest for another species. Not our track or puppet.

## Fix
None needed (the game). puppet_jump keeps choosing the guest's own creature; `PUPPET_JUMP_WORLD=1` runs it with the
world's species and prints the dying game's own steps (commit 9828917b).

## Verification
no-fail-run: not a bug of ours; evidence out/lab/20261104-114050 (world species, no own step) and out/lab/20261104-113744 (own creature, 6 m step caught in 0.08 s).

## History
- 2026-11-04: opened.
- 2026-11-04: analysed on fix/w42-creature-left: the game does not step a hatchling of the nest's species; closed as not ours.
