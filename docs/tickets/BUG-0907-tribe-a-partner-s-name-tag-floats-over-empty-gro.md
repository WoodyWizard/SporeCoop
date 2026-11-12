---
id: BUG-0907
title: Tribe: a partner's name tag floats over empty ground on the guest (no member drawn under it)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-10
source: same_view_tribe and play_tribe key frames, lab 20261110-201619
role: both
versions: 0.7.11.2 and earlier
fixed_in: 0.7.12
branch: fix/w47-tribe-looks
commits: 679037b0
tests: tribe_name_tags, same_view_tribe, play_tribe
signature:
docs: docs/re/tribe.md#tribe-name-tags, docs/06-testing.md#testing-tribe-looks
related: BUG-0764, BUG-0768
---

## Report
Seen in the review of the key frames of lab run 20261110-201619: in same_view_tribe (views 1 and 2) the guest's screen shows a name tag 'Alice' over empty ground right of the fire, with no member drawn under it (the host's screen has no creature there either); in play_tribe's start frame both games show a partner tag ('Bob' / 'Alice') in the middle of an empty slope. The census of the creatures in the picture matched 3/3, so the tag belongs to something that is no citizen on the screen (a member under the ground, a hidden mirror, an old tag). Sheets: `out/lab/20261110-201619/frames/sheets/011_same_view_tribe_01_view1.jpg`, `out/lab/20261110-201619/frames/sheets/012_same_view_tribe_02_view2.jpg`, `out/lab/20261110-201619/frames/sheets/004_play_tribe_01_start.jpg`.

## Root cause
Observed (the code, and `coop.find labels` with the fix off, lab 20261110-210554): the tag is the partner's
"puppet" tag of coop.cpp's overlay. In Tribe (and Civ) the puppet is no object but a marker at the planet point under
the partner's mouse (modes.cpp rts_fill; since input.window the last point its pointer moved over in the game
window), and the overlay drew the partner's bare name 0.5 m over it. With the pointer pinned 9 m from the members the
label "Bob" / "Alice" stood 407-483 px from every member of that partner. The labels of the members themselves
(the owner's name, "<name>'s chief") were at their members (1-3 px). Not a hidden or sunk member: the census matched
because there was none.

## Fix
coop.cpp on_overlay: in Tribe a partner's pointer gets no name; the partner's labels are those of its chief and
members, and F2's arrow when the chief is off screen. Civ keeps the name at the pointer (the "where the partner
plays" mark its arrow points at). Switch `coop.fix tribe.pointer_tag off`: the name at the pointer (0.7.11). New test
read-out `coop.find labels`: every name label of the last overlay pass with its screen point.

## Verification
`tribe_name_tags` (coop_tribe_looks.py --part tags): both pointers pinned on empty ground, the same camera on both
games; every label naming the partner within 60 px of one of the partner's members (the host's position, projected
by that game's camera), the chief labelled; a key frame of kind sameview.
* FAIL with `SPORE_FIX_OFF=tribe.pointer_tag`: lab 20261110-210554 (host: "Bob" 483 px from every member, guest:
  "Alice" 407 px).
* PASS: lab 20261110-210218, 20261110-214352, 20261110-215928 (labels 1-3 px from their members).
* By eye: the sheets of same_view_tribe, play_tribe and tribe_name_tags of lab 20261110-214352 (no name over empty ground; play_tribe, lab 20261110-214352, passed with it).

## History
- 2026-11-10: opened.
- 2026-11-10: fixed on fix/w47-tribe-looks (tribe.pointer_tag).
- 2026-11-11: released in 0.7.12.
