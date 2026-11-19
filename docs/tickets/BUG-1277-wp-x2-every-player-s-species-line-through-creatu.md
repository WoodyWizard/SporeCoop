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
commits: a65aa814 b7d8fe0f
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

## History
- 2026-11-18: opened.
- 2026-11-18: test, suite entries and docs on feat/w54-x2; stays open until the night run gives the PASS and FAIL run
  dirs (as BUG-1271..1274).
