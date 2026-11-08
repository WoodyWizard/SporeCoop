---
id: BUG-0683
title: Creature: a partner's puppet 0.65 m under the ground at the nest while the brain upgrade caption plays after the evolution (monitor rule ground)
status: fixed
kind: test
stage: Creature
reported: 2026-11-07
source: lab 20261107-141346 campaign_tribe (rel/0.7.10 e7d1734c, lane 5)
role: guest
versions: 0.7.5+
fixed_in: unreleased
branch: fix/w46-k7-findings
commits: dbe7a1cf, 839499be
tests: puppet_unground, own_ground
signature: MONITOR-VIOLATION ground <role>: partner N's puppet 0.5-0.7 m under the ground (Creature) on 2 lines, right after the evolution editor (busy=cinematic,input_lock)
docs: docs/re/creature.md#creature-puppet-unground
related: BUG-0504, BUG-0592, BUG-0435
---

## Report
campaign_tribe on rel/0.7.10 (lab 20261107-141346, lane 5) printed RESULT: OK; the lab failed it on the monitor gate
`ground extra2: partner 1's puppet 0.65 m under the ground (Creature) on 2 lines from s 240`. On extra2 (Dave, 4
players) after the evolution editor all four creatures stood at the nest spot (-171.5 -466.9 -83.6, d=0), the
brain upgrade caption played (`busy=cinematic,input_lock`); Bob's puppet read agl 0.24 at s 230, -0.65 at s 240 and
s 250 (drawn_v 0.0), -0.03 at s 260 after the caption (campaign_tribe.extra2.game.log lines 473-494).

## Root cause
* Observed: the same pattern on rel/0.7.5 campaign_tribe (lab rel/0.7.5 20261104-061516: host, partner 1's puppet
  0.55 m under, from s 210, right after the evolution editor at the nest, agl -0.67 / -0.55) and partner puppets
  under the ground on rel/0.7.9 too (creature_carry 20261106-200316 and -231404, 0.74 m / 1.15 m, other spots), so not
  a 0.7.10 regression.
* Inferred (as BUG-0504 for the own creatures): the creatures stacked on one point of the nest mound under the caption
  (paused game time, the mod's frame still runs) are pushed apart and one is pressed under the mound's surface; the
  puppet stands still, so creature.puppet_mound / puppet_ground do not move it until the caption ends. BUG-0504's fix
  (creature.own_ground) covers own creatures only.

## Fix
`creature.puppet_unground` (modes.cpp puppet_unground, called from creature_drive every frame): a partner's puppet
whose partner stands (under 0.3 m/s, not jumping, not dead) and that has been more than 0.25 m under this game's terrain
(GetHeightAt) for 0.5 s is put onto it where it stands (game::place, no teleport mark), at its target's height when the
target is within 2 m and higher (a nest mound). The same rule as creature.own_ground (BUG-0592) for the own creature.
Test hooks: `coop.puppet agl` (each Creature puppet's height over this game's terrain) and `coop.puppet sink <depth>
[side]` (in commit 839499be). `coop.fix creature.puppet_unground off`: 0.7.10.

## Verification
Lab, lane 9, `puppet_unground` (coop_puppet_unground.py: creature fixture, both standing; on the host the guest's
puppet is put 0.7 m to the side and 0.69 m under its terrain, as the nest's separation left Bob's puppet, twice; 4 s of
samples of the puppet's own height over the host's terrain):
- FAIL (SPORE_FIX_OFF=creature.puppet_unground): lab 20261108-005021 and 20261108-005440: round 1 the puppet stays
  0.6-0.69 m in the ground for all 4 s (10 / 9 pairs under 0.25 m), the partner's own creature on its ground (0.00)
  (round 2 the game's own update put it back in the first run: not every press is kept, as in the reports).
- PASS: lab 20261108-005145, 2 of 2: on the ground 0.3-0.6 s after each sink (`ungrounded=2`).
- The caption itself (all four creatures on one nest point after the evolution) is not forced by the test; campaign_tribe
  on the final build: see History.

## History
- 2026-11-07: opened.
- 2026-11-08 (fix/w46-k7-findings): fixed (creature.puppet_unground); puppet_unground FAIL x2 / PASS x2.
