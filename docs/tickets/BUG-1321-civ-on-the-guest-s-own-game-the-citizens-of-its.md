---
id: BUG-1321
title: Civ: on the guest's own game the citizens of its city are drawn white (no paint) while the host's game paints them
status: fixed
kind: bug
stage: Civ
reported: 2026-11-19
source: lab
role: 
versions: 
fixed_in: 0.7.16
branch: fix/w57-civlook
commits: 0452cf09 2fc331e7
tests: civ_own_species, civ_own_species_bake_off, line_stages
signature:
docs: docs/re/creations.md#creations-model-bake, docs/06-testing.md#testing-civ-look
related: BUG-1273, BUG-1270
---

## Report
Key frames of civ_own_species (all its checks passed): lab 20261119-114816 frames 02 / 03 (guest_citizen1/2, the same
camera on both games): the host draws the guest's city citizens as the guest's creature with its paint (purple hat,
white dress, green trim); the guest's own game draws the same citizens plain white / grey (no paint). In lab
20261119-095414 frame 02 it was the other way round (the host's citizens white). The species key is the same on both
games (7B635F66, the guest's Civ outfit's shared copy); only the paint is missing.
To reproduce: `SPORE_DETECTOR=20 python3 tools/lab.py run civ_own_species --lane N`, then
`tools/frames_review.py <run> --test civ_own_species`.

## Root cause
A species line creation (0x7B...... key from the content, species_line.cpp) is a key no game has baked yet, and nothing
in the citizen path (City_Species -> CityCitizens_Sync -> SetSpeciesProfile) asks the game to bake it: a citizen made
of a species whose model is not baked is drawn plain white until the game's own (lazy) bake ends. Measured with the
game's own state (`Editors::IBakeManager` vt 0x1C IsBaked, docs/re/creations.md#creations-model-bake): in the twin run
both games had **not** baked the guest's Civ outfit 7BE27A2C when the citizens took it, the game's own bake ended
30.5 s after the line was known (lab 20261119-141341 civ_own_species_bake_off). Which game shows white depends on its
graphics cache: a lane's cache keeps a bake once made (the earlier twin passed on games that had baked 7B635F66 in the
run before, lab 20261119-140235), so the night runs saw it on the guest mostly. The Creature / Tribe line keys were
baked by the creature's own use before. The old test checked +0xB28 keys only, not the drawn paint.

## Fix
`civ.own_species_bake` (civ_species.cpp): as soon as a player's Civ line is known on a game, the game's own bake is
asked for (`game::model_baked(key, true)`: IsBaked, else IsQueued, else RequestBake {0x609B763, 0x40000} as
NpcTribeController_FormTribe does); a city answers the line's species only once its model is baked (the
civilization's species until then; the 0.5 s check then resyncs the city with the baked model). Measured: the bake
takes 2-4 s after the line is known. `coop.civ citizens` prints `baked=` per line and per (city, species) of the
citizen objects, read live. `coop.fix civ.own_species_bake off`: no bake asked, the species answered at once (as before).

## Verification
civ_own_species (cold now: the guest's Civ outfit is a fresh copy of one of its creatures with its metadata times moved,
a key never baked on any lane) asserts the game's bake state of the citizens' species on both games right after the
capture (`check_paint`) and keeps its key frames (02 / 03: one citizen up close, painted); line_stages does the same.
- FAIL (twin `civ_own_species_bake_off`, SPORE_FIX_OFF=civ.own_species_bake): lab 20261119-141341
  (`{'host': {'7BE27A2C': 0}, 'guest': {'7BE27A2C': 0}}`).
- PASS: lab 20261119-141341 civ_own_species (baked 4.1 s / 2.1 s after the line was known; frames 02 / 03 painted); on the merged tree: twin FAIL and PASS lab 20261119-143657-2, PASS again lab
  20261119-151431 (a42100f2).

## History
- 2026-11-19: opened (fix/w57-savestrip, seen in the key frames while fixing BUG-1273's save check); not fixed there.
- 2026-11-19: fix/w57-civlook: cause measured (the line key not baked), civ.own_species_bake, bake check and fresh outfit in the tests; fixed.
- 2026-11-23: released in 0.7.16.
