---
id: BUG-1277
title: WP-X2: every player's species line through Creature -> Tribe -> Civ (one test)
status: fixed
kind: test
stage: All
reported: 2026-11-18
source: plan-0.7.16
role: 
versions: 
fixed_in: unreleased
branch: feat/w54-x2, fix/w58-linestages
commits: a65aa814 b7d8fe0f 0db36ea8 79ee9769 08bce529 869bc4fa ae084a94 13d4c653 0489143d 8c2b99ed 08ad0bc2
tests: line_stages, line_stages_outfit, line_stages_editors, line_stages_off, line_stages_nodesign_off
signature:
docs: docs/06-testing.md#testing-line-stages
related: BUG-1270, BUG-1271, BUG-1272, BUG-1273, BUG-1274, BUG-1275, BUG-1276, BUG-1328
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

fix/w58-linestages (2026-11-19, the last three failures on the merged tree; test and driver only, no game code):
* line_stages_editors ("no such city" on the guest, no capital citizens there, another city kind at the siege; lab
  20261119-151550 / 20261119-153243 of fix/w57-own-editors): a test problem, not a city sync regression. The guest's
  game log: its own City Hall editor ended, 6 s later its vehicle editor opened (81 s after the arrival); the test's
  wait ("any design of the guest's on both games") was true in those 6 s, stopped the campaign driver and the guest
  stayed in its vehicle editor for the rest of the test ("the host founded a city while this game is outside the Civ
  mode" x9): no city on its game, its buy "no such city". Now the driver runs until `coop.civarrival` says
  phase=done, the guest is back in Civ and its line holds the City Hall and the vehicle editor's design id on both
  games; then the host's cities are on the guest's game at once (capital 19 citizens on both). The city kind at the
  siege differs per seed (the nearest NPC city); the test buys the city of the editor's land design id as before.
* line_stages_editors then failed on the guest's vehicle "built from 3A5843B7, the culture's 3A5843B7" (lab
  20261119-164538): the campaign driver made the host's end-of-Tribe vehicle and the guest's own with the same three
  drags, the same model byte for byte (the key frame shows the same green car). With `--civ-editors` the guest's
  arrival editors get a fourth part (`Campaign.arrival_parts`): its vehicle D003B93A / 9AE3BD18 vs the culture's
  3A5843B7. The check is unchanged.
* The arrival editors come on every path of the merged tree (core.own_civ_arrival in own mode), so the test drives
  them also without `--civ-editors` (lab 20261119-164538 line_stages_outfit: they opened in the play phase without a
  driver and the guest stayed in its City Hall editor).
* line_stages_outfit "built from none" (lab 20261119-162318): not seen again; on the merged tree the guest's vehicle
  of the `owndesign set` path is re-keyed on both games (host culture key 0, guest 29271FD6 / generated; 7BE222FB built
  from 76023C9D): lab 20261119-165858 line_stages, 20261119-182732 line_stages_outfit. The test now prints the whole
  model key (`ownveh ... mtype mgroup`) when the check fails. After the guest's own Tribal Outfitter its own City Hall
  editor at the arrival could not be named (no dice in the name panel, 4 of 4 runs): BUG-1328 (open); the driver leaves
  such an editor with the red cross after 14 "done" (`Campaign.arrival_cancel_after`; not with `--civ-editors`).

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

* fix/w58-linestages, all on rel/0.7.16 merged (lanes 5 and 6): **line_stages PASS** lab 20261119-165858 (and
  20261119-1xxxxx-final, see below); **line_stages_editors PASS** lab 20261119-171852 and 20261119-180816 (the guest's
  City Hall 7B... and vehicle of its own design on both games, the host's Civ save clean); **line_stages_outfit PASS**
  lab 20261119-182732 (the outfit's citizens, the vehicle 7BE222FB built from 76023C9D on both games; the arrival
  editors left with the red cross, BUG-1328). Twins: **line_stages_off FAIL** lab 20261119-182732 (Creature line 0 on
  both games, the Tribe members the slot's); **line_stages_nodesign_off FAIL** lab 20261119-173854 (the guest's
  vehicle +0xC4 00000000, max hp 10, on both games). FAIL before: lab 20261119-151550 / 20261119-153243 (editors),
  20261119-164538 (editors: the same model; outfit: no driver), 20261119-165858 / 20261119-171852 /
  20261119-173854 / 20261119-180816 (outfit: the unnamed City Hall, BUG-1328).
* BUG-1274 holds in all three: the guest's vehicle shows the guest's design on both games (line_stages and _outfit
  through `owndesign set` and the zero-key path, _editors through the guest's own vehicle editor).

## History
- 2026-11-18: opened.
- 2026-11-18: test, suite entries and docs on feat/w54-x2; stays open until the night run gives the PASS and FAIL run
  dirs (as BUG-1271..1274).
- 2026-11-19: fix/w57-savestrip: save checks pass (BUG-1315), the Civ cities' wait and the cinematic end; open for the WP-C2 / WP-X1 / WP-T2 steps (other packages).
- 2026-11-19: fix/w57-civlook: the Civ vehicle (BUG-1274) and citizen paint (BUG-1321) steps fixed, line_stages passes; line_stages_outfit / line_stages_editors wait for the guest's own editors (fix/w57-own-editors).
- 2026-11-19: fix/w58-linestages: the arrival editors' wait, the guest's own vehicle model, the editors on every path, BUG-1328's red cross; all three PASS, both twins FAIL: fixed.
