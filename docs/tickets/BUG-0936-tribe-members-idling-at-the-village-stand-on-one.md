---
id: BUG-0936
title: Tribe: members idling at the village stand on one game while they crouch or sit at the fire on the other (same-view pairs differ in pose, in both directions)
status: open
kind: bug
stage: Tribe
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-090547
role: 
versions: 0.7.12 (unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
Not analysed. Observed: the difference goes both ways (the guest shows a sit the host does not, the host shows a
crouch the guest does not) and touches the host's member as well as the guest's chief.
Inferred (medium): the idle animations of a member at rest (sit or warm at the fire, look around) are picked by
each game for itself, or are not part of what tribe.mirror_anims relays (BUG-0569 relays behaviour animations;
BUG-0914 already asks whether idle animations with an item are carried). Probably the same cause as BUG-0914;
kept apart because here no item is involved and the guest's own chief differs on the host too.

## Fix
Open.

## Verification
Open. How to test: same_view_tribe with the members at rest by the fire for 60 s: sample the game's current
animation id of each member on both games every second (the read tribe_held_item / BUG-0569's test uses) and
require the same id within 1 s for 90 % of the samples, the facing within 30 degrees.

## History
- 2026-11-11: opened.
