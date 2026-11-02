---
id: BUG-0407
title: tests: the walk smoothness tests are flaky on a loaded machine (puppet_walk matched 81-91 %, npc_walk 1 jump, puppet_slope falls 3, tribe_coop mirror error 3.7 m)
status: fixed
kind: test
stage: Creature
reported: 2026-11-01
source: lab
role: 
versions: 0.7.3.3
fixed_in: 0.7.3.4
branch: fix/w34-walk-stable
commits: cce264b, 5557071, abc2b80
tests: puppet_walk, puppet_slope, puppet_under, npc_walk, tribe_walk, tribe_coop, walk_selftest (make test)
signature:
docs: docs/06-testing.md#testing-walk-loaded
related: BUG-0394, BUG-0360, BUG-0361, BUG-0365, BUG-0408, BUG-0409, BUG-0436, BUG-0416, BUG-0402
---

## Report
The per-frame walk tests failed on main and on every branch while 5+ agents ran 8+ lanes (observers at 11-29 fps):
puppet_walk 81-91 % matched windows (limit 85 %), npc_walk "1 proxy jump", puppet_slope "falls 3", tribe_coop
"mirror error 3.7 m", tribe_walk stop-and-go 11 % (limit 10 %) and too few frames; e.g. labs 20261101-135017,
20261101-141648, 20261101-135924 (fix/w33-null-call), 20261101-100420, 20261101-111051. A flaky gate hides real
stepping as noise or blocks a release on noise.

## Root cause
Every failure of the current driving (e20e9a3 and later, recorded track JSONs, `coop.track` / `coop.npctrack`)
classified; details and numbers in docs/06-testing.md#testing-walk-loaded:
* measurement: windows judged on frames drawn one frame after their placing, around hitches (a window ending
  after a long frame swung 0.25-2.4x with drawn errors of 0.1-0.4 m); npc_walk's jump limit from the run's top
  speed (4 m/s) against an NPC fleeing at 9.2 m/s; tribe_walk's stop-and-go frames with jittering frame times (the
  host's own game, no driving, 5-10 % "uneven"); tribe_coop compared a walking mirror with the ends of a 0.3-1.2 s
  round of console calls (not verified: the old test printed no member data; 0 of 30 runs since);
* test input / harness: walkers that did not walk (keys not taken by a guest at 8-18 fps, clicks that gave no
  walk), an observer at 11 fps with 41 % hitched frames, npc_walk hitting the host's own family (posse_damage gate,
  10 of 10 runs on main), a timed-out console call whose late reply was read as the next call's (two threads on
  one console);
* real (tickets, still caught): BUG-0408 (falls at stops on a slope, rare), BUG-0409 (a hit NPC's proxy held off its
  path), BUG-0436 (the puppet slides in its standing animation), the Tribe `drawn` monitor gate on a jumped RTS
  pointer (BUG-0416's class), tribe `n apart` (BUG-0402).

## Fix
Tests only (no mod change): puppet_walk / npc_walk compare the drawn way one frame after the path's and leave
out windows with an observer frame over 0.1 s (counted, printed); puppet_walk leaves out windows with a jump of
the target, walks on when it measured too little, reports NOT MEASURED over 20 % hitched frames, prints stops
(BUG-0408); npc_walk's jump limit uses the NPC's own speed, walkers / hit NPCs outside the host's family;
tribe_walk leaves out stop-and-go frames with a frame time 1.5x off the one before and walks on when it measured
too little; tribe_coop judges a mirror against the host's path over the round. The watch threads share the
console under a lock; sporectl drops a connection whose call timed out. walk_selftest.py in make test. No
threshold was lowered.

## Verification
A/B on lanes 2-3 (load 14-41 on 12 cores, not idle):
* main's tests, fix on: out/lab/20261101-143049 (44/60: puppet_walk 7/10, npc_walk 0/10, tribe_walk 8/10,
  puppet_under 9/10, puppet_slope and tribe_coop 10/10).
* FAIL with `SPORE_FIX_OFF=creature.puppet_walk,creature.puppet_path,npc.walk,npc.path,tribe.mirror_walk`:
  out/lab/20261101-154246 (puppet_walk, npc_walk, tribe_walk, puppet_slope, puppet_under 50 of 50 FAIL; tribe_coop
  10/10 passes, it has no twin among these switches).
* PASS, fix on: out/lab/20261101-154000 (57/60) and out/lab/20261101-173719 (24/25): puppet_walk 15/15, npc_walk
  15/15, tribe_walk 13/15, puppet_slope 14/15, puppet_under 14/15, tribe_coop 10/10; the 4 failures are BUG-0436 (2)
  and the Tribe `drawn` monitor gate (2), none a walk measure.

## History
- 2026-11-01: opened.
- 2026-11-01: fixed on fix/w34-walk-stable (tests).
- 2026-11-02: released in 0.7.3.4.
