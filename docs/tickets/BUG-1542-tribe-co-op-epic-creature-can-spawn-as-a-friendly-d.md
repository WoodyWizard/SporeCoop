---
id: BUG-1542
title: Tribe: co-op epic creature can spawn as a friendly/domesticated species near the village
status: open
kind: bug
stage: Tribe
reported: 2026-11-24
source: Discord bug report 2026-11-24 (SporeCoop-report-20261124-214704)
role: both
versions: 0.7.16, 0.7.16.1, 0.7.99
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0121, BUG-0146, BUG-0515, BUG-0563
---

## Report
During Tribe stage, when the co-op epic encounter triggers ("an epic creature comes! together: 80 food"), the spawned
epic creature can be friendly to the player tribe instead of hostile.

## Root cause
In `mods/coop/tribe.cpp` (`epic_spawn`), the species profile for the epic is chosen as the `cCreatureAnimal` closest to
the player village (`d < pk->bd`). In Tribe stage, domesticated pets in the village pen (or allied/friendly species near
the village) are closest to the village, so the epic spawns using a friendly/domesticated species and herd without
setting hostile relationship with the player tribe.

## Plan (0.8)
Skip domesticated pets and allied/friendly species when selecting the epic's species profile, and ensure the spawned
epic herd is hostile to the player tribe.
