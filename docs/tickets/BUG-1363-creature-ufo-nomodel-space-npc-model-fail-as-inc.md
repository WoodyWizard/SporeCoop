---
id: BUG-1363
title: creature_ufo_nomodel / space_npc_model fail as INCONCLUSIVE 3 in a row: the game's random ship pick never brings a ship without its model
status: fixed
kind: test
stage: Creature, Space
reported: 2026-11-20
source: final check 20261120-133703
role: 
versions: unreleased
fixed_in: 0.7.16
branch: fix/w61-creature-d
commits: c67ac9af
tests: creature_ufo_nomodel, space_npc_model, space_npc_model_coop
signature:
docs: docs/06-testing.md#testing-fixcheck-nan-claims
related: BUG-1213, BUG-0546, BUG-0686
---

## Report
0.7.16 second final check (lab 20261120-133703 on rel/0.7.16 673f7a55): creature_ufo_nomodel (lane 2, 8 visits,
"every ship of 8 visits had a model") and space_npc_model (lane 4, `--solo`, 4 arrivals, "ship models defaulted 0"
each) print `RESULT: SKIP` and both games are fault-free, yet both are FAIL in the summary and in coverage. Not from a
twin or fix-off step (out/night-0.7.16-b/2-*.txt do not run them).

## Root cause
Observed (results.json of the run): `inconclusive: true`, then `FAIL lab: INCONCLUSIVE 3 runs in a row (...): the
precondition never happens; ticket candidate` from lab.py streak_fails (A15, docs/06-testing.md#testing-lab-verdicts):
the lab is right, the test's precondition is the game's random pick of a ship. BUG-1213 (0.7.15) made it likelier
(empty GraphicsCache, more visits) and recorded the residual: INCONCLUSIVE in about half the runs on some lanes. In
the first 0.7.16 final check (20261120-041304) and in this one it did not happen at all (the history: 0 of 20 creature
runs failed, INCONCLUSIVE 7; space_npc_model INCONCLUSIVE 13). Nothing of 0.7.16 is in that path (gamefixes.cpp
ufo_set_model_guard / ufo_gfx_create_guard unchanged since 0.7.15); the guard counters were 0 because no ship needed
them. The 20261119-214823 PASS of creature_ufo_nomodel shows the case when it comes: `ship model 29133E22/476A98C7/
40656200 is not in this game: none known, kept` then `a ship gfx was made without a model ... no fault`.

## Fix
A test seam, as BUG-1213 proposed: `coop.ufoplant [n]` (gamefixes.cpp, debug.cpp) makes the next n ship models the
game asks for (the BUG-0546 SetModel hook) use the key 0BAD0546/476A98C7/40656200, which no game has, before the guard
decides; the guard and the game's own SetModel then do exactly what they do for a cache ship whose model is missing
(the game's model world makes no model; the BUG-0686 factory path or the BUG-0546 SetVisible path follows). The tests
keep the game's own picks first (creature_ufo_nomodel 3 visits, space_npc_model 2 arrivals) and plant only when those
brought no such ship (creature: up to 5 visits instead of 8; space: 3 arrivals instead of 4), printing which way the
case came ("natural" / "planted"); SKIP stays for a run where even that did not happen (it should not). Nothing the
tests check changed; no mod behaviour outside the test command changed (`coop.status` gains `ship model keys planted N
(armed M)`).

## Verification
no-fail-run: test seam only; open, waiting for the lab run (no game today, owner rule). Tonight:
`SPORE_DETECTOR=20 python3 tools/lab.py run creature_ufo_nomodel space_npc_model space_npc_model_coop --lane N --retry 0 --no-build`
must PASS (not SKIP), each log with "a ship gfx without a model (natural|planted ...)" / "ship models defaulted N";
`SPORE_FIX_OFF=game.ufo_gfx python3 tools/lab.py run creature_ufo_nomodel --lane N --retry 0 --no-build` must FAIL
(the host's fault at 0xE99524 / 0xE9A908 on the planted visit at the latest): that FAIL is the proof that the planted
key reaches the game's own null-model path.

## History
- 2026-11-20: opened.
- 2026-11-23: released in 0.7.16.
