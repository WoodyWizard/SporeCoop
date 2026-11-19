---
id: BUG-1277
title: WP-X2: every player's species line through Creature -> Tribe -> Civ (one test)
status: open
kind: test
stage: All
reported: 2026-11-18
source: plan-0.7.16
role: 
versions: 
fixed_in:
branch: feat/w54-x2
commits: a65aa814 b7d8fe0f 0db36ea8 79ee9769 08bce529
tests: line_stages, line_stages_outfit, line_stages_off
signature:
docs: docs/06-testing.md#testing-line-stages
related: BUG-1270, BUG-1271, BUG-1272, BUG-1273, BUG-1274, BUG-1275, BUG-1276
---

## Report
Plan 0.7.16 item 4, WP-X2: one test from the creature fixture through Creature -> Tribe -> Civ with the guest's own
creature, on the real stage-end routes the campaign uses, both games: in Creature its own creature, in Tribe its
members are its line (optionally a tribe outfit from its own outfitter), in Civ its city's citizens and its vehicle are
its line; each step checked through the game's own fields on both games, key frames with same views per stage, a host
save parsed per stage, FAIL twin with `SPORE_FIX_OFF=species.line`.

## Root cause
No bug: a test package. The pieces (WP-0, WP-T1, WP-T2, WP-C1, WP-C2) each had a test on its own stage's fixture; none
went through the live stage changes, where the game carries only the host's line (docs/plan-0.7.16.md item 4).

## Fix
`tools/tests/coop_line_stages.py` (docs/06-testing.md#testing-line-stages), built on the campaign's `Campaign`
(advance, follow, phase_frame, segment_save, kick_civ) and the packages' helpers (coop_tribe_own_species,
coop_tribe_own_outfitter.outfitter_route (moved out of its run(), no change), coop_civ_own_species,
coop_civ_own_designs, coop_species_line). Suite entries `line_stages`, `line_stages_outfit` (extended) and the twin
`line_stages_off`. `--civ-editors` switches the Civ part to the guest's own City Hall / vehicle editors at the arrival
(WP-C3 / WP-X1; without it WP-C2's setter is the stand-in); `--raids` ends Tribe by the campaign's raids.
coop_campaign.py: the editor driver uses the City Hall palette in the Civ follow phase too (a guest's own editors at
its arrival). No game code changed.

## Verification
Not run yet: the owner's rule of 2026-11-18 (no lab runs until the night run). Night commands (lanes 2 and 3):
`SPORE_DETECTOR=20 python3 tools/lab.py run line_stages line_stages_outfit --lane 2 --lane 3 --retry 0 --no-build`,
then `SPORE_DETECTOR=20 python3 tools/lab.py run line_stages_off --lane 2 --retry 0 --no-build` (must FAIL at the
Tribe members). Once WP-X1 is in: `SPORE_DETECTOR=20 python3 tools/lab.py run line_stages --lane 3 --retry 0
--no-build` with the entry's command plus `--civ-editors` (a `line_stages_editors` entry with WP-X1's merge).

* fix/w57-savestrip (lane 4): the night run's own failures of the test fixed: the save checks (BUG-1315: Tribe and Civ
  saves clean in lab 20261119-114816, 20261119-123654 all three variants, 20261119-133307), the NPC cities after the
  founding shortcut (the host was held in a cinematic: the test ends it, then the cities came in 6 s, lab
  20261119-123654 line_stages_editors). FAIL twin `line_stages_off`: lab 20261119-120449 (Creature line, Tribe members).
* Still FAIL, outside this branch's area: line_stages lab 20261119-133307 only "the guest's vehicle is of the guest's
  design" (+0xC4 0 on both games, WP-C2 / BUG-1274); line_stages_editors lab 20261119-123654 the guest's own editors
  gave no designs (WP-X1 / WP-C3, BUG-1276 / BUG-1275); line_stages_outfit lab 20261119-123654 the guest stays in its
  own Tribal Outfitter (WP-T2, BUG-1272), then stuck in civ/follow; lab 20261119-120449 line_stages a host fault in
  TribeStage_Graduate (BUG-1325, once).

* fix/w57-civlook (lanes 4 and 5): line_stages PASSES end to end: lab 20261119-142443-2 and on the tree merged with
  rel/0.7.16 (741c292b) lab 20261119-143657 (the guest's vehicle of its design on both games, BUG-1274; its city's
  citizens painted, the game's bake state checked, BUG-1321; the host's Civ save clean). Twin of the vehicle step:
  line_stages_nodesign_off FAIL lab 20261119-143657. Again after the next merge (a42100f2): PASS lab 20261119-151431-2.
  line_stages_outfit on the same tree (lab 20261119-144822): still FAIL outside this branch's area: the guest stays in
  its own Tribal Outfitter ("the guest is back in Tribe" fails, WP-T2 / BUG-1272), then never reaches the host's Civ
  world (stuck civ/follow), so every guest-side Civ check is empty; the host-side Civ checks and the Civ save pass.

## History
- 2026-11-18: opened.
- 2026-11-18: test, suite entries and docs on feat/w54-x2; stays open until the night run gives the PASS and FAIL run
  dirs (as BUG-1271..1274).
- 2026-11-19: fix/w57-savestrip: save checks pass (BUG-1315), the Civ cities' wait and the cinematic end; open for the WP-C2 / WP-X1 / WP-T2 steps (other packages).
- 2026-11-19: fix/w57-civlook: the Civ vehicle (BUG-1274) and citizen paint (BUG-1321) steps fixed, line_stages passes; line_stages_outfit / line_stages_editors wait for the guest's own editors (fix/w57-own-editors).
