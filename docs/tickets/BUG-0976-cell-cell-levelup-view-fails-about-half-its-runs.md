---
id: BUG-0976
title: Cell: cell_levelup_view fails about half its runs on the level-ups: the guest loses 76-100% of its screen on an up change where the host's game loses 0-60%
status: fixed
kind: test
stage: Cell
reported: 2026-11-12
source: fix/w48-cellcull labs 20261112-195306-2, 20261112-200916, 20261112-202450
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-cell
commits: 23b91609, 23cc5481
tests: cell_levelup_view
signature:
docs: docs/06-testing.md#testing-cell-c1-0714
related: BUG-0635, BUG-1064
---

## Report
Seen while checking BUG-0975 on lanes 6 / 7 (GOG 2024). `cell_levelup_view` (BUG-0635) failed its screen check in 5 of
7 runs: guest 55-82 % lost on average against host 28-47 % (limit host + 20 %). The failing changes are the level-UPS
(+150): the guest lost 70-100 % of its screen, the host's game 0-60 % (often with fewer than 8 cells on the host's
screen). The downs were 0-53 %. Same with BUG-0975's switch off (`SPORE_FIX_OFF=cell.tile_cap`, lab 20261112-200916:
1 of 2 failed) and with the final BUG-0975 code, whose hold does not run in this test (0 skipped passes, lab
20261112-202450: 1 of 2 failed). Earlier history: 61 % / 81 % failures on 2026-11-07 (labs 20261107-161122,
20261106-224735). So not caused by BUG-0975; the test or the BUG-0635 fix (`cell.levelup_keep`) does not hold on ups.

How to reproduce: `tools/lab.py run cell_levelup_view --lane N --repeat 4`.

## Root cause
The test, not a loss of cells. Observed (lab 20261113-021621, 4 runs, 1 failed; run 4 with the guest's screen keys
also asked of the host's game): 0 cells gone only on the guest in all changes; every cell gone from the guest's
screen was gone in the HOST's game too, and the host's wipe destroyed none of them (`wipe_near_lost` 0). The guest
shows the host's world exactly. The failures came from the up changes: a level-up removes 43-91 % of any screen in a
game alone (the host's own screen), the host's screen then held as few as 7 cells, and the check compared the
per-change average of two different places with a margin of 20 points. Left over: round a partner the host's game
removes about 15 points more of the old cells on an up change than round its own cell (10 of 11 ups): BUG-1064.

## Fix
Test only (tools/tests/coop_cell_levelup_view.py): a new check that no cell is gone on the guest while the host's game
still has it (at most 10 %); the BUG-0635 comparison is pooled over the down changes (guest loss <= host + 25 points,
at least 35 % allowed); the up changes are printed, not compared. No game code change, so no switch.

## Verification
no-fail-run: test only (no game code changed); the decision rests on the runs below.
Evidence: out/lab/20261113-021621 (4 runs of the old test: 1 FAIL, 3 OK; run 4 with the host cross-check: 0 cells gone
only on the guest); out/lab/20261113-022529-2 (reworked test: 0 of 284 gone only on the guest).
The reworked test still catches BUG-0635: FAIL with SPORE_FIX_OFF=cell.levelup_keep out/lab/20261113-024127 (the host's
wipe destroyed 628 cells on the guest's screen). PASS: out/lab/20261113-024127-2 (2 runs).

## History
- 2026-11-12: opened.
- 2026-11-13: closed on fix/w49-cell (package C1): the test, not a loss of cells; BUG-1064 opened for the extra loss round a partner.
- 2026-11-14: released in 0.7.14.
