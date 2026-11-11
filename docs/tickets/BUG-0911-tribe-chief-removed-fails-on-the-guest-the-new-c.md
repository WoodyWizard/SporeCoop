---
id: BUG-0911
title: tribe_chief_removed fails on the guest: the new chief is not 1.3x its own size (1.58, 1.20)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-10
source: fix/w47-tribe-looks, lab 20261110-210722 and 20261110-211149
role: guest
versions: 0.7.12 (unreleased)
fixed_in: unreleased
branch: fix/w47-tribe-looks-2
commits: da7bf994
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
The test's guest-side ratio, not the size on the screen.
* Observed (lab 20261110-221742): on the guest the new chief and the other player's chief have exactly the host's
  sizes (2.443 / 2.443, 2.844 / 2.844, `coop.tribe scale <id>` on both games) while the guest's `chief_scale=` ratio
  is off.
* From the code: `chief_scale` is the member's size over the size chiefs_tick noted for it, and that note is the
  game's GetDesiredScale (vt 0x80), a new random pick of each game for a member that does not lead. The host sets
  1.3x of its pick, so its ratio is 1.30 by construction; the guest's mirror gets the host's size from the members'
  scale field (BUG-0475) while its note is its own game's pick: host size / guest pick = anything (1.58, 1.20).
* With `SPORE_FIX_OFF=tribe.scale` the guest's ratio reads 1.30 again (lab 20261110-221942): the check was written
  before the scale field and has measured the guest's own bookkeeping since.

## Fix
coop_tribe_chief_removed.py: the host keeps the 1.3x check; for the guest the test compares the size of the new chief
and of the other player's chief with the host's game (3 %). No change of the mod.

## Verification
* FAIL (the old check): lab 20261110-210722 (1.58), 20261110-211149 (1.20).
* PASS: lab 20261110-221742, 20261110-222213 (twice in that run).

## History
- 2026-11-10: opened.
- 2026-11-10: fixed on fix/w47-tribe-looks-2 (the test).
