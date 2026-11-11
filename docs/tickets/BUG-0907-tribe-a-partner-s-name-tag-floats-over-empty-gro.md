---
id: BUG-0907
title: Tribe: a partner's name tag floats over empty ground on the guest (no member drawn under it)
status: open
kind: bug
stage: Tribe
reported: 2026-11-10
source: same_view_tribe and play_tribe key frames, lab 20261110-201619
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Seen in the review of the key frames of lab run 20261110-201619: in same_view_tribe (views 1 and 2) the guest's screen shows a name tag 'Alice' over empty ground right of the fire, with no member drawn under it (the host's screen has no creature there either); in play_tribe's start frame both games show a partner tag ('Bob' / 'Alice') in the middle of an empty slope. The census of the creatures in the picture matched 3/3, so the tag belongs to something that is no citizen on the screen (a member under the ground, a hidden mirror, an old tag). Sheets: `out/lab/20261110-201619/frames/sheets/011_same_view_tribe_01_view1.jpg`, `out/lab/20261110-201619/frames/sheets/012_same_view_tribe_02_view2.jpg`, `out/lab/20261110-201619/frames/sheets/004_play_tribe_01_start.jpg`.

## Root cause
Not analysed: which object the tag is drawn for (the overlay's tag list at that moment).

## Fix
Not part of feat/w47-visual (found by its review step).

## Verification
Open.

## History
- 2026-11-10: opened.
