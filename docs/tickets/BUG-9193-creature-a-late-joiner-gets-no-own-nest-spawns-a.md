---
id: BUG-9193
title: Creature: a late joiner gets no own nest, spawns at the host's, and the host's family goes into cocoons while the joiner is in the editor
status: fixed
kind: bug
stage: creature
reported: 2026-11-13
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 64c3afed
tests: tools/qa_win (to run: a late joiner into Creature)
signature:
docs:
related: BUG-9128 BUG-9181 BUG-9173
switch: creature.place_at_own_nest creature.family_own_species
---

## Report
Owner live test 2026-11-13 20:31-20:55 (their own PC hosting Vandh.spo in Creature; two test guests from the MSI joined late: MSIGuest with a creature, MSIJoiner without one, editor first). "instead of having their own nest near the host's, they spawned with the host and the host's family started their cocoon evolve."

## Root cause
Observed (logs, a subagent's analysis): (1) a guest loads a copy of the host's save, so its avatar stands where the host's stood; nest.cpp follow_home only joins the own nest's herd (the home for respawns), nothing moves the body (MSIGuest 20:34:20 joined its herd at (265 -423 -92) while drawn 2 m from the host). (2) MSIJoiner had no creature and played the host's species 291EE5B5; evolve.cpp family_in wraps every creature of the puppet's species within 50 m: the host's family (host 20:34:14 "5 of a partner's family into the cocoon").

## Fix
creature.place_at_own_nest (on): once per world, when the own nest is known and the body is more than 30 m from it, the guest's avatar goes 4 m from it (nest.cpp place_at_home). creature.family_own_species (on): a partner whose puppet plays this game's species puts nobody into a cocoon (evolve.cpp family_in).

## Verification
no-fail-run: not run again live yet (the owner tests the build on their install).

## History
- 2026-11-13: opened.
