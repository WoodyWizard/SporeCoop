---
id: BUG-0906
title: Tribe: the guest's copy of the host's chief has no chief's staff (the host's game draws it)
status: open
kind: bug
stage: Tribe
reported: 2026-11-10
source: same_view_tribe key frames, lab 20261110-201619
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
Seen in the review of the key frames of lab run 20261110-201619 (same_view_tribe, the same fixed camera on both games): the host's game draws its chief ('Alice's chief') holding the chief's staff (a stick with a white skull over its head); the guest's game draws the same member at the same place without the staff. Same in view 1 and view 2. Sheets: `out/lab/20261110-201619/frames/sheets/011_same_view_tribe_01_view1.jpg`, `out/lab/20261110-201619/frames/sheets/012_same_view_tribe_02_view2.jpg`; frames in out/lab/20261110-201619/frames/same_view_tribe/. Reproduce: `tools/lab.py run same_view_tribe --lane N` and open the sheets (`tools/frames_review.py <run>`).

## Root cause
Not analysed. The guest's mirror of a host member is a citizen of the guest's game; the chief's held staff (a held tool or a part of the chief's look) is not copied to it.

## Fix
Not part of feat/w47-visual (found by its review step).

## Verification
Open: a check would compare the held tool of the chief's mirror with the host's.

## History
- 2026-11-10: opened.
