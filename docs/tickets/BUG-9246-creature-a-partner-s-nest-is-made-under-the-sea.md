---
id: BUG-9246
title: Creature: a partner's nest is made under the sea
status: open
kind: bug
stage: creature
reported: 2026-11-16
source: owner 2026-11-16
role: host
versions: 0.7.14.1-lineages63-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: manual Windows QA (TITAN, Dorger.spo copy, coop.ground, coop.homes)
signature:
docs:
related: BUG-9167, BUG-9173, BUG-9245
---

## Report
Owner 2026-11-16, watching the TITAN QA guest (the Dorger.spo copy): "...is the nest underwater?..." and "Just look at the minimap": the partner's nest and its creature sit past the shore line, in the sea.

## Root cause
Observed (TITAN QA, `coop.homes`, `coop.ground`): seat 1's nest at (439 186 -143), 45 m from the host's: ground 497.88, the water surface 499.98 (game::water_radius, BUG-1088): about 2 m under water. The host's nest ground is 503.73, 3.75 m over the water. home_nests site_near (decision 2, BUG-9167, BUG-9173) takes a spot within 6 m of the host's nest height and never looks at the sea level, so any host nest under 7 m over the water can get a partner nest in the sea.
Effects seen the same evening: the partner respawned at that nest and died several times in a few minutes, by the deep-water sea monster ("Dangerous creatures lurk in the deep", drama kind 6) and by starving; a summoned flock beside it drowned (2 of 3 dropped in 3 minutes), which also left a dead creature in the summon event's list (BUG-9245).
The nest is noted per planet (worlds.ini "nest<seat>", BUG-9173) and found again by its species (nest_herd_near), so a nest already in the sea stays there in later sessions.

## Fix
- creature.nest_site_dry (home_nests.cpp site_near): a new nest spot and the ring of land around it must be 1 m over the water (game::water_radius; no check when it is unknown); the closest-height fallback takes dry spots only. Off: 63-dev.
- Not done: moving a nest that is already in the sea (the owner's world has one). Removing it like the old-nest ring (BUG-9182) is unsafe here: the partner's puppet and its pack members are in that nest's herd. Decision 5A.

## Verification
Built only (the new-nest path was not exercised: the QA world's seat nest already existed). FAIL evidence: the readings above (63-dev).
no-fail-run: not run yet.

## History
- 2026-11-16: opened from the owner's question; cause found; new nests fixed (not run); existing nests open (decision 5A).
