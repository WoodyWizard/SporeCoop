---
id: BUG-1092
title: Creature: puppet_air fails on rel/0.7.14: the host's puppet of the walking guest jumps 2-8 times (no jump check)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-13
source: lab
role: host
versions: rel/0.7.14
fixed_in: 0.7.14
branch: fix/w49-puppet
commits: 215adcfb, 7c5c6f20, 59a155c8
tests: puppet_air, net_tests test_interp
signature:
docs: docs/re/creature.md#creature-puppet-agl-lerp, docs/06-testing.md#testing-puppet-look, docs/known-issues.md#issue-puppet-jumps
related: BUG-0376, BUG-0425, BUG-0532, BUG-1093
---

## Report
fix/w49-raid2 regression (2026-11-13): `puppet_air` FAILs on every run with "host watches guest: no jump of the puppet
while it walks (N)", N = 1-8 (the puppet's position steps while the guest's creature walks); the other checks pass
(jumps shown, walk gait, falls). Also on the base rel/0.7.14 at 470f1ccd built alone (lab 20261113-185616: 7 and 4),
so not fix/w49-raid2's change; on fix/w49-raid2 with and without `game.air_culled` the same (20261113-185217: 2, 7;
20261113-185217-2 off: 3, 8; 20261113-184358: 1). Last FAIL of this kind before: fix/w41-puppet-look (2026-11-04).
Reproduce: `lab.py run puppet_air --repeat 2`.

## Root cause
- Observed (track json of lab 20261113-185217 #1, host sees guest): every counted "jump" is a 1.45-1.56 m step in a
  10-30 ms frame at a hop's start. In the first frame with PF_JUMPING the puppet's placed position is already the full
  1.5 m up, while its target (the interpolated position) is 0.36-0.51 m up and rises over the next 2-3 frames.
- Observed (code): `SnapshotBuffer::sample` between two states copies the newer state (`out = b.s`) and blends only
  pos / rot, so `agl` (the partner's height over its own terrain, sent with PF_JUMPING) is the newer state's 1.5 m.
  `creature_drive` places a PF_JUMPING puppet at `drive::at_sender_agl(pos, agl)` (this game's ground + agl on pos's
  radial line): the blended height was replaced by the full one.
- Observed (lab): not a 0.7.14 change. v0.7.13 built alone fails 3 of 3 (20261113-190621: 4, 7, 13 steps; worktree
  bisect/w49-puppet-0713), the clean rel/0.7.14 2 of 2 (20261113-190457: 1, 6). The same 1-1.5 m steps failed about
  1 run in 3 on 0.7.5-0.7.6 (fix/w41-puppet-look, fix/w42-creature-left); puppet_air was not run between 0.7.6 and
  this batch.
- Inferred: the test's hitch allowance (`TP_FACTOR * top * dt`) hides the step in 50-70 ms frames (20 fps); at the
  lab's 40-70 fps now it does not (the fix-off run 20261113-191329: #1 at 28 fps no step, #2 at 67 fps 9 steps).
  The test is right: the step is drawn.

## Fix
- `drive.agl_lerp` (mods/coop/net/interp.h, switch in modes.cpp): between two states the agl is blended with the
  position, so the puppet rises with its target. A state without agl counts at its own height over the newer state's
  ground (0 m on the ground; the hop's first state can already be up before PF_JUMPING: the first version took 0 m
  and dropped the puppet 0.87 m for a frame, lab 20261113-192634-2 #2). Off: 0.7.13.
- Test (tools/tests/coop_puppet_walk.py): with `--air` the walk gait check holds the puppet to the walker's own gait
  share less 10 points (at most 80 %). It failed with and without the fix when the walker hopped often: its own avatar
  had a gait in 73-86 % of its moving frames (none in the air), the puppet 70-79 % (lab 20261113-191644,
  20261113-191329 #1, 20261113-190457 #1).
- Unit test: tests/net_tests.cpp test_interp (a state on the ground, the next 1.5 m up: half-way the agl is 0.75 m;
  `SPORE_FIX_OFF=drive.agl_lerp` fails).

## Verification
- FAIL with `SPORE_LAB_FIXOFF="puppet_air=drive.agl_lerp"`: lab 20261113-191329 #2 (lane 3, 9 steps of 1.5 m at
  67 fps; #1 at 28 fps had none and failed only the walk gait check, see below). Without the fix also 20261113-190457
  (rel/0.7.14, 1 and 6) and 20261113-190621 (v0.7.13, 4, 7, 13).
- PASS (final code): lab 20261113-193243 (lane 2, 3 of 3) and 20261113-193241 (lane 3, 3 of 3), 0 steps in each;
  the gait bar decided one run (193241 #2: 76 % >= 73 %). First version: 20261113-191022 (lane 2, 3 of 3).
- Unit: `make test` passes; `SPORE_FIX_OFF=drive.agl_lerp net_tests` fails test_interp.
- Regression (final code, --quick): lab 20261113-195009 (lane 2: puppet_far_jump, puppet_jump, creature_air, npc_air,
  tribe_mirror_jump OK), 20261113-193854 (lane 3: tribe_member_air, puppet_walk, puppet_mound, puppet_unground OK);
  first version: 20261113-191413 (puppet_stops, puppet_jump, puppet_walk, npc_air, puppet_unground OK),
  20261113-192249 (puppet_mound, npc_air_live OK), 20261113-191644 (puppet_far_jump, tribe_member_air,
  tribe_mirror_jump, creature_air OK). puppet_stops is flaky with and without the switch (FAIL 193854, 195009 and
  with `drive.agl_lerp` off 20261113-200403 #1; PASS 195639 and off 195007): opened as BUG-1093.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-puppet (drive.agl_lerp); the cause predates 0.7.14 (v0.7.13 fails the same way).
- 2026-11-14: released in 0.7.14.
