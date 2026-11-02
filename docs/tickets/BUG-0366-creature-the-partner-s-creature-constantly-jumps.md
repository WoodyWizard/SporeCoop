---
id: BUG-0366
title: Creature: the partner's creature constantly jumps on the host's screen (guest at 30 fps, 0.7.3.3)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-01
source: player report 2026-11-01, host F9 mark s 1743
role: host
versions: 0.7.3.3
fixed_in: 0.7.3.4
branch: fix/w30-puppet-jump
commits: dbadb92, 4e74918
tests: puppet_slope (tools/tests/coop_puppet_walk.py --ground)
signature: the puppet plays the fall animation 0x0692E6A9 (+0x137 = 0) walking downhill
docs: docs/re/creature.md#creature-puppet-ground, docs/06-testing.md#testing-puppet-ground
related: BUG-0360, BUG-0367, BUG-0376
switch: creature.puppet_ground
---

## Report
Two players on 0.7.3.3 (Windows; the host GOG 2024 at 60 fps, the guest Steam at ~30 fps on a weak
PC; rtt 70-150 ms with spikes to 400 ms+, one minute with 1.1 s), Cell > Creature. The host marked
with F9 (s 1743): "my friend is constantly jumping" (the guest's creature on the host's screen). The
10 s trace lines around the mark show the guest standing 4 m from the host (flags 0x1, same spot in
the guest's own trace for 30 s) and running before (flags 0x7); nothing else about the puppet.

## Root cause
Observed (lab, `coop.track` with heights over the ground, docs/re/creature.md#creature-puppet-ground):
0.7.3.3 (BUG-0360, `creature.puppet_walk`) places the walking partner's puppet exactly on the
partner's path every frame. Walking DOWNHILL the puppet is 2-3 cm over the ground after the game's
next step; the game takes it as falling (+0x137 = 0) and plays the fall animation 0x0692E6A9 for
1-5 frames, again and again: 4-7 times in 40 s on a slope of the fixture with the guest at 30 fps
over a home-like connection, 4 at 60 fps on a clean one, the walker's own creature 0 times (0.7.3.2's
driving: 0-1). That is a creature that keeps hopping.
Not the cause (measured): the 30 fps sender and the network (latency spikes, 30 % loss bursts) keep
the 0.7.3.3 puppet on its path (93-98 % matched windows, no vertical bounce); the ground is the same
on both games.
Inferred: the players' "constantly" while the guest stood (10 s samples only) may cover the walks
between the samples; real jumps of the guest's creature could not be tried (the fixture's
creatures cannot jump; BUG-0376 for what a hop does).

## Fix
`creature.puppet_ground` (modes.cpp creature_drive, on_own_ground): a target from 3 m under to 0.5 m
over this game's ground (cPlanetModel::GetHeightAt) is put 5 cm into the ground, walking and
standing; the game sets the grounded puppet onto its terrain (drawn -0.03 to 0.00 m) and it stays
grounded. A partner higher up (a jump, a rock) is placed as before. `coop.fix creature.puppet_ground
off`: 0.7.3.3.

## Verification
`coop_puppet_walk.py --dir guest --guest-fps 30 --goto=-195,-465,-45 --secs 40 --ground` with
`SPORE_NET=player` (suite puppet_slope): the puppet starts the fall animation at most once more than
the walker's own creature, never under the ground.
* FAIL with `SPORE_FIX_OFF=creature.puppet_ground`: out/lab/20261101-082223-fix-w30-puppet-jump (6 falls,
  the walker's own creature 0), out/lab/20261101-084515-fix-w30-puppet-jump (2 of 2: 4 and 5 falls).
* PASS: out/lab/20261101-082323-fix-w30-puppet-jump (2 of 2: 0 falls, lowest drawn height 0.00 m),
  out/lab/20261101-084728-fix-w30-puppet-jump (2 of 2 checks; #2 failed only on the known cold-start
  caught fault at 0x11ECE4F on both games before the test began, docs/06-testing.md).
* Regression (lane 6): out/lab/20261101-082801-fix-w30-puppet-jump: sync, creature, creature_desync,
  puppet_walk, team ok; puppet_walk matched windows 89-96 % with the fix, 86-93 % without it
  (out/lab/20261101-084118-fix-w30-puppet-jump), so the placing into the ground costs nothing there.
* Exploration (out/explore, not lab runs): 0.7.3.3 4-7 falls per 40 s on the slope, 0 with the fix in
  4 runs; 0.7.3.2's driving 1.

## History
- 2026-11-01: opened.
- 2026-11-01: reproduced and fixed on fix/w30-puppet-jump (creature.puppet_ground); BUG-0376 opened.
- 2026-11-02: released in 0.7.3.4.
