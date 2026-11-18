---
id: BUG-9234
title: Creature: a migration loads the world twice for a guest with its own species (at the start, then its new nest)
status: fixed
kind: bug
stage: creature
reported: 2026-11-15
source: owner play test 2026-11-15 (59-dev)
role: guest
versions: 0.7.14.1-lineages59-dev
fixed_in: 0.7.14.1-lineages60-dev
branch: feat/lineages-47
commits: 6dc24d1b
tests: none yet (a real migration is needed)
signature:
docs:
related:
---

## Report
Owner 2026-11-15: "is it possible to stop the double world loading we have at times? Like when the player is loading into their new nest?" Host log (59-dev play test): BigTexas got the world at the join (18:18), when its evolution started a migration (18:22, "evolved: BigTexas gets the world (a migration started)") and again when the host reached the new nest and the guest's own nest was made there (18:30, "seat 1's world goes out 14 s after its nest was made").

## Root cause
Observed in the log and the code: home_nests makes a seat's nest within kNear of the host's current home (the avatar's nest). During a migration that home is the old nest until the host claims the new one, so the migration's world (needed: the guest's game learns the migration only from a world) went out with the old nest, and the claim later made the guest's nest at the new site and sent the world again.

## Fix
- While the migration property 0x514A219 is 1 or 2, host_home() is the migration's target nest (CreatureGame_NestHerdFor(1), the nest the "Migrate" goal points at): the guest's nest is made there at the migration's start and the migration's world waits for it (BUG-9210 hold), so the guest loads once. Switch creature.nest_at_migration_target.

## Verification
no-fail-run: built only; a migration on TITAN + the MSI is still to be run

## History
- 2026-11-15: opened.
- 2026-11-15: built, not tested yet.
