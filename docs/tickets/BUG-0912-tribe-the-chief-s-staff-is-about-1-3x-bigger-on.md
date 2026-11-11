---
id: BUG-0912
title: Tribe: the chief's staff is about 1.3x bigger on the guest's screen than on the host's
status: open
kind: bug
stage: Tribe
reported: 2026-11-10
source: tribe_held_item key frames, lab 20261110-214352
role: guest
versions: 0.7.12 (unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0906, BUG-0911, BUG-0475
---

## Report
Seen in the key frames of tribe_held_item after BUG-0906 was fixed (sheet
`out/lab/20261110-214352/frames/sheets/008_tribe_held_item_01_chief_start.jpg`, the same camera 14 m from the host's
chief): both games draw the chief with its staff upright, but the guest's staff is longer and its skull bigger (the
skull is over the tree line on the guest, at the chief's crest on the host), about 1.2-1.3x. The chief itself has the
same size on both. Reproduce: `tools/lab.py run tribe_held_item --lane N`, `tools/frames_review.py <run>`.

## Root cause
Not analysed. Inferred: the game sizes the item's model at the attach (0xC250A0: model vt 0x44(5, citizen vt 0xE0(),
3) for the racks' items and the staff). The host's staff was attached while its chief had its own size (2.19), then
the mod made the player's chief 1.3x (2.84, chiefs_tick); the guest's copy gets the staff when it already has the
host's size 2.84 (the members' scale field). Not verified (vt 0xE0 not read; whether the host's staff grows when the
game attaches it again later).

## Fix
Open (found at the end of fix/w47-tribe-looks).

## Verification
Open: a check needs the item model's scale on both games (the attachment record's model, docs/re/tribe.md#tribe-held-item).

## History
- 2026-11-10: opened.
