---
id: BUG-0906
title: Tribe: the guest's copy of the host's chief has no chief's staff (the host's game draws it)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-10
source: same_view_tribe key frames, lab 20261110-201619
role: guest
versions: 0.7.11.2 and earlier
fixed_in: unreleased
branch: fix/w47-tribe-looks
commits: a5b39cc1
tests: tribe_held_item, tribe_tools, same_view_tribe
signature:
docs: docs/re/tribe.md#tribe-held-item, docs/06-testing.md#testing-tribe-looks
related: BUG-0514, BUG-0583, BUG-0652
---

## Report
Seen in the review of the key frames of lab run 20261110-201619 (same_view_tribe, the same fixed camera on both games): the host's game draws its chief ('Alice's chief') holding the chief's staff (a stick with a white skull over its head); the guest's game draws the same member at the same place without the staff. Same in view 1 and view 2. Sheets: `out/lab/20261110-201619/frames/sheets/011_same_view_tribe_01_view1.jpg`, `out/lab/20261110-201619/frames/sheets/012_same_view_tribe_02_view2.jpg`; frames in out/lab/20261110-201619/frames/same_view_tribe/. Reproduce: `tools/lab.py run same_view_tribe --lane N` and open the sheets (`tools/frames_review.py <run>`).

## Root cause
* Observed (runtime lane 6, mem.read; Ghidra GOG 2024): what the game draws in a member's hand is its handheld item
  (cCreatureCitizen +0xFFC mCurrentHandheldItem, 0x11 = the chief's staff) with a model attached to the hand, set by
  0xC250A0, which only the game's behaviours (the grab, idle and gather nodes) and SetChieftain reach.
  SetSpecializedTool writes only the tool type +0x102C. The host's chief: tool 11, item 0x11, model 1. The guest's
  mirror of it (the guest's own save's member, bound as the mirror): tool 11, leader 1, **item 0, no model**. The
  same for members with instruments: the host's #6 tool 7 item 0x10, the guest's mirror tool 7 item 0.
* Observed: an NPC tribe's chief had its staff on the guest: tribe.leader_sync had called SetChieftain on that
  mirror, which ends in 0xC26470 (the hand shows the tool). The player tribe's leader was already the leader in the
  guest's game, so nothing called it.
* Cause: a guest's mirrors run no behaviours (their AI ticks are skipped), so nothing puts the item into their
  hands; the tool field of BUG-0514 copied the tool type only.
* Observed (runtime lane 6, shots; Ghidra): the item alone was not the look. The attach adds a timed modifier of kind
  5 to the creature that holds the item "as it is being taken" while the animation playing at the attach lasts, and
  its end puts the item into the resting grip. A mirror's animation is its idle loop, so the modifier never ended:
  with the item replicated the staff stuck out forward with the arm down (the first sheets, lab 20261110-211339).
  The same happened on the host when a test attached the staff to its standing chief, and went away when the
  modifier was ended by hand. An older note that SetSpecializedTool "refreshes the
  handheld" was wrong (vt 0xAC rebuilds the abilities): corrected in docs/re/tribe.md#tribe-held-item and the .sym.

## Fix
The handheld item is a replicated field of the tribe members (fields.h kind `tribe_member_item`, field `item`, the
item + 1 on the wire): the host sends every member's +0xFFC, the guest attaches it to its mirror with the game's own
0xC250A0(item, -1) (`SporeCoop::Citizen_SetHandheldItem`) whenever the mirror's differs, and ends a mirror's kind 5
modifier that has lasted 1.5 s with the game's own remove (`SporeCoop::CreatureModifiers_Remove`), so the item
settles into the resting grip. It covers the chief's staff,
the racks' weapons and instruments, the healer's rod, and the hand emptied when the host's member puts the item
away. The guest's own members are run by the host's game like all others, so the same field covers them (the host's
screen is the source). Switch `coop.fix tribe.held_item off`: the mirrors' own hands again. `coop.tribe` rows print
`item=`, `model=` (the hand's attachment record has a model, `SporeCoop::Creature_AttachmentOf`) and `hold=` (1: the
item is still held as in the middle of taking it).

## Verification
`tribe_held_item` (coop_tribe_looks.py --part item): every member's item and hand model on the guest are the
host's, the host's chief holds the staff with a model on both, again after the guest's rejoin; key frames of kind
sameview 14 m from the chief. `tribe_tools` also checks the weapon a guest's member took is in its hand on both.
The test also asks for the resting grip on the guest (`hold=0`, kept for 8 s).
* FAIL with `SPORE_FIX_OFF=tribe.held_item`: lab 20261110-214355 (and 20261110-210216, the test before the grip check) (the chief's copy: item 0 model 0).
* PASS: lab 20261110-214352, 20261110-215928.
* By eye: the sheets of same_view_tribe and tribe_held_item of lab 20261110-214352: the staff upright in the raised hand on both screens (its size differs: BUG-0912). The first sheets with the field alone
  (lab 20261110-211339) showed the staff sticking out forward on the guest while every state check passed: the
  modifier part of the fix and the `hold=` check came from looking at them.

## History
- 2026-11-10: opened.
- 2026-11-10: fixed on fix/w47-tribe-looks (tribe.held_item).
