---
id: BUG-9276
title: Civ own nations: a partner who gets a species of their own on a planet with no free city site gets no nation (it shares the host's)
status: open
kind: bug
stage: Civ
reported: 2026-11-20
source: found in the port of BUG-9274 (lane C, 2026-11-20)
role: 
versions: 0.7.15.5 (feat/lineages-civ)
fixed_in:
branch: feat/lineages-civ
commits:
tests:
signature:
docs: docs/re/civ.md#civ-own-nations
related: BUG-9274
---

## Report
With every player's own nation (BUG-9274), the host founds a partner's capital at the game's next free city site. On a
planet whose ten sites are all founded (the dev Kealeneon Civ save on lane C: 10 cities, cCivExpansion +0xB0 = 0) a
partner who joins with a creature of their own gets no nation: the host logs "Bob's capital waits: no free site" and the
partner plays the host's nation (the 0.7.15 split).

## Root cause
Observed: SpawnNextCity takes only a free site; none is left. Lineages had the same limit (its late capital: "No site
left: a notice").

## Fix
Not fixed. Candidates: give the partner an NPC city (a purchase-free capture of the weakest NPC city, its nation taking
the partner's species), or found a city at a new place (the site record's position field is not known yet,
docs/re/civ.md#civ-multi-founders in Lineages).

## Verification
no-fail-run: not fixed

## History
- 2026-11-20: opened (lane C, Kealeneon).
