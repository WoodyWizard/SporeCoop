---
id: BUG-0010
title: Creature: the guest does not see a companion of a species only the host has
status: fixed
kind: desync
stage: Creature
reported: 2026-10-25
source: player screenshots
role: guest
versions: 0.7.2.1
fixed_in: 0.7.2.3
branch: fix/creature-desync
commits: dbd1d3d, bb915f1
tests: creature_desync
signature: 
docs: docs/re/creature.md 11
related:
---

## Report
"He doesn't even see my companion."

## Root cause
An NPC of a species only in the host's profile (own creations, downloads) failed to spawn on the guest ("unknown species").

## Fix
The guest asks the host for unknown species (MSG_CREATION kind 4/3), registers them and builds the NPC.

## Verification
coop_creature_desync.py: the companion stayed pending 3x before, built after.

## History
- 2026-10-25: reported (player screenshots).
- 2026-10-25: fixed on fix/creature-desync, released in 0.7.2.3.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-11-15: switch `creations.ask_species` retired in 0.7.15 (B1, BUG-1138): the old code path is gone, FAIL run 20261108-074108:creature_desync on record; its test runs PASS-only.
