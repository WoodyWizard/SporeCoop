---
id: BUG-0376
title: Creature: the partner's jumps (leaving the ground) are mostly not shown, its puppet stays on the ground
status: open
kind: desync
stage: Creature
reported: 2026-11-01
source: lab, fix/w30-puppet-jump (hops simulated with coop.abilities lift)
role: both
versions: 0.7.3.3
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md#creature-puppet-ground, docs/known-issues.md#issue-puppet-jumps
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

## Fix

## Verification
`coop_puppet_walk.py --dir guest --jumps 0.6` and the track json (walker vs puppet airborne
episodes); no pass/fail check yet. docs/known-issues.md#issue-puppet-jumps.

## History
- 2026-11-01: opened.
