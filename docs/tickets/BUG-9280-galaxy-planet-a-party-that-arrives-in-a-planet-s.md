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
Built, not proven: `tribe.nest_anywhere` (tribe.cpp nest_founding_site): another party's player founds at its own nest
however far from the host's village, level with that nest's ground.

CORRECTION (2026-11-19 23:17, TITAN rerun on the Tribe world saved before QA 9 joined): no founding ran for QA 9 at all.
The village by the host's was there already: QA 8's Tribe start (in its crossing) made the villages of the founder seats
its Creature save carried, and that save had a partner seat ("Pandize 2") whose nest was 40-80 m from QA 8's (decision
1B, the co-op party rule), from earlier QA sessions. So the two real problems are: (1) a crossing party's Tribe start
also makes villages for founder seats of players who are not in the crossing (other parties, still in Creature or long
gone); (2) on a galaxy planet a stranger's nest is placed by the host's nest as if they were partners (1B). Decision 7E
(where a new player's nest goes on a shared planet) comes first; then (1) is a filter on the founder seats at the
Tribe start (the crossing's members only), and the arriving party founds at its nest with nest_anywhere.

## Verification
Not yet (see the CORRECTION).

## History
- 2026-11-19: opened (seen in BUG-9278's join test).
