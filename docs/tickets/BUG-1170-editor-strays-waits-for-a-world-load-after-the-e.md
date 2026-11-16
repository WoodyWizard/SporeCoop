---
id: BUG-1170
title: editor_strays waits for a world load after the evolution that the in-place evolution (BUG-0508) no longer makes when the guest plays its own creature
status: fixed
kind: test
stage: Creature
reported: 2026-11-15
source: lab 20261115-164240 (feat/w51-core2)
role: 
versions: 
fixed_in: unreleased
branch: fix/w51-evolve
commits: 54da09ad
tests: editor_strays
signature:
docs: docs/06-testing.md#testing-evolve-own-creature, docs/re/creations.md#creations-swap-cinematic
related: BUG-0508, BUG-0212, BUG-0689, BUG-1141, BUG-1176
---

## Report
editor_strays FAILED in lab 20261115-164240 (feat/w51-core2, lane 10), both rounds: "the evolution cinematic is over,
the guest loaded the host's world after it" (wait_settled: a new host share and a guest load). No fault, no stray, the
herd checks after it passed.

## Root cause
Observed (host and guest game logs of the run): the guest plays its own creature (`body: own avatar species 065CBE70 ->
290225CD ... from this world's record (worlds.ini)`, the own-creature choice of BUG-0689 / 0.7.13); after the host's
evolution the host decides "evolved in place: Bob keeps its world" (BUG-0508, session_glue.cpp evolve_inplace_ok) and
its 30 s fallback ("still plays <host species>: sending the world") does not fire, because the guest plays its own
species, not the host's. So no world is shared and nothing is loaded: the behaviour BUG-0508 wants. The last PASS runs
of the test (20261108-071725, fix/w46-g4-tests) shared the world only through that fallback (the guest still played the
host's species then). Inferred: the test's precondition (a load after every evolution) is older than BUG-0508 and the
own creature; not caused by C2: the same FAIL on rel/0.7.15 9362bcdd without feat/w51-core2 (baseline worktree, lab
20261115-180220) and again on the branch (20261115-174822).

## Fix
A stale test, not a mod bug: the same FAIL on v0.7.14.1 (a worktree of the tag, lane 9, 20261115-191956: the host's
log says "evolved in place: Bob keeps its world" after both evolutions, the guest plays its own 'Kirgle'), so neither
0.7.15 nor C2 changed it. The behaviour the test now checks is the one BUG-0508 (0.7.4,
docs/re/creations.md#creations-evolve-in-place) documents: after an evolution the host sends its world only when it
needs to; a guest of this version in the same world and stage is kept in place, and gets the world after all only
when its avatar still plays the host's pre-editor species 30 s later. A guest that plays its own creature in this
world (worlds.ini, BUG-0345; the lane's record, BUG-1176) is always kept in place.
* `coop_editor_strays.py` `wait_settled`: settled when the cinematic is over, no destroy is pending, no world is being
  sent or received and the guest is in Creature (all as before), and either the host shared and the guest loaded
  (as before) or the host kept the guest in place (`kept` grew), sent nothing after all and its 30 s check is over.
  A fallback world (`sent after all` grew) still needs the share and the load. The strays and herd-home checks after
  it are unchanged.
* `coop.status` (host): `evolve in place: kept N, sent after all M, saves S, checking C` (C: guests whose 30 s
  species check still runs; tests only, no behaviour change).
No switch: the mod is unchanged (one status field added).

## Verification
* FAIL (the old test): rel/0.7.15 9362bcdd out/lab/20261115-180220 (feat/w51-core2-base, lane 10, both rounds); v0.7.14.1
  fix/w51-evolve-v07141/out/lab/20261115-191956 (lane 9, both rounds, "evolved in place: Bob keeps its world").
* PASS (the new test, fix/w51-evolve): out/lab/20261115-192824 (lane 9, both rounds "in place"); final runs
  out/lab/20261115-195303 and 20261115-195524 (lane 9, both rounds "in place" each).
* Not run: the "shared" branch of the new wait on a lane whose guest plays the world's species (every lane here has
  the worlds.ini record, BUG-1176); that branch is the old condition unchanged.

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on fix/w51-evolve (the test follows BUG-0508; FAIL on rel/0.7.15 and v0.7.14.1, PASS runs above).
