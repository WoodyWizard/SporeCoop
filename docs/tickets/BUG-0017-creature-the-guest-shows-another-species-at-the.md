---
id: BUG-0017
title: Creature: the guest shows another species at the host's nests ("Datzu")
status: fixed
kind: desync
stage: Creature
reported: 2026-10-25
source: player screenshots (both screens)
role: guest
versions: 0.7.2.3
fixed_in: 0.7.2.4
branch: fix/nest-species
commits: 82180c1
tests: nest_species
signature: 
docs: docs/re/creature.md 15
related:
switch: creations.own_species
---

## Report
At the same nest the host saw Woopsies, the guest other creatures and "The Datzu are your allies!".

## Root cause
Creation ids are per profile and collide; the guest's game resolved the host's creation key to its own creation (and caches by key).

## Fix
The host sends its own creations used by the world (MSG_CREATION kind 5); the guest registers them under its own stable keys and builds the host's NPCs from them.

## Verification
coop_nest_species.py FAIL before (wrong body), OK 2/2 after.

## History
- 2026-10-25: reported (player screenshots (both screens)).
- 2026-10-25: fixed on fix/nest-species, released in 0.7.2.4.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: switch: creations.own_species (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
