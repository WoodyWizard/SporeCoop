---
id: BUG-9289
title: Era visitors: name tags; walking lag measured
status: fixed
kind: feature
stage: creature
reported: 2026-11-20
source: step 4 part 5 polish
role: any
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: e2e7c598
tests: tools/tests/galaxy_server_check.py (the stream); in game: TITAN QA runs (the tag)
signature:
docs: docs/12-server-and-players.md#server-era-visitors-more
related: BUG-9287, BUG-0929
---

## Report
A visitor had no name over it, and it trailed its player while walking.

## Root cause
Feature. The trailing: at 2 VISITs a second and 10 FPS it stays 5-6 m behind on average.

## Fix
A grey name tag over each visitor within 120 m (its name, "<Era> era"), at its pose's top as a partner's tag
(modes::creature_tag_height). Switch `galaxy.visitor_tags`. Aiming 0.5 s ahead on the path was tried and taken out: the
mean gap was 5.2 m with it, 5.7 m without, over 4 walks each (no real change).

## Verification
- no-fail-run: the switch-off run was not done (the tag is new drawing, nothing drew it before). PASS: the tag "Pandize -NoShare- / Tribe era" over the Tribe player's
  creature in the Creature game (TITAN, 2026-11-20).
- no-fail-run: the walking lag is a measurement (no change kept).

## History
- 2026-11-20: opened.
- 2026-11-20: built and seen in game / in the server checks (TITAN).
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
