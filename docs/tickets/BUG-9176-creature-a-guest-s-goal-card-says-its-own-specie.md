---
id: BUG-9176
title: Creature: a guest's goal card says its own species is extinct (the host-key copies all converted)
status: open
kind: bug
stage: creature
reported: 2026-11-13
source: test
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests:
signature:
docs:
related: BUG-9169 BUG-9174
---

## Report
Seen on the MSI as a guest (2026-11-13, screenshot): the goal card "Hunt 6 Lilku" turned into "The Lilku species is extinct!" (twice). Lilku is the player's own creature.

## Root cause
Observed: on the guest the host's key of the player's own creature (7CD76455, later 7C5EE376) is a second species with the same name; its only members are the copies of the player's family, and npc.own_family_species (BUG-9169) gives them all the player's own species. Inferred: the game counts the second "Lilku" as extinct once none is left of it, and its hunt goal (it is not the player's species there) reports it.

## Fix
Fixed by BUG-9180 (one key): the guest's creation is one species on its game, so no second "Lilku" goes extinct.

## Verification
no-fail-run: by BUG-9180; the MSI's goal card after it: "Hunt 6 Lilku" (the host's creature, also named Lilku), no "extinct" in the session.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed by BUG-9180.
