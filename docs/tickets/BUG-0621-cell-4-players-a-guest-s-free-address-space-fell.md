---
id: BUG-0621
title: Cell, 4 players: a guest's free address space fell 3502 -> 1427 MB in 6 minutes at 1-3 fps
status: open
kind: bug
stage: Cell
reported: 2026-11-06
source: lab 20261030-104149 campaign#2 (split from BUG-0307)
role: guest
versions: 0.7.3.1
fixed_in:
branch:
commits:
tests: cell_nan_drain, cell_churn, cell_nan_all
signature:
docs: docs/known-issues.md#issue-cell-own-nan
related: BUG-0307, BUG-0314, BUG-0355
---

## Report
Lab 20261030-104149 campaign#2 (main 813f8cd, lane 4), the third player (a guest, 900 mirrors): while its own
cell went NaN 26 times (BUG-0307) and its fps fell from 26 to 1-3, its free address space fell from 3502 MB to
1427 MB in 6 minutes; the other three games stayed at ~3500 MB. Split from BUG-0307 on 2026-11-06 when that
ticket's NaN source was found and fixed (the collision contact normal, cell.contact_finite).

## Root cause
Not known. Negative (BUG-0307's history): re-creating all the guest's mirrors 40 times (cell_churn, lab
20261030-121142) cost 0 MB; 15 s of an own cell NaN in every field (lab 20261030-122306) 0 MB; 3 minutes of a NaN
own cell with level-ups (cell_nan_drain, lab 20261030-133256) 0 MB; 4 players with a guest jumping at 3 fps
(28 538 mirrors created) 3613-3620 MB. Candidates not checked: model / resource loads for the new cells around a
jumping centre, something that grows with the frame time at 1-3 fps, BUG-0355's per-frame CreateSoftBody calls on
a full graphics pool (fixed since by cell.gfx_full).

## Fix
None yet.

## Verification
no-fail-run: not reproduced (see Root cause).

## History
- 2026-11-06 (fix/w45-cell-nan): opened, split from BUG-0307 (its NaN source fixed there).
