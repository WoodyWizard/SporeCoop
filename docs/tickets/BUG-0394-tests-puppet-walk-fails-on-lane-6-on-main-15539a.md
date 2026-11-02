---
id: BUG-0394
title: tests: puppet_walk fails on lane 6 on main 15539ad (3 of 3 runs, each on another check)
status: fixed
kind: test
stage: Creature
reported: 2026-11-01
source: fix/w31-creature-gate regression run 2026-11-01
role: both
versions: 0.7.3.3
fixed_in: 0.7.3.4
branch: fix/w30-npc-walk
commits: 75e3371, 59db48a, b3c3acd, 6be0e20, e20e9a3, fa06a11
tests: puppet_walk_novel, npc_walk_novel, puppet_walk_fps30, puppet_walk_player, npc_walk_fps30, npc_walk_player, tribe_walk_fps30, tribe_walk_player
signature:
docs: docs/re/creature.md#creature-walk-30fps, docs/06-testing.md#testing-walk-30fps
switch: creature.puppet_path, npc.path
related: BUG-0360, BUG-0361, BUG-0387, BUG-0365, BUG-0366
---

## Report
puppet_walk on lane 6 (GOG 2024), 2026-11-01:
* fix/w31-creature-gate d89e8c5: lab 20261101-100420 (host watches guest: walk gait 0 % of 552 frames, the
  puppet's animation 02481DE5 the whole walk) and 20261101-103812 (2 runs: "guest watches host: no jump
  of the puppet while it walks (2)"; "stands in 5 % <= 5 %, at its speed 88 %").
* main 15539ad itself (a detached worktree, the same lane): lab 20261101-104255, 3 of 3 FAIL, each on
  another check (the walker walked only 94 / 0 frames; stands 27 % / 9 %; walk gait 77 %).
`lab.py coverage`: 21 of 37 recorded runs passed. The branch changes nothing in the puppet driving.
Reproduce: `lab.py run puppet_walk --lane 6 --repeat 3`. Not triaged (not part of the branch's task):
the walker itself not walking (0 / 94 frames) points at the test's input (WASD to a background
window), the rest at the puppet driving.

## Root cause
Observed (fix/w30-npc-walk, lanes 2-5, `SPORE_FPS=30`, `SPORE_NET=player`; coop.track / coop.npctrack, every frame):
1. A partner (and a host NPC) can move without a speed of its own: the walker's creature carried by an
   animation (B4212A07 after a click: 3-4.6 m/s for a second with the game's velocity 0 and speed state 0).
   Its states said "standing", so the puppet was driven as standing (drive::step): it stood while its path
   moved, fell 2.9 m behind and was put in place (18 % standing windows, the run failed). The same for an NPC
   proxy (2.2 m/s at speed 0, pulls undone by its locomotion, a 2.7 m teleport).
2. Right after the join the guest loaded for seconds (18 fps on average, single frames of 5.4-5.6 s); its
   game did not update the partner's puppet for 7 s (moved only where placed, game speed 0, walk gait 0):
   the walk gait failures (59-79 %). Test timing: measured before the games ran steadily.
3. At 21-29 fps a 0.1 s window holds 1-3 frames: the speed ratio swung with the frame times while the
   drawn error stayed 0.01-0.6 m (79-81 % matched; over 4 frames 89-98 %). Measurement, not steps (inferred).
4. The walker did not walk (0 / 11 / 94 frames): after a first walk ended 15-25 m apart its clicks on the
   partner's tag did not bring it back. Test input.

## Fix
* `creature.puppet_path` (modes.cpp): the puppet walks with its path's speed (`drive::path_velocity`, the
  shared path tracker in driver.h) when the partner's state reports none. `npc.path` (npc.cpp): the same
  for NPC proxies (Creature and Tribe). Off: the old driving as standing.
* driver.h: ONE `drive::walk_place` / `walk_end` for the puppet, Tribe mirrors and NPC proxies (merged
  with fix/w30-tribe-walk's version; NPC options walk_speed, snap only when the path itself jumped,
  keep_lost) and `path_update` / `path_velocity` for all of them.
* Tests: puppet_walk / npc_walk wait until both games run steadily; puppet_walk puts the walker next to
  the observer before its walk and walks back with W when a click does not; speed windows of at least 4
  frames (unchanged at 60 fps); puppet_walk's frame counts scale with the observer's frame rate; levers
  `coop.track novel 1` / `coop.npctrack novel 1` (the partner's / NPCs' states without their speed);
  suites `puppet_walk_novel`, `npc_walk_novel` and the `_fps30` / `_player` variants of puppet_walk,
  npc_walk, tribe_walk.

## Verification
* creature.puppet_path: FAIL with `SPORE_FIX_OFF=creature.puppet_path`: out/lab/20261101-122732-fix-w30-npc-walk
  (puppet_walk_novel 2 of 2 FAIL: standing 39-63 %, 4-9 jumps per direction). PASS:
  out/lab/20261101-123326-fix-w30-npc-walk (puppet_walk_novel 2 of 2), 20261101-122740 #1 (#2 only the known
  startup fault at 0x11ECE4F).
* npc.path: FAIL with `SPORE_FIX_OFF=npc.path`: out/lab/20261101-124932-fix-w30-npc-walk (npc_walk_novel 2 of 2:
  standing 63-67 %, 30-40 jumps). PASS: out/lab/20261101-124940-fix-w30-npc-walk (2 of 2: standing 0 %, matched
  97-98 %, no jumps, gait 98-99 %).
* Variants at 30 fps / player network: out/lab/20261101-123326-fix-w30-npc-walk 12 of 14 (puppet_walk_player#1:
  the walker could not walk back, fixed in the test after; npc_walk_fps30#1: a 2.7 m teleport of a proxy whose
  NPC moved without a speed, fixed by npc.path after).
* Final code, gates on (lanes 2-5): out/lab/20261101-125335-fix-w30-npc-walk 12 of 17 (ok: puppet_under, npc_walk,
  npc_walk_tribe, tribe_walk_mobile, creature_partner_hp, creature, team, sync, puppet_walk_fps30,
  puppet_walk_player, npc_walk_fps30, npc_walk_player; failed: tribe_walk on the sync-check gate "n: 3 apart"
  (BUG-0393, also in 11 main runs before), puppet_walk (the walker walked little, 26 windows), puppet_slope (guest
  null call and freeze, BUG-0398), tribe_walk_fps30 / _player (frame counts for 60 fps and SPORE_FPS capping the
  load: fixed in the test and the suite after)). Again on lanes 4-5: 20261101-130448 puppet_walk 2/2, puppet_slope
  2/2, tribe_walk 2/2, tribe_walk_fps30 2/2, tribe_walk_player 1/2 (frame count, fixed after);
  20261101-131942 tribe_walk_fps30 2/2, tribe_walk_player 2/2.
* creature fails on BUG-0390 (no hit applied) with this branch's switches on and off alike: on 2 of 3
  (20261101-132708), off (`SPORE_FIX_OFF=npc.walk,npc.path,npc.walk_tribe,creature.puppet_path`) 1 of 3
  (20261101-132700).
* Smoke (lanes 2-3): out/lab/20261101-130440-fix-w30-npc-walk 13 of 14 (creature: BUG-0390). make -j8 and
  make test pass.

## History
- 2026-11-01: opened.
- 2026-11-01: fix/w30-npc-walk: causes measured, creature.puppet_path, npc.path, test fixes.
- 2026-11-02: released in 0.7.3.4.
