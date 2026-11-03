---
id: BUG-0517
title: the host's avatar keeps its pre-editor species after its evolution editor until the game is saved
status: open
kind: bug
stage: Creature
reported: 2026-11-03
source: test (coop_evolve_in_place.py, fix/w40-no-reload)
role: host
versions: 
fixed_in:
branch: fix/w40-no-reload
commits:
tests: evolve_in_place_creature (coop_evolve_in_place.py --case creature)
signature:
docs: docs/re/creations.md#creations-evolve-in-place
related: BUG-0508, BUG-0457
---

## Report
Found while making BUG-0508: with the world share after the host's evolution editor skipped, the host's own
avatar kept the pre-editor species (065CBE70) for 2+ minutes (the trace's `species=`), although the game's world
player species (cSpeciesManager +0xA4) was the new one (291433C0) and the new creation was in the host's My
Creations (lab 20261103-173652). With a save of the game (the share's save of 0.7.3.6, or the one BUG-0508's fix
makes) the avatar takes the new species 0.6-1.7 s later (20261103-173315, 20261103-174221).

## Root cause
Observed: the swap follows the save (MessagePost 0x1CD20F0); creations.cpp did not call SetSpecies on the host
(swaps=0). Not looked up: what in the save applies it (the stage's return 0xD43DE0 / its stored result this+0x60,
a mode round trip inside the save, or an autosave of the game after the editor that the mod's request path does not
get). Inferred, not verified: a host playing alone (no guest: no share, no save) or with `coop.share off` keeps its
old creature after an evolution; the editor request here is `coop.vote evolve` (the mod's copy of the game's
request), the game's own request at the nest was not tried. A possible cause of BUG-0457's 'previous version'.

## Fix
Worked around for co-op by BUG-0508 (world.evolve_in_place: the host still saves once per evolution in Creature).
Not fixed for a host without guests.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
