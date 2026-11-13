---
id: BUG-1057
title: Creature: the guest's world player species is not moved to the host's evolved one when the species is not known in the first Creature frame
status: open
kind: bug
stage: Creature
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-23
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0508
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-23 (creations.cpp). Not seen at runtime, found by code reading.
After the host evolves its creature in place, the guest's game should write the host's new species into the world's
player species (BUG-0508). The write is tried once: if the new species is not known to the guest's game in the first
Creature frame after the bundle came (game::species_by_key null), the request is dropped and the guest's world keeps
the host's previous species (relations, "is the player's species" answers of BUG-0462 / BUG-0508 drift).

## Root cause
Observed in code (main efd2e0ba); whether species_by_key can be null in that frame is inferred (confidence medium):
mods/coop/creations.cpp:183 player_species_tick: `Key to = g_ps_to; g_ps_to = {};` (creations.cpp:188-189) comes
before `if (!game::species_by_key(...)) return;` (creations.cpp:192): a null species loses the request for good. The
request is set once per received bundle (creations.cpp:1896-1898).

## Fix idea
Clear g_ps_to only after the write or after a definite "another species since" (k[0] != from); retry while
species_by_key is null, for a few seconds. Switch idea `coop.fix creations.species_retry off`.

## How to test
creature fixture twoinst test of the in-place evolution (the BUG-0508 test, coop_evolve_inplace or similar): delay the
species' registration on the guest by one frame (a test hook) and assert the guest game's cSpeciesManager +0xA4 holds
the host's new instance afterwards. Main: the old one: FAIL.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
