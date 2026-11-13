---
id: BUG-0936
title: Tribe: members idling at the village stand on one game while they crouch or sit at the fire on the other (same-view pairs differ in pose, in both directions)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-090547
role: 
versions: 0.7.12 (unreleased)
fixed_in: 0.7.13
branch: fix/w48-tribe-mirrors
commits: 6fc65d20
tests: tribe_mirror_idle
signature:
docs: docs/re/tribe.md#tribe-mirror-looks, docs/06-testing.md#testing-tribe-mirror-looks
related: BUG-0914, BUG-0569, BUG-0889
---

## Report
Key-frame review of the second final check out/lab/20261111-090547 (rel/0.7.12 94ca59ab).
Sheets: `out/lab/20261111-090547/frames/sheets/051_same_view_tribe_02_view2.jpg`, `out/lab/20261111-090547/frames/sheets/052_same_view_tribe_03_view3.jpg`, `out/lab/20261111-090547/frames/sheets/056_tribe_name_tags_01_tags.jpg`
(same camera on both games; the label's automatic check passed: 3 / 3 matched by place).
Frames: `out/lab/20261111-090547/frames/same_view_tribe/02_view2_host.jpg` / `_guest.jpg`, `03_view3_host.jpg` / `_guest.jpg`,
`out/lab/20261111-090547/frames/tribe_name_tags/01_tags_host.jpg` / `_guest.jpg`.

* view 2: "Bob's chief" (the guest's chief) stands upright on the host; on the guest it sits on the ground at the fire.
* view 3 (seconds later): on the host "Bob's chief" and the member "Alice" crouch toward the fire with the arms
  out; on the guest both stand upright in the rest pose and face another way.
* tribe_name_tags: "Bob's chief" sits low on the host and stands tall with an arm out on the guest.
* Places agree (the same spots in the picture), the held items agree; only the pose and the facing differ.
* views 1 and 4 and both tribe_held_item pairs agree except for the staff swing of BUG-0914 (view 4: the host's
  chief holds the staff as a spear, the guest's copy holds it upright).

Severity: low (cosmetic, members at rest). Confidence: medium (3 of 7 Tribe same-view pairs; stills only, a lag of
a few seconds of one animation cannot be told from another animation). Regression of 0.7.12: unknown; the first
final check's review (20261111-044257) wrote that the Tribe pairs agree apart from BUG-0914.

## Root cause
Proven (lab, `coop.tribe anims` of both games twice a second). The relay (BUG-0569) starts the host's behaviour
animations on the mirrors and a mirror keeps each until it ends: it has no behaviour tree to cut it short. The host's
members' trees cut their idle variants (0x02C39001..3: sitting / crouching at the fire), gestures (0x04C6CB02) and
work short and the members stood (0x02481DE5) or walked (0x04866DAB / 0x04866D8E) again by their own idle refresh,
which was never relayed: with the host's member in its own idle choice, the guest's mirror stood or walked too in
only 30 % (probe) / 62 % (test, fix off) of the samples. The BT's idle override +0xE3C was -1 on every member (not
it). Both directions of the report follow: a mirror still sitting where the host's member stands, and a member that
the host's game set into a pose after the mirror's relayed one ended (the snapshot relayed behaviour animations
only).

## Fix
`tribe.mirror_idle`: the host notes each member's idle-refresh plays (game.cpp's idle listener) and once a second sends
what every member plays that is not its behaviour animation, with whether it is its idle choice (MSG_NPC_ANIM kind 10);
a guest mirror playing something else ends it (not within 1.5 s of a relayed start, at most every 2 s per mirror):
the game's idle refresh (forced) for an idle choice, the host's animation for another start (never the locomotion's
idle / walk ids).

## Verification
Test `tools/tests/coop_tribe_mirror_idle.py --part idle` (suite `tribe_mirror_idle`): samples where the host's member
is in its own idle choice; the mirror must play it or its own idle / walk in 80 %; key frame `rest` of kind sameview.
* FAIL with `SPORE_FIX_OFF=tribe.mirror_idle`: out/lab/20261112-092953 (62 %).
* PASS: out/lab/20261112-090616 (96 %), out/lab/20261112-094544 (99 %).

## History
- 2026-11-11: opened.
- 2026-11-12: fixed (fix/w48-tribe-mirrors): the host's idle choices relayed.
- 2026-11-12: released in 0.7.13.
