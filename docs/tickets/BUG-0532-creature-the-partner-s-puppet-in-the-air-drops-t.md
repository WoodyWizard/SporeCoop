---
id: BUG-0532
title: Creature: the partner's puppet in the air drops to the ground for 1-3 frames when the game starts another animation on it (creature.puppet_air)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-04
source: lab, fix/w41-puppet-look puppet_air
role: both
versions: unreleased (fix/w41-puppet-look)
fixed_in: 0.7.6
branch: fix/w42-creature-left
commits: 985a9bd6 dbcb172b
tests: puppet_air
signature:
docs: docs/re/creature.md#creature-puppet-look, docs/re/creature.md#creature-puppet-air-anim
switch: creature.puppet_air_anim
related: BUG-0376
---

## Report
puppet_air on fix/w41-puppet-look (BUG-0376's fix on): the drawn puppet of a hopping partner (coop.abilities lift 1.5)
is drawn on its terrain for 1-3 frames in the middle of a fall and back in the air in the next frame, 0-3 times per
30 s run; the test's `no jump of the puppet while it walks` fails on those 1.4-1.6 m frame steps (lab 20261104-042712
#1: 2 steps; #2, #3: none).

## Root cause
Observed: every drop comes with a change of the puppet's main animation (fall 0692E6A9 -> idle 02481DE5, or the walk
start 04866D8E) in the same frame; +0x137 is 0 before it (set by the mod each frame). Before the puppet's velocity was
set to the partner's every frame the drops were longer (5-20 frames after 0.3-0.5 s of a fall: lab 20261104-041216,
7 steps). Inferred: starting an animation (Creature_IdleRefresh / the behaviour tree) grounds the creature in that update.
Observed (fix/w42-creature-left, lane 6, counters of the fix): the held puppet's +0x137 is 1 right after
Anim::AnimatedCreature::PlayAnimation 0xA0C5D0 (1-4 times a run), the idle refresh (2), CreaturePlayAnim (1-2), once
after cCreatureAnimal::Update: an animation start marks it grounded and the update puts it onto its terrain.

## Fix
`creature.puppet_air_anim` (game.cpp air_hold, modes.cpp creature_drive): while a puppet is held in the air the mod
keeps the partner's place; after those calls on it, when the game marked it grounded, it is put back there, marked not
grounded and given the partner's velocity. The hooks without 0xA0C5D0 left 1 step in 3 runs (lab 20261104-115008).
Off: `coop.fix creature.puppet_air_anim off` (0.7.5).

## Verification
puppet_air (`no jump of the puppet while it walks`; the drops happen in about 1 run of 3, so series of runs):
- FAIL with SPORE_FIX_OFF=creature.puppet_air_anim: out/lab/20261104-115336 (#3: 9 steps), out/lab/20261104-120645
  (#2: 2 steps, #4: 3 steps); 3 of 7 runs failed.
- PASS: out/lab/20261104-115803 (3/3), out/lab/20261104-120319 (3/3): 0 steps in 6 runs.

## History
- 2026-11-04: opened.
- 2026-11-04: fixed on fix/w42-creature-left (creature.puppet_air_anim).
- 2026-11-04: released in 0.7.6.
