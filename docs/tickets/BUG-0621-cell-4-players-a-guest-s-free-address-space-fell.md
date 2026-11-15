---
id: BUG-0621
title: Cell, 4 players: a guest's free address space fell 3502 -> 1427 MB in 6 minutes at 1-3 fps
status: fixed
kind: bug
stage: Cell
reported: 2026-11-06
source: lab 20261030-104149 campaign#2 (split from BUG-0307)
role: guest
versions: 0.7.3.1
fixed_in: 0.7.3.2
branch: fix/w26-cell-spare (the fix), fix/w46-addr-space (the evidence and test)
commits: 0b1e55e, cd89fd4, 1e827ed, cd75347
tests: mem_census_cell_crowd, cell_spare, cell_nan_drain, cell_churn, cell_nan_all
signature:
docs: docs/re/graphics.md#graphics-address-space, docs/known-issues.md#issue-cell-own-nan
related: BUG-0307, BUG-0314, BUG-0355, BUG-0631
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

Found (2026-11-06, fix/w46-addr-space, the address space census docs/06-testing.md#testing-mem-census):
* Observed: BUG-0355's case drains the address space. A crowd of 1000 of the host's cells around its own (`coop.pool
  crowd 1000 8` every 20 s, 8 min) with BUG-0355's fixes off (`cell.gfx_full`, `cell.spare`): the host's game heap
  grows 16 MB every 2 minutes, linear, 8.3 MB/min (lab 20261106-210201, 20261106-215757), from effects and animations
  (callers Swarm::EffectWorld::CreateEffect 0xA71408, Anim::AnimatedCreature::PlayAnimation 0xA0F707: every frame the
  game calls CreateSoftBody again for each cell past the 256th of the full cell graphics pool, which sets the cell up
  again and plays an animation). With them on: 0 MB/min (20261106-211056, 20261106-215759).
* Inferred: the campaign guest of this report (0.7.3.1, lab 20261030-104149, before BUG-0355's fix in 0.7.3.2) had
  900 mirrors crowding its screen: the same per-frame calls (BUG-0355's own report shows the same drain, 3526 -> 1899
  MB at 3 fps); its 1-3 fps were those calls too. The 2000 MB in 6 min (~350 MB/min) is faster than the 8 MB/min of
  the lab's 1000 cells around one host: the guest had its whole mirror population in the active area at 1-3 fps
  frames; not measured at that size.

## Fix
BUG-0355's `cell.gfx_full` (no soft body is made while the cell graphics pool is full) and `cell.spare`, 0.7.3.2
(fix/w26-cell-spare, commits 0b1e55e, cd89fd4, 1e827ed, cd75347). Nothing new in this branch but the test.

## Verification
`mem_census_cell_crowd` (coop_mem_census.py --fixture cell --crowd 1000 --minutes 8 --sample 2 --max-slope 3): no game
loses over 3 MB/min of free address space (least squares from the second census).
* FAIL: 20261106-215757 (`SPORE_FIX_OFF=cell.gfx_full,cell.spare`: the host 8.3 MB/min, 3470 -> 3420 MB).
* PASS: 20261106-215759 (host 0.0, guest 0.0 MB/min).

## History
- 2026-11-06 (fix/w45-cell-nan): opened, split from BUG-0307 (its NaN source fixed there).
- 2026-11-06: Discord report 2026-11-06 (0.7.8): a sibling in normal play, BUG-0631: a host on the 2 GB space (large-address flag off) drained 1935 -> 133 MB free (largest block 12 MB) over ~2 h Cell > Civ, ~740 MB of it in 50 min of Creature (the guest ~384 MB in the same time), and crashed in Civ in a model's vertex blend (+0x3995F0). Same class (address space drained), other scenario; what grows on the host is not known yet.
- 2026-11-06 (fix/w46-addr-space): the drain measured with the address space census: BUG-0355's per-frame soft body
  calls on a full cell graphics pool (8.3 MB/min with that fix off, 0 with it on); fixed in 0.7.3.2 by BUG-0355.
  New test mem_census_cell_crowd. Status fixed.
