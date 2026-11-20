---
id: BUG-9280
title: Galaxy planet: a party that arrives in a planet's Tribe era gets its village next to the host's, not at its own nest
status: open
kind: bug
stage: tribe
reported: 2026-11-19
source: TITAN test 2026-11-19 (BUG-9278)
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
TITAN 2026-11-19 22:47 (images/stage4_join_tribe_host_view.png): QA 9's party ended Creature and joined QA 8 in
Samuth (Tribe) as late joiners; QA 9's village was founded next to QA 8's village. The owner's rule (2026-11-19, 6Z):
"don't they upgrade their nest to the next stage? It's not like they get a randomly placed new one right?" A party
arriving in an era should get its village at its own nest's spot.

## Root cause
Inferred: the late joiner's village site (docs/re/tribe.md, the late-join village notes) takes the player's own
nest herd only within 400 m of the host's village, else a spot near the host's village. A party arriving from its
crossing has its nest anywhere on the planet. Also to check: the Tribe world was made from another party's save, so
the arriving party's nest may not be in it at all (then its nest position has to come from its own Creature world).

## Fix
Not yet.

## Verification
Not yet.

## History
- 2026-11-19: opened (seen in BUG-9278's join test).
