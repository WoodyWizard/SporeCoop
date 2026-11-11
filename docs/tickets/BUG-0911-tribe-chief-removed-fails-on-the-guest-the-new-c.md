---
id: BUG-0911
title: tribe_chief_removed fails on the guest: the new chief is not 1.3x its own size (1.58, 1.20)
status: open
kind: test
stage: Tribe
reported: 2026-11-10
source: fix/w47-tribe-looks, lab 20261110-210722 and 20261110-211149
role: guest
versions: 0.7.12 (unreleased)
fixed_in:
branch:
commits:
tests: tribe_chief_removed
signature:
docs:
related: BUG-0081, BUG-0475
---

## Report
`tribe_chief_removed` (coop_tribe_chief_removed.py, lanes 6 and 7, cold, rel/0.7.12 + fix/w47-tribe-looks) fails its
check "guest: the new chief is 1.3x its size in the game": 1.58 in lab 20261110-210722, 1.20 in lab 20261110-211149
(that run with the branch's two new switches off, `SPORE_FIX_OFF=tribe.held_item,tribe.pointer_tag`: the failure does
not come from them). The host's value is 1.30 in both. Before the test's own steps the guest's `coop.tribe` already
printed `chief_scale= 1:1.17 0:1.30` on the unchanged rel/0.7.12 build (lane 6, by hand). Every other check of the
test passes. Reproduce: `tools/lab.py run tribe_chief_removed --lane N --retry 0`.

## Root cause
Not analysed. Inferred from the code: the guest's chiefs_tick notes the mirror's own desired scale (its game's
GetDesiredScale, a random size per game) and sets 1.3x of it, then the members' scale field (tribe.scale, BUG-0475)
puts the host's member's size on the mirror: the ratio the test reads (the mirror's scale over the guest's own note)
is the host's chief size over the guest's base, not 1.3. If so the size on the screen is the host's (right) and the
test's guest-side ratio is the wrong thing to check; not verified.

## Fix
Open (not part of fix/w47-tribe-looks).

## Verification
Open: the guest's check should compare the mirror's size with the host's member's size.

## History
- 2026-11-10: opened.
