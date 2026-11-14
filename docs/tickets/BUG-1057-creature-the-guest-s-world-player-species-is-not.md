---
id: BUG-1057
title: Creature: the guest's world player species is not moved to the host's evolved one when the species is not known in the first Creature frame
status: fixed
kind: bug
stage: Creature
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-23
role: guest
versions: 
fixed_in: 0.7.14
branch: fix/w49-small
commits: f4aca716
tests: evolve_in_place_late
signature:
docs: docs/06-testing.md#testing-t1-small
related: BUG-0508
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-23 (creations.cpp). Not seen at runtime, found by code reading.
After the host evolves its creature in place, the guest's game should write the host's new species into the world's
player species (BUG-0508). The write is tried once: if the new species is not known to the guest's game in the first
Creature frame after the bundle came (game::species_by_key null), the request is dropped and the guest's world keeps
the host's previous species (relations, "is the player's species" answers of BUG-0462 / BUG-0508 drift).

## Root cause
Observed in code and reproduced with a test hook: player_species_tick cleared the request before species_by_key; a
species not known in that frame lost the write and the guest's world kept the host's previous species. Whether the
species can really be unknown in that frame on a player's PC is not seen (the hook forces it).

## Fix idea
Clear g_ps_to only after the write or after a definite "another species since" (k[0] != from); retry while
species_by_key is null, for a few seconds. Switch idea `coop.fix creations.species_retry off`.

## How to test
creature fixture twoinst test of the in-place evolution (the BUG-0508 test, coop_evolve_inplace or similar): delay the
species' registration on the guest by one frame (a test hook) and assert the guest game's cSpeciesManager +0xA4 holds
the host's new instance afterwards. Main: the old one: FAIL.

## Fix
The request stays while the species is unknown, for up to 10 s (cleared on a write, on another species / a load, or
after the 10 s with a log line). Switch `coop.fix creations.species_retry off` (0.7.13). Test command
`coop.creations ps_null <n>`; the status line has `retries` / `lost`.

## Verification
Test `evolve_in_place_late` (coop_evolve_in_place.py --case creature --species-late 30: the guest takes the host's new
species as unknown for its first 30 tries).
FAIL (SPORE_FIX_OFF=creations.species_retry): out/lab/20261113-032124. PASS: out/lab/20261113-031843, out/lab/20261113-033100-2.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-small (package T1).
- 2026-11-14: released in 0.7.14.
