---
id: BUG-1213
title: space_npc_model / creature_ufo_nomodel INCONCLUSIVE: no ship without its model came (one visit, five visits)
status: fixed
kind: test
stage: Space
reported: 2026-11-16
source: final check 20261116-025154
role: 
versions: 0.7.14.1, unreleased
fixed_in: 0.7.15
branch: fix/w51-t3
commits: 3a2ed87f, c550f886
tests: space_npc_model, creature_ufo_nomodel
signature:
docs: docs/re/space.md#space-gfx-cache-models, docs/06-testing.md#testing-space-autosave
related: BUG-0546, BUG-0686, BUG-1138
---

## Report
0.7.15 final check (lab 20261116-025154): space_npc_model INCONCLUSIVE "no NPC ship without its model in this run"
(lane 3) and creature_ufo_nomodel INCONCLUSIVE "every ship of 5 visits had a model" (lane 4); the same on rel/0.7.15
on lanes 6 and 7 (lab 20261116-082811).

## Root cause
* Not a 0.7.15 change: v0.7.14.1 on lane 6 (lab 20261116-085912): space_npc_model SKIP the same way, creature_ufo_nomodel
  needed 4 visits (a ship gfx without a model at visit 4). Earlier passing runs had it at visit 1 or 2 (lanes 3, 5).
  Which ship a visit (or the NPC empire's fleet) brings is picked from the host's Sporepedia cache: only some picks
  are a UFO with a summary and no model, so one arrival (space_npc_model) or five visits are often not enough. The
  history: space_npc_model had the case only on lane 5 (4 of 6 runs), never on lanes 2, 3, 4, 6.
* ✔ Why lanes differ (manual run on lane 6, 2026-11-16 09:3x, `coop_space_npc_model.py --solo` with the lane's
  GraphicsCache.package and GraphicsCache.001.package moved aside): the first arrival had 2 ships whose model the guard
  had to replace (`ship models defaulted 2`); with the lane's own GraphicsCache 4 arrivals had none (lab
  20261116-090823). The baked models of those ships in a lane's GraphicsCache make their keys known to the resource
  manager (creations::key_known), so the guard never sees a ship without its model there. Lane 5's had none of them.
* `game.ufo_model` was retired in 0.7.15 (B1, BUG-1138) with its fix kept on; the test code and the guard are the same
  as in 0.7.14.1 (gamefixes.cpp ufo_set_model_guard / ufo_forget unchanged), so the retirement is not the cause.

## Fix
Tests only, nothing they check changed: both tests start the host with an empty GraphicsCache (the lane's files moved
aside and put back after the run, as the Sporepedia cache already was: `gfx_aside` / `gfx_back` in
coop_space_npc_model.py). As a backstop space_npc_model flies to the NPC empire's star up to 4 times (back to the home
star in between) until a ship needed the guard (suite minutes 4 -> 7); creature_ufo_nomodel brings up to 8 UFO visits
instead of 5 (minutes 6 -> 14). A run where the case still does not happen stays INCONCLUSIVE (SKIP).

## Verification
no-fail-run: test change only. The case is random per arrival / visit: with the changes it happened in
creature_ufo_nomodel out/lab/20261116-090823 (visit 1), -092001 #2 (visit 4), -100807 #2 (visit 1), INCONCLUSIVE in
-092001 #1 and -100807 #1 (8 visits each); space_npc_model: manual run on lane 6 with the GraphicsCache aside (2 ships
defaulted at the first arrival) and out/lab/20261116-092001 #2 (defaulted 2 at visit 1); INCONCLUSIVE in -090823,
-092001 #1, -100807 #1 and #2 (4 arrivals each). v0.7.14.1 behaves the same (out/lab/20261116-085912: npc SKIP,
creature at visit 4): not a 0.7.15 regression. Residual: INCONCLUSIVE in about half the runs on lanes 6 / 7; the case
itself is the game's pick of a ship, which no test seam controls yet.

## History
- 2026-11-16: opened.
- 2026-11-16: fixed on fix/w51-t3 (tests: empty GraphicsCache, more visits); INCONCLUSIVE stays possible.
- 2026-11-18: second final check 20261117-154911 INCONCLUSIVE again (lane 4), rerun 20261117-213354 INCONCLUSIVE; with --repeat 4 (out/lab/20261117-232700) PASS in #4 (lane 3, the guard replaced a ship model), INCONCLUSIVE in #3: the case stays random, the guard works.
- 2026-11-18: released in 0.7.15.
- 2026-11-20: 0.7.16 final check 20261120-041304 (fix/w60-misc's look): space_npc_model INCONCLUSIVE (lane 4, empty GraphicsCache, 4 arrivals at 004A0014: `ship models defaulted 0` each), creature_ufo_nomodel INCONCLUSIVE (lane 4, 8 visits, every ship with a model; guard counters 0, 0); both games fault-free. The same residual as above (the game's pick of a ship), nothing of 0.7.16 in that path; the guard itself is unchanged. A test seam that forces a ship key this game does not have would make it deterministic (not done: needs a lab run to prove it reaches the game's own null-model path).
