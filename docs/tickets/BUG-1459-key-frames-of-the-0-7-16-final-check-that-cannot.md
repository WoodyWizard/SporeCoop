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
branch: fix/w84-tests
commits: f8cbe143
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
Test code only (fix/w84-frames, merged into fix/w84-tests), no mod change:
* Baby views, cameras in a bush or a head: `keyviews.clear_view` / `look_both_clear` (new) pick the camera round the
  subject (8 azimuths at 12 m / 5 m up, then 9 m up, then 8.4 m away 12 m up) whose line of sight passes no placed game
  object (`coop.census all 40 <subject>`: huts, buildings, plants, rocks as upright cylinders 3 m wide, 6.5 m high), no
  other creature or member (`coop.census creatures`, 1.3 m / 2.8 m) and no terrain (`coop.ground` at 5 points of the
  line: the terrain and the snap surface). `coop_tribe_own_species.closeup` (tribe_birth_ask, tribe_birth_species,
  tribe_kid_species baby; tribe_own_species carrying; tribe_own_outfitter host_village; line_stages member views) uses
  it; the frame's manifest gets a line `view: clear line of sight (...)` or `view: NO clear line of sight found
  (... blocked by ...): the subject may be hidden in this frame`. The object sizes are generic (the census has no
  bounds): a heuristic, checked by eye in the next key frame review.
* civ_citizen_trail run frames: the same choice on the objects only (no coop.ground calls: the citizen runs 8 m/s),
  then the camera set round the citizen's position read again; the manifest line says which view was taken. Whether a
  city wall is a census object is not known: the review of run1/run2 tells.
* line_stages_outfit 09-11: `play_civ.answer_talk` (new) answers a nation's waiting talk as the campaign's bots do
  (its trade offer accepted, a surrender accepted, its goodbye) on both games before the citizen close-ups and the
  vehicle frame (`talk_before_frames`); printed as "a nation's talk answered before the key frames".
* species_line next_session: the frame waits up to 20 s until the guest's avatar is its own creature (`coop.avatar`
  key = its `mine creature` key, or at least not the host's avatar species); the manifest says after how long or that it
  was not seen. The note now says what to see (the guest with its own creature).
* own_editor_civdrop 01 / 02 (civdrop_alone, civdrop_rejoined): after frame_city (it answers a specialty box) the test
  camera on the guest's city on both games (70 m off, 45 m up, the city in the middle), then the games' own cameras
  again (lab 20261122-194210 02: the guest's picture had the city at its edge).
* line_stages_editors 10 (unpainted vehicle): expected by the test's design, unchanged.

## Verification
Open, pending night run: tribe_birth_ask, tribe_birth_species, tribe_kid_species, line_stages_outfit, species_line,
civ_citizen_trail, tribe_own_species, tribe_own_outfitter, own_editor_civdrop:
`SPORE_DETECTOR=20 python3 tools/lab.py run tribe_birth_ask tribe_birth_species tribe_kid_species line_stages_outfit
species_line civ_citizen_trail tribe_own_species tribe_own_outfitter own_editor_civdrop --lanes 4 --retry 0 --no-build`
(or `--lane N` one at a time), then review the frames of each (frames/sheets): the baby, the carrying member and the
host's member in the picture with no wall, bush or head in front; no nation's talk over line_stages_outfit 09-11;
species_line next_session with the guest in its own creature; civdrop 01 / 02 with the guest's city in the middle;
every close-up's manifest line `view: ...` (a `NO clear line of sight` line names what blocked it).
no-fail-run: key frame cameras and timing only (no check changed).

## History
- 2026-11-23: opened (fix/w66-frames, key frame review of the 0.7.16 final check, lab 20261122-194210 on 5376d388).
- 2026-11-23: fix/w84-frames (WP-T1): clear-view cameras, nation talk answered, avatar swap waited, civdrop test camera; waits for the night run and the frame review.
