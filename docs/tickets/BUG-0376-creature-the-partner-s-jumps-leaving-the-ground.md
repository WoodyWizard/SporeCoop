---
id: BUG-0376
title: Creature: the partner's jumps (leaving the ground) are mostly not shown, its puppet stays on the ground
status: fixed
kind: desync
stage: Creature
reported: 2026-11-01
source: lab, fix/w30-puppet-jump (hops simulated with coop.abilities lift)
role: both
versions: 0.7.3.3
fixed_in: 0.7.5
branch: fix/w41-puppet-look
commits: af6fe121, see the branch log
tests: puppet_air
signature:
docs: docs/re/creature.md#creature-puppet-look, docs/re/creature.md#creature-puppet-ground, docs/known-issues.md#issue-puppet-jumps
related: BUG-0366, BUG-0360
---

## Report
Found on fix/w30-puppet-jump while looking for BUG-0366: when the partner's creature leaves the
ground, its puppet mostly stays on the ground on the other machine.

## Root cause
Observed: hops simulated by `coop.abilities lift 1.5` on the walker (the fixture's creatures cannot
jump): of 24 hops the 0.7.3.3 puppet stayed on the ground in 21-22 (the game puts a grounded creature
back on its terrain in the same frame) and fell with the partner in 2-3; the 0.7.3.2 driving
teleported it up in 5. The partner sends no airborne state (PF_JUMPING is never set).
Observed (2026-11-04, fix/w41-puppet-look, lane 2): the game leaves a puppet placed in the air there when it is marked
not grounded (+0x137 = 0) after the placing, and lets it fall; without the mark it grounds it in the same update. Walked
in the air (walk_place) it was grounded again in 15-20 frames of a fall. docs/re/creature.md#creature-puppet-look.

## Fix
`creature.puppet_air` (modes.cpp): the partner's game sets PF_JUMPING while its creature is not grounded and more than
0.15 m over GetHeightAt; the observer then skips creature.puppet_ground, ends the walk and places the puppet at the target
with +0x137 = 0 every frame. Off: 0.7.4 (no flag sent, the flag ignored). Test lever `coop.track air <v>` (variants).

## Verification
puppet_air (`coop_puppet_walk.py --dir guest --jumps 0.9 --air --secs 30`): frame by frame, of the frames with the
walker's target over 0.4 m above the host's ground, the next frame draws the puppet at least half as high in >= 80 %.
- FAIL with SPORE_FIX_OFF=creature.puppet_air: out/lab/20261104-025342 (24 of 485 frames, 5 %; 1 of 15 hops shown).
- PASS with the fix: out/lab/20261104-030351 (cold, 2 runs: 632 of 641 and 646 of 654 frames, 99 %).
- Regression on the merged branch (rel/0.7.5 merged): out/lab/20261104-041216 FAILED `no jump of the puppet` (7 frame
  steps: after 0.3-0.5 s of a fall the game took the puppet 1-1.6 m down in one update, its own fall speed had grown
  while the placing held it up). The puppet's velocity is now the partner's every frame in the air (variant 4 of
  `coop.track air` keeps the old): out/lab/20261104-042712 (cold, 3 runs: 99 / 98 / 100 % of the frames up; run #1
  still FAILED `no jump` on 2 one-to-three-frame drops when the game started another animation: BUG-0532).
- Final code (f5301458), cold: FAIL with SPORE_FIX_OFF=creature.puppet_air out/lab/20261104-043624 (238 of 589 frames,
  40 %); PASS out/lab/20261104-043742 (723 of 735, 98 %, no frame step), with puppet_walk, puppet_jump, puppet_mound.
- Not checked: a creature that really jumps (the fixture's cannot; hops are `coop.abilities lift 1.5`).

## History
- 2026-11-01: opened.
- 2026-11-04: fixed on fix/w41-puppet-look (creature.puppet_air).
- 2026-11-04: released in 0.7.5.
