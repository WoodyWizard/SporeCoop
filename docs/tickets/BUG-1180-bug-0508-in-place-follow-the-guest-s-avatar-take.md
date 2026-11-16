---
id: BUG-1180
title: BUG-0508 in-place follow: the guest's avatar takes the host's evolved species with SetSpecies; does it wait for a running cinematic as BUG-1173's swap does
status: duplicate
kind: bug
stage: Creature
reported: 2026-11-15
source: feat/w51-labstate
role: 
versions: 
fixed_in:
branch: feat/w51-labstate
commits: c2068f03, c6e27832
tests: evolve_in_place_cine (coop_evolve_in_place.py --case creature --cine)
signature:
docs: docs/re/creations.md#creations-follow-cinematic
related: BUG-1173, BUG-0508, BUG-0019
---

## Report
docs/re/creations.md#creations-swap-cinematic (BUG-1173) said: "Not covered: the in-place follow of the host's new
species (BUG-0508 `follow`) still calls SetSpecies without that wait." Asked (feat/w51-labstate): make that path wait
the same way and test it.

## Root cause
Observed (code reading, creations.cpp at rel/0.7.15 95948986): the follow sets no species itself. `on_bundle`
(BUG-0508) only records the host's new key (`g_follow`, with its stage epoch); `mine()` returns it as the guest's
creature (a guest without own creations or without a record for this world), and the one SetSpecies on the avatar
in creations.cpp is `apply_own_avatar` (`own_key_here` -> `mine`), where BUG-1173's wait
(`creations.swap_after_cinematic`, `world::cinematic_playing()`) comes before it. `player_species_tick` writes the
world's player species key (cSpeciesManager +0xA4), no SetSpecies. tribe.cpp's SetSpecies is the Tribe members'.
Observed at runtime (evolve_in_place_cine): the follow's swap is that call: "creations: the host's creature changed
065CBE70 -> 29243096: this game's world species follows it in place (BUG-0508)", then "own avatar 29243096 waits:
cinematic F4F96E43 plays (BUG-1173)", then "own avatar is now 29243096" 4 s later. So the docs line was wrong; the
in-place follow has waited since BUG-1173's fix.

## Fix
No mod change (BUG-1173's fix covers the path: duplicate). A test that drives it: `coop_evolve_in_place.py --case
creature --cine` (suite evolve_in_place_cine): after the guest left its own editor and before the host's done, the
host adds 250 DNA; the guest's own game plays a brain level cinematic (F4F96E43 in both runs) when the host's new
species arrives. Checked: the follow waited for that cinematic and swapped in place (before the host's world, which its
own brain upgrade makes it send after the cinematic, BUG-0019), and the guest's cinematic ends and its input is free.
CORRECTION in docs/re/creations.md#creations-follow-cinematic.

## Verification
* PASS: out/lab/20261115-203937 (lane 10: the follow waited for F4F96E43; that run failed only the in-place checks the
  host's brain upgrade breaks, dropped for --cine after it), out/lab/20261115-204255 (lane 9, PASS).
* Twin `SPORE_LAB_FIXOFF=evolve_in_place_cine=creations.swap_after_cinematic`: out/lab/20261115-204250 (lane 10, FAIL:
  the swap 1.8 s after the follow under the cinematic, "waited for None"). The player-visible harm of BUG-1173 (a
  brain upgrade caption that never comes) did not follow with this cinematic: it ended by itself and the input was
  free, so the twin fails on the wait, not on the picture. Not verified: CRG_BrainUpgrade1 (BUG-1173's cinematic) under
  the follow (the test's 40 DNA before the vote passes that level first).

## History
- 2026-11-15: opened.
- 2026-11-15: duplicate of BUG-1173 (feat/w51-labstate): the follow goes through apply_own_avatar and waits; test evolve_in_place_cine.
