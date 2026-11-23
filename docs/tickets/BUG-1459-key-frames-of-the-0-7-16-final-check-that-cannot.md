---
id: BUG-1459
title: key frames of the 0.7.16 final check that cannot be judged: baby views behind the hut, an NPC offer over line_stages_outfit, species_line next_session before the guest's swap, cameras in walls
status: open
kind: test
stage: All
reported: 2026-11-23
source: lab
role: test
versions: 0.7.16
fixed_in:
branch:
commits:
tests: tribe_birth_ask, tribe_birth_species, tribe_kid_species, line_stages_outfit, species_line, civ_citizen_trail, tribe_own_species, tribe_own_outfitter, own_editor_civdrop
signature:
docs:
related: BUG-0924, BUG-0984, BUG-1220
---

## Report
Key frame review of the 0.7.16 final check, lab 20261122-194210: frames whose note the picture cannot hold (no mod
bug seen in them, the frame just cannot be judged):
* Baby views: `out/lab/20261122-194210/frames/sheets/053_tribe_birth_ask_02_baby.jpg`, `054_tribe_birth_species_02_baby.jpg`,
  `065_tribe_kid_species_02_baby.jpg`: the camera 12 m from the new baby looks through the wall of the hut; the baby is
  never in the picture. The same in every run since lab 20261120-041304 (closeup() of coop_own_editors_robust.py:
  pick a direction whose line of sight to the baby is clear, or a higher eye).
* line_stages_outfit 09 / 10 / 11 (`041_..`, `042_..`, `043_line_stages_outfit_11_civ_guest_vehicle.jpg`): an NPC
  nation's trade route offer ("Red Nation": accept / reject) is open on the host and covers its view (the world blurred
  behind it); keyframe_settle neither waits for it nor closes it.
* species_line next_session (extra frame `frames/species_line/02_next_session_*.jpg`): shot 2 s after the guest's world
  appeared, before the guest's game swapped its avatar to its own creature (the swap comes about 3 s after the world:
  lab 20261119-214823 / 20261120-041304 at s 7.0, keyframe after it, guest green; since lab 20261120-133703 the frame
  comes at s 6.0, the guest still in the host's species on its own screen, the host already draws it as its own
  creature), and the test restores the profile right after. Wait for the guest's `own avatar species ... -> <own>` (or
  `coop.creations` mine) before the frame.
* Cameras in a wall, a bush or a head: `016_civ_citizen_trail_02_run2.jpg` (in a city wall), `079_tribe_own_species_03_carrying.jpg`
  (in a bush, member #2 not visible), `077_tribe_own_outfitter_03_host_village.jpg` (inside the chief's head);
  own_editor_civdrop 02 (the guest's camera away from its city).
* Not judgeable but expected by the test: line_stages_editors 10 (the guest's editor-made vehicle is unpainted on both
  games: the test's design has no paint).

## Root cause
Test scenes (camera placement, key frame timing, a game dialog), not the mod.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened (fix/w66-frames, key frame review of the 0.7.16 final check, lab 20261122-194210 on 5376d388).
