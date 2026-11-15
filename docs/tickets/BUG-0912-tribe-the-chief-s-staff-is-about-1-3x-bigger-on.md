---
id: BUG-0912
title: Tribe: the chief's staff is about 1.3x bigger on the guest's screen than on the host's
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-10
source: tribe_held_item key frames, lab 20261110-214352
role: guest
versions: 0.7.12 (unreleased)
fixed_in: 0.7.12
branch: fix/w47-tribe-looks-2
commits: 0c9dc172
tests: tribe_held_item
signature:
docs: docs/re/tribe.md#tribe-item-size
related: BUG-0906, BUG-0911, BUG-0475
---

## Report
Seen in the key frames of tribe_held_item after BUG-0906 was fixed (sheet
`out/lab/20261110-214352/frames/sheets/008_tribe_held_item_01_chief_start.jpg`, the same camera 14 m from the host's
chief): both games draw the chief with its staff upright, but the guest's staff is longer and its skull bigger (the
skull is over the tree line on the guest, at the chief's crest on the host), about 1.2-1.3x. The chief itself has the
same size on both. Reproduce: `tools/lab.py run tribe_held_item --lane N`, `tools/frames_review.py <run>`.

## Root cause
* Observed (Ghidra GOG 2024, 0xC250A0 at 0xC250F9..0xC25114 and 0xC2552F..0xC25563): the attach computes
  `(bounding box [+0x14] - [+8] of spatial vt 0x68) * the float at 0x1581334` and writes it as the scale of the item
  model's transform (model vt 0x20 read, scale at +0x10, vt 0x18 write). Nothing sizes the model again. (Citizen vt
  0xE0, guessed earlier, is not the size: it returns an id passed to model vt 0x44.)
* Observed (lab 20261110-221944, the fix off): the host's staff model has scale 2.188 (the chief's own size when the
  game gave it the staff), the chief itself 2.844 (the mod's 1.3x for a player's chief, chiefs_tick, later); the
  guest's copy of the chief has 2.844 from the members' scale field before its staff is attached, so its staff got
  2.845: exactly 1.3x the host's. Other members' items were the same on both (1.72, 1.42, 1.49).

## Fix
The size of the model in a member's hand is a replicated field of the members (fields.h kind
`tribe_member_item_scale`, field `item_scale`): the host sends its model's scale, the guest writes it into its
mirror's item model the way the attach does. Switch `coop.fix tribe.item_scale off`: the mirror's own item size.
`coop.tribe` rows print `iscale=` (the held model's scale, 1 without one) and `scale=` (the member's).

## Verification
`tribe_held_item` now also compares the held models' scale of every member on both games (5 %) and the chief's staff
(3 %), before and after the rejoin, with its same-view key frames.
* FAIL with `SPORE_FIX_OFF=tribe.item_scale`: lab 20261110-221944 (guest 2.845, host 2.188).
* PASS: lab 20261110-221744, 20261110-222215.
* By eye: sheet of tribe_held_item of lab 20261110-222215.

## History
- 2026-11-10: opened.
- 2026-11-10: fixed on fix/w47-tribe-looks-2 (tribe.item_scale).
- 2026-11-11: released in 0.7.12.
