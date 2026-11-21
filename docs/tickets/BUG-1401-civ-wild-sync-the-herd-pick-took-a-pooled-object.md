---
id: BUG-1401
title: civ_wild_sync: the herd pick took a pooled object at an unplaced point, the kill happened out of the cameras' relevance (test bug)
status: fixed
kind: test
stage: Civ
reported: 2026-11-21
source: lab 20261121-003202
role: test
versions: 
fixed_in: unreleased
branch: fix/w62-flake-tcs
commits: 69313126
tests: civ_wild_sync
signature:
docs: docs/re/civ.md#civ-wild-reused-frozen
related: BUG-0973, BUG-1100
---

## Report
civ_wild_sync FAILED in the third final check of 0.7.16 (lab 20261121-003202, lane 2): "FAIL the guest's attack order
killed the same creature in the host's game" (the host's creature, serial 207, "alive after 61 s") and "FAIL the killed
creature is dead on the guest's screen too (a body 1000000000.0 m from it)". The history calls it a known flake (1 of 6
on rel/main); the earlier FAIL runs (20261113-040648, 20261113-111705, fix/w49) were the wild sync itself (worst
500 m), fixed then: a different cause.

## Root cause
* Observed (the log): `host: shifted 1 creatures` (every passing run of all worktrees: 7) and the first key frame
  camera at (0, 525, 0): the herd pick (the live host creature with the most others within 30 m) took an object at
  the pole. docs/re/civ.md#civ-wild-reused-frozen: the game's pool gives objects out at unplaced points
  ((0 524.8 0), z = 0) while its relevance pass rebuilds herds. The census lists them as live creatures.
* Observed: the guest's target was then a creature 449 m from the vehicles (passing runs: 40-211 m) and about 180 m
  from the cameras' point (-21 525 -31). The host's log: `civ: order 1 of vehicle 6: noun 018EB45E at (-123 482 111)
  is the host's at 0.0 m` (s 53.1), then `npc 140 (species 066742CD) is updated at the planet's centre: herd
  00000000, 507 m from where it was a frame before, dead=1` (s 75.8, radius of the target 509 m): the vehicles killed
  it, and in the same frame the game took the body into its pool (out of the Civ relevance sphere, leave 200 m,
  #civ-wild-sync) and gave the object out again alive: the test's 1 s census saw serial 207 alive at (0 0 0) for the
  rest of the 60 s, and `last` became (0 0 0), so the guest's body was looked for at the planet's centre.
* Inferred: the host's game shows no body either (its body went to the pool), so the guest's screen (no creature
  there, dead 0) agreed with the host's: not a mod bug. The test's pick made the case.
* Old: the pick is as old as the test (BUG-0973, 0.7.14); which objects stand live where after the load is the game's
  relevance pass. Not a w61 regression (the only w61 commit in the area, 94b6740d, adds npctrack columns).

## Fix
Test only (tools/tests/coop_civ_wild_sync.py):
* `placed(p)`: an object at the planet's centre or with a coordinate of exactly 0 (the pool's unplaced points) is no
  candidate for the herd or the target.
* The herd: the host's simulated (`own=npc`) placed creatures seen in two censuses 1 s apart (same serial within
  10 m), the one with the most others within 30 m, and at least 3 of them; waited for up to 30 s (a state, not a
  sleep). No herd: a FAIL that says so.
* The target: the guest's placed creature nearest to the moved herd (not the one nearest to the vehicles), and both
  cameras framed on it before the order, so the kill happens inside the relevance sphere and the body stays. The kill
  and body checks are unchanged.
No mod change, no switch.

## Verification
FAIL (old test): 20261121-003202:civ_wild_sync (`shifted 1 creatures`, camera (0 525 0), target 449 m from the
vehicles). Passing runs of the old test had `shifted 7 creatures` and the target 40-211 m from the vehicles
(20261120-041304, 20261120-133703 and 13 more).
no-fail-run: the pool's timing cannot be forced (BUG-1100: a natural reuse could not be forced); open, waiting for the
lab run (no game today, owner rule). Tonight: `SPORE_DETECTOR=20 python3 tools/lab.py run civ_wild_sync --lane N
--retry 0 --no-build` must PASS with `shifted N creatures` N >= 3 and the target framed (a second key frame camera
line right before the attack orders); `SPORE_FIX_OFF=npc.civ_wild python3 tools/lab.py run civ_wild_sync --lane N
--retry 0 --no-build` must still FAIL (the test's documented twin).

## History
- 2026-11-21: opened.
- 2026-11-21 (fix/w62-flake-tcs): cause found (the herd pick and the target out of relevance), test fixed.
