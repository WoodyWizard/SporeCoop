---
id: BUG-1358
title: Test latency_step_ingame: the walker's random click on its own nest opens the evolution editor, the second walk has no partner
status: open
kind: test
stage: Creature
reported: 2026-11-20
source: final-check-20261120-041304
role: 
versions: 0.7.15.5, unreleased
fixed_in:
branch: fix/w84-tests
commits: ff36cb71 79281a0a
tests: latency_step_ingame, puppet_walk, puppet_walk_novel, puppet_stops
signature:
docs:
related: BUG-0333, BUG-0394
---

## Report
Final check of 0.7.16 (lab 20261120-041304, lane 3): latency_step_ingame FAIL in its second half (host watches
guest): "the walk was measured in 0 s of the 32 s at 300 ms"; the guest's walk log is "back 505m (no closer)" then
"back 505m (not found)" for the whole walk. The first half (guest watches host) passed. History: 2 of 4 failed on
rel/main, 5 of 11 on all branches since 2026-11-01.

## Root cause
Observed (latency_step_ingame.host.game.log): at s 77.86, at the end of the host's walk, "nest: an egg hatched or was
eaten" and "editor request in Creature: editor 312E9D6A ... (an evolution in the stage)": the host's game opened its
evolution editor (core.editor_now: the host's editor at once) and stayed in it; the guest's trace shows "partner 0
'Alice' Editor ... puppet=hidden" and from s 150 "Alice is not responding". The host's walk (coop_puppet_walk.walk)
clicks random ground points 45-75 % down the screen; the walker was at its own nest ("nest d=0 m" at s 70), so a click
landed on the nest (the game's nest click is the editor) or an egg. With the host in its editor the guest's `coop.status`
remote line had no state (pos=(0 0 0)): "505 m" is the planet's radius, the walk's "back to the partner" never found
it. A test fault, not the mod.
* 2026-11-23 (fix/w84-tests, lab 20261123-023643 latency_step_ingame.host.game.log): the host's own trace has
  `input_lock=1 busy=cinematic,input_lock` from s 60 (at s 50 none), its avatar back at the spawn point on the nest
  (nest d=0 m at s 70), then at s 82.6 the evolution editor. Both players spawn at the host's nest, so the observer
  (the guest) stands on it; the walker's way back to the partner (coop_puppet_walk.back_to_partner) clicks 60 px under
  the partner's tag, which is on the nest: that click (not guarded by ff36cb71, which guarded only the random clicks)
  sent the walker onto its nest, its game played the nest cinematic and opened the editor. Inferred from the timing;
  the walk log has "back 22m (no closer); back 18m; ... back 13m (not found)" in that stretch.

## Fix
coop_puppet_walk.py walk(): before a random click it asks `coop.targets objs` for the screen points of nests (cNest
0x52AA6122, 260 px) and eggs (cEgg 0x02A034CD, 90 px) and makes a 1.5 s W hold instead of a click that close to one
(logged as "not the click ... the nest or an egg under it"). Used by every test on walk() (latency_step, puppet_walk
and its variants).
* fix/w84-tests (79281a0a): back_to_partner asks near_nest() for its click point too and makes a 2 s W hold toward
  the partner instead of a click on the nest (logged as a normal "back"). coop_latency_step.py: when a player's game is
  in an editor before a direction or after its walk (`game.mode`), that direction is not judged and the run ends
  `RESULT: INCONCLUSIVE a direction was not judged (... BUG-1358 ...)` (a failed check of the other direction still
  FAILs). Test only, no mod change.

## Verification
no-fail-run: test-only change (the walker's input). Lab tonight: latency_step_ingame PASS (both directions measured);
`SPORE_FIX_OFF=net.offset_window` latency_step_ingame FAIL (its twin, unchanged). Open, waiting for the lab run.
- Still open (2026-11-23). latency_step_ingame PASS in 20261120-133703, 20261121-003202, 20261122-194210 and 20261123-075458 (2 of 2), but FAIL in 20261123-023643 and 20261123-071312 for this ticket's cause: the host's walk no longer clicks its nest ("not the click ...: the nest or an egg under it"), yet at the end of its walk near the nest its game still opened the evolution editor ("editor request in Creature: editor 312E9D6A ... (an evolution in the stage)" with "nest: an egg hatched or was eaten", s 82.6 and s 93.4), so the guest's walk had no partner ("back 505m (not found)"). Next: find what opens it (the egg hatch, a walk over the nest) and keep the walker off the nest. Its words were removed from the CHANGELOG's test-fix line.
- Pending night run (fix/w84-tests): `SPORE_DETECTOR=20 python3 tools/lab.py run latency_step_ingame --repeat 3 --lane N --retry 0 --no-build` PASS (or INCONCLUSIVE naming BUG-1358;
  never "back 505m (not found)"); `SPORE_DETECTOR=20 python3 tools/lab.py run puppet_walk puppet_walk_novel puppet_stops --lane M --retry 0 --no-build` PASS
  (the same walk); twin `SPORE_LAB_FIXOFF="latency_step_ingame=net.offset_window" SPORE_DETECTOR=20 python3 tools/lab.py run latency_step_ingame --lane M
  --retry 0 --no-build` FAILS. FAIL before: 20261123-023643, 20261123-071312.

## History
- 2026-11-20: opened from the 0.7.16 final check; fixed in coop_puppet_walk.py on fix/w60-misc.
- 2026-11-23: lab evidence of the 0.7.16 final check added; stays open.
- 2026-11-23: fix/w84-tests (WP-T1 of 0.7.17): the way back to the partner does not click the nest; an editor makes the run INCONCLUSIVE (79281a0a).
