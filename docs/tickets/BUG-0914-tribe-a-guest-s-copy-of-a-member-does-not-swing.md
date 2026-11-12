---
id: BUG-0914
title: Tribe: a guest's copy of a member does not swing its staff or instrument when the host's member does (it stands with the item in the resting grip)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-10
source: tribe_held_item key frames, lab 20261110-214352 (sheet 009)
role: guest
versions: 0.7.12 (unreleased)
fixed_in: unreleased
branch: fix/w48-tribe-mirrors
commits: 6fc65d20, b059baac, b781e846
tests: tribe_mirror_hold
signature:
docs: docs/re/tribe.md#tribe-mirror-looks, docs/06-testing.md#testing-tribe-mirror-looks
related: BUG-0906, BUG-0569
---

## Report
Seen in tribe_held_item's key frame after the rejoin (lab 20261110-214352, sheet
`009_tribe_held_item_02_chief_rejoin.jpg`, the same camera on both games): the host's chief swings its staff forward
(one of its idle animations with the item, `hold=1` in `coop.tribe` on the host), the guest's copy stands still with
the staff upright. The same in tribe_name_tags' frame of that run. Musicians: not looked at.

## Root cause
Proven (lab). The staff (or instrument) is in the hand only while the timed modifier of kind 5 lives, which the attach
(Citizen_SetHandheldItem 0xC250A0 -> 0xC240E0) binds to the animation playing then (#tribe-held-item); the host's
behaviour nodes take the item inside their animations (the chief's gestures 0x04C6CB02 / 05: `hold=1` for 0.5-2 s,
at times for half a minute). The guest's mirror plays the same animation through the relay (BUG-0569) but nothing
takes the item there (no behaviour tree), so the staff stays in the resting grip (upright). Measured with the host's
chief taking the staff in its animation: the guest's copy held it in 0 of 3 rounds with the fix off; key frame
`hold` of lab 20261112-100511 shows the host's staff forward in the hand, the guest's upright.

## Fix
`tribe.mirror_hold`: the host sends each start of a member's hold with the animation it plays (MSG_NPC_ANIM kind 11,
unreliable; once a second every hold again) and each end (anim 0, reliable); the guest attaches the item again on its
mirror while it plays that animation (also right after the relay starts it, and four times a second while a hold is
pending), so the hold is bound to the mirror's animation and ends with it or with the host's end; BUG-0906's 1.5 s
end leaves such a hold alone for up to 8 s.

## Verification
Test `tools/tests/coop_tribe_mirror_idle.py --part hold` (suite `tribe_mirror_hold`): three rounds of the host's chief
holding its staff in its animation (`coop.tribe hand`, the game's attach, or a gesture holding it already) and putting
it back: the guest's copy holds it 1.5 s after each take and has it back in the grip 1.5 s after each end; key frames
`swing` and `hold` of kind sameview.
* FAIL with `SPORE_FIX_OFF=tribe.mirror_hold`: out/lab/20261112-100511 (0 of 3; key frame: the staff upright on the guest).
* PASS: out/lab/20261112-100852 (3 of 3; key frame: the staff in the hand on both), HOLDPASS.

## History
- 2026-11-10: opened.
- 2026-11-12: fixed (fix/w48-tribe-mirrors): the members' holds relayed. The natural-hold rate alone is no measure (the mirror's game takes the item itself in some animations).
