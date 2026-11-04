---
id: BUG-0532
title: Creature: the partner's puppet in the air drops to the ground for 1-3 frames when the game starts another animation on it (creature.puppet_air)
status: open
kind: bug
stage: Creature
reported: 2026-11-04
source: lab, fix/w41-puppet-look puppet_air
role: both
versions: unreleased (fix/w41-puppet-look)
fixed_in:
branch: fix/w41-puppet-look
commits:
tests: puppet_air
signature:
docs: docs/re/creature.md#creature-puppet-look
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
Not found: which call.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: open

## History
- 2026-11-04: opened.
