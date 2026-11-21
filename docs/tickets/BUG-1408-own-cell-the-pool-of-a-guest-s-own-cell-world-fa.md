---
id: BUG-1408
title: own_cell: the pool of a guest's own Cell world falls from ~170 cells to 0-25 within 5 s and fills again 15-25 s later; the check's last 25 s catch the trough
status: open
kind: bug
stage: Cell
reported: 2026-11-21
source: lab rel/0.7.16 20261121-003202 own_cell
role: 
versions: 0.7.15.1, unreleased
fixed_in:
branch: fix/w62-flake-cc
commits:
tests: own_cell
signature:
docs:
related: BUG-0797
---

## Report
0.7.16 third final check (lab 20261121-003202, lane 6): own_cell FAIL "the guest's own game populates its pool ([0, 0, 0,
153, 153, 153, 174, 0, 0, 22, 21, 20]: the last 25 s)". Known flake: 2 of 10 on rel/main, 5 of 15 on all branches; the
same shape in fix/w49-warm 20261113-090414 ([.., 170, 1, 23, 22, 19, 170]) and rel/0.7.15.1 20261118-110806 ([.., 166, 13,
11, 9, 9, 9, 9]). Old (before 0.7.16 and before the w61 changes).

## Root cause
Not found in the logs. Observed:
* the guest's log (own world Othercell, solo=1 from the load at s 35.9 to the end, no "host back" line): `cell pool`
  1/4096 until s 65.9, 154 at s 75.9 (one populate burst ~40 s after the load, as docs/re/cell.md#cell-own-world-pool
  says), 172 at s 95.9, then 23 at s 105.9 and 22 / 19 after; its status line has pool=175 at s 90 and pool=0 at s 100.
  The mod logs nothing in between (no purge: the guest drops MSG_CELL_STATE while solo; the guest's own population is
  the game's: pop_tiles runs the game's pass, `ours` is false on a guest; the mod's only other destroys are the
  background cull over 150 m and the mirrors).
* every own_cell run since 2026-11-13 shows the same cycle: a batch of ~150-180 cells made at once, gone 20-25 s later
  except ~20 (or all), the pool filled again 15-25 s after: [0, 0, 0, 28, 185, 180, 178, 21, 20, 20, 20, 167],
  [0, 0, 0, 167, 140, 168, 165, 25, 25, 24, 23, 169]. The check (min of the last 5 samples >= 10) passes when the
  trough is not in the last 25 s or stays at 20+.
Inferred: the game's own tile cull (0xE7C080 -> DestroyCell, return 0xE7C3E6) when the guest's tiles go inactive, or the
game's per-tile population lifetime; why a whole batch goes at once while the guest's cell barely moves (99.9 100.6 ->
96.8 100.5) is open. Whether a player sees it: the guest of a session who plays its own Cell world swims in a nearly
empty pool for ~20 s every ~45 s; not reported by players.

## Fix
None yet (cause not found). The test now prints, at each fall of the pool by half from 40 or more, the game's
DestroyCell calls by caller since the sample before (`coop.pool orphans`: "DestroyCell from") and the guest's
cell_tiles / cell_away lines, so the next run names the destroyer. The check is unchanged.

## Verification
Lab (shows the cause): `SPORE_DETECTOR=20 python3 tools/lab.py run own_cell --repeat 4 --lane N --retry 0
--no-build`: the line "the guest's own pool fell ... DestroyCell by
{...}" names the caller (0xE7C3E6 = the tile cull). Open: waiting for that run, then the fix.

## History
- 2026-11-21: opened from the third final check of 0.7.16 (fix/w62-flake-cc); diagnostics added to the test.
