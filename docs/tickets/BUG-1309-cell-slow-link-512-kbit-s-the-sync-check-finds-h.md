---
id: BUG-1309
title: Cell, slow link (512 kbit/s): the sync-check finds hundreds of the host's cells missing on a guest that just arrived
status: fixed
kind: desync
stage: Cell
reported: 2026-11-19
source: lab: evo_screens_switch_slow
role: guest
versions: 0.7.15.1, 0.7.15.2, 0.7.16
fixed_in: unreleased
branch: fix/w57-cellslow
commits: eb50b7d9, 7c4e2d49, 88973cd7, 4103ec7e, 0429715f, cafb0ef1
tests: cell_slow_link, cell_slow_link_off, evo_screens_switch_slow
signature: sync-check "c: N missing on the guest" under SPORE_NET=lowband
docs: docs/re/cell.md#cell-slow-link
related: BUG-1240, BUG-1245, BUG-1255
---

## Report
The lab marks every run of `evo_screens_switch_slow` (coop_evo_screens.py --part switch, SPORE_NET=lowband: the
guest's link capped at 512 kbit/s, 40 ms delay) FAILED by the log gate sync-check, while the test's own checks pass
(RESULT: OK). rel/0.7.16 lab 20261119-102828 (lane 2): host and guest log
`sync-check: MISMATCH ... (Cell 9652F8AD, 135 objects compared): c: 247 missing on the guest` 7.5 s after the guest
appeared in the Cell world (its world transfer took 56 s at 55 KB/s with 72 % resends), and again
`c: 13 missing on the guest` at the start of the Creature switch vote (100 s).

Not new in 0.7.16: the same signature in every run of this test since it exists (2026-11-18), on the release trees
too: rel/0.7.15.1 out/lab/20261118-110806 (301 missing), 20261118-121937 (304), rel/0.7.15.2 out/lab/20261118-144132
(295 missing, 1 extra), 20261118-164423 (165), fix/w53-cellvote 20261118-123948 (188). BUG-1240's ticket noted it
on 2026-11-18 as outside that fix. The clean-link twins (evo_screens_switch, _noready, _lowfps) have no Cell
mismatch.

## Root cause
Real, not the check's window (fix/w57-cellslow, 2026-11-19). The host sent each guest its whole Cell state
(MSG_CELL_STATE: 63 bytes a cell, chunks of 14, ~29 chunks for the ~400 cells of level 1) ten times a second, every
round in the same pool order and in one frame: ~260 KB/s per guest. A 512 kbit/s link carries 64 KB/s; its queue
(400 ms in netem.py's lowband, a home router's buffer alike) kept the same first ~7 chunks of every round and dropped
the rest every time (host log: `net: spike with player 1: loss 70.7% of 1605 packets in 5 s` after the arrival). The
guest's first round after the world load came through whole (the queue was empty: `pool=364 mirrors=364` 1 s after
the arrival), then the guest removed every mirror not listed for a second (BUG-0032's rule for a round with a lost
chunk): its own game kept ~100 of the host's ~400 cells, the ones in the first chunks (pool order, spread over the
whole pool, not the nearest), for as long as the link was full. A player on such a link (or a host with a slow
upload: three guests are three times this) saw mostly empty water around its cell, and the reliable messages competed
with the flood (72 % resends). Measured with the new `coop.pool near` on the guest's own game: 25-46 % of the host's
cells within 40 units of the guest over 40 s with the old sender (twin run 20261119-130241), ~100 mirrors in all.
The second, smaller mismatch at the switch vote (`c: 13 missing`, level 10, ~140 cells = 10 chunks) is the same
overload with fewer cells.

