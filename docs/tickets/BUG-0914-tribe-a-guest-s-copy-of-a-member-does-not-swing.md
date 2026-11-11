---
id: BUG-0914
title: Tribe: a guest's copy of a member does not swing its staff or instrument when the host's member does (it stands with the item in the resting grip)
status: open
kind: bug
stage: Tribe
reported: 2026-11-10
source: tribe_held_item key frames, lab 20261110-214352 (sheet 009)
role: guest
versions: 0.7.12 (unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0906, BUG-0569
---

## Report
Seen in tribe_held_item's key frame after the rejoin (lab 20261110-214352, sheet
`009_tribe_held_item_02_chief_rejoin.jpg`, the same camera on both games): the host's chief swings its staff forward
(one of its idle animations with the item, `hold=1` in `coop.tribe` on the host), the guest's copy stands still with
the staff upright. The same in tribe_name_tags' frame of that run. Musicians: not looked at.

## Root cause
Not analysed. Known: the hand's item and its resting grip are replicated (BUG-0906), animations come through the
behaviour animation relay (tribe.mirror_anims, BUG-0569); whether that relay carries these idle animations, or they
are played by a path it does not hook, is not checked. docs/re/tribe.md#tribe-held-item.

## Fix
Open (cosmetic; left for after 0.7.12).

## Verification
Open: a same-view frame while the host's member has `hold=1`, the animation id of both.

## History
- 2026-11-10: opened.
