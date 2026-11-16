---
id: BUG-1213
title: space_npc_model / creature_ufo_nomodel INCONCLUSIVE: no ship without its model came (one visit, five visits)
status: open
kind: test
stage: Space
reported: 2026-11-16
source: final check 20261116-025154
role: 
versions: 0.7.14.1, unreleased
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