## Fix
`cell.slow_link` (cellsync_host.cpp, `cell_logic::LinkPace` in cell_logic.h, docs/re/cell.md#cell-slow-link): each
guest's round is sorted nearest first, and the host sends it no faster than the guest's link carries. The rate follows
the connection's own loss counters: a 0.5 s window with over 15 % of the packets lost cuts it to 85 % of the cell state
that got through, a window under 3 % with the rate used raises it by 15 %, above 2 MB/s it is unlimited (the old whole
round every tick, nearest first). A round lists the nearest cells the rate sends in 0.4 s and goes out over several
ticks within its tokens (at most 0.25 s of the rate at once). Random loss of a few % never cuts. Wire format unchanged
(a 0.7.15 guest reads the paced rounds). `coop.pool pace` shows each guest's rate; the host logs the first cut per
guest and 30 s (`cell: the link to player N carries less than the Cell state ...`). Off: whole rounds in pool order
every tick (0.7.16 before this fix). Unit test: cell_tests link_pace (the 512 kbit/s queue in numbers: the nearest
100 cells reach the guest every second with the pace, under 30 without).
Test: cell_slow_link (coop_cell_slow_link.py, SPORE_NET=lowband): every 2 s for 40 s the host's cells within 20 units
of the guest that were there 2 s before, against the guest's own game's cells there; PASS: at least 80 % in every
sample but one from 8 s on, median 90 %; key frame slow_link_cells. Twin cell_slow_link_off must FAIL.
The sync-check (diag_sync.cpp) takes a paced guest's round cap as the host's "nearest N" cells, the rule it already
applies with 900 (BUG-0295, BUG-0379: the nearest 80 % of N must be on the guest): within its 60 units the host has
~440 cells at level 1, more than a 512 kbit/s link carries in a round (~250-380), and a merged-tree run with the
test's own checks at 100 % within 20 units still had `c: 105 missing` there (20261119-131719). Not paced (a link
without loss, or cell.slow_link off): 900 as before.

## Verification
cell_slow_link (coop_cell_slow_link.py, SPORE_NET=lowband, lane 10):
* PASS out/lab/20261119-130733 (cell_slow_link): 100 % of the host's cells within 20 units of the guest (55-108 of
  them) in the guest's own game in every one of 25 samples over 48 s; the sync-check OK; the host paced to ~54 KB/s.
  An earlier run with 40 units (20261119-130241) had median 1.00 but 0.71-0.79 at four moments: 270-490 cells, more
  than one round at 512 kbit/s carries (~320 nearest), the wrong measure (the test's docstring says why).
* Merged tree (rel/0.7.16 b0fe4ada in, the sync-check rule for paced guests): PASS out/lab/20261119-134335
  (cell_slow_link: min 0.92, median 1.00 over 19 samples, sync-check OK with 505 objects compared) and
  out/lab/20261119-133159 (cell_slow_link OK; evo_screens_switch_slow OK with the lab gates clean: its sync-check
  `OK ... 454 objects compared` where every run since 2026-11-18 had `c: 165-304 missing`); twin FAIL
  out/lab/20261119-134335 and 20261119-133159 (cell_slow_link_off: median 0.26 / 0.29, gate `c: 290 missing`).
  Regression guards on the same tree, 20261119-131719: cell_coop OK (clean link), cell_mirror_turn_net OK (250 ms,
  5 % random loss: no cut). Key frames slow_link_cells looked at: at level 1 the screen shows 2-16 cells, the twin's
  and the fix's frames differ little; the numbers are the proof.
* After merging rel/0.7.16 6c8cc309: PASS out/lab/20261119-135303 (cell_slow_link), twin FAIL in the same run
  (cell_slow_link_off: median 0.30, gate `c: 182 missing`).
* FAIL out/lab/20261119-130733 (cell_slow_link_off, cell.slow_link off): 23-45 % (median 0.35) over 20 samples, and the
  lab's sync-check gate `c: 13 missing on the guest`; 20261119-130241 (twin, 40 units): 26-46 %, ~100 mirrors in all.


## History
- 2026-11-19: opened.
- 2026-11-19: fixed on fix/w57-cellslow (cell.slow_link).
- 2026-11-20 (fix/w61-cell): second final check: cell_slow_link with `SPORE_LAB_FIXOFF=cell_slow_link=cell.slow_link` FAILED (lab 20261120-133208, median 0.31) where the night plan expected a PASS: the expectation was wrong (it is the twin's case; BUG-1334's split command names cell_kin_species), nothing is broken; the twin cell_slow_link_off FAILED as wanted (20261120-130608, median 0.28).
