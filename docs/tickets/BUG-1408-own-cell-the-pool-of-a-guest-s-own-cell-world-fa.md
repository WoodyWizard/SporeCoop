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
branch: fix/w84-tests, fix/w85-cell-pool
commits: 9cebc645 454c040a
tests: own_cell, own_cell_steady, own_cell_steady_refill_off, cell_tests solo_refill
signature:
docs: docs/re/cell.md#cell-own-pool-refill
switch: cell.solo_refill
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
* 2026-11-23 (fix/w84-tests): a real defect, not the test's: the guest's own pool is emptied by the game's tile cull
  (DestroyCell from 0xE7C3E6, lab 20261122-193406) in every own_cell run since 2026-11-13 (the cycle shows in the passing
  runs too, their trough stayed at 20+ or fell outside the last 25 s). On a guest h_TileActive (cellsync_tiles.cpp)
  returns the game's own IsTileActive (its union of view boxes runs only with 2 or more boxes, g_union, on the host),
  so the game's own answer said "not active" for the guest's tiles while its cell stood still; which input of it (the
  game's view box, the tile keys after a world scale step) is open. Not fixed here (WP-T1 is test-only); needs a mod
  ticket owner.

* 2026-11-23 (fix/w85-cell-pool), found: observed in the guest logs of all four runs (20261122-193406 #1 / #2,
  20261122-194210, 20261123-023643): the guest's first tile pass of its own world made the 40 tiles of its screen at s
  34.75-34.78 and left the cell pool at 1/4096; "host away: the guest plays its own pool (0 cells released)" came 11-19 ms
  later (solo_tick runs in the next tick). So h_Populate (a guest that is not solo mirrors the host) refused every
  gameplay marker of those tiles. The game populates a tile only when it makes it, so the 40 tiles stayed empty while
  they stayed active (pool 1 until s 64.8 in every run). The 150-180 cells came from the 1-6 tiles the idle cell's
  drifting camera made later at the edge of the active range (40 -> 41..46); when those tiles left it again (43 -> 38)
  the tile cull took every cell of the pool at once (175 -> 1 by 0xE7C3E6). The host's own pool in the same runs swings
  by whole tiles too (377 -> 180, 297 -> 132; the game's own behaviour), but its other tiles keep their cells. Inferred:
  the cull itself is correct (objdump of ShouldCullCell / IsTileActive: the cell's tile from its own level and position,
  the guest's view box and food did not change); the defect is the empty core tiles. RE notes in
  docs/re/cell.md#cell-own-pool-refill.

## Fix
None yet (cause not found). The test now prints, at each fall of the pool by half from 40 or more, the game's
DestroyCell calls by caller since the sample before (`coop.pool orphans`: "DestroyCell from") and the guest's
cell_tiles / cell_away lines, so the next run names the destroyer. The check is unchanged.
* fix/w84-tests (454c040a), test only: own_cell judges BUG-0797's claim on the whole 60 s ("40 or more own live cells
  in a read"; the twin cell.solo_elsewhere off reads 0 in every read, lab 20261112-030821), not on the last 25 s that
  caught BUG-1408's trough by chance; the trough is printed as "(BUG-1408, open: ...)". The defect has its own test:
  `own_cell_steady` (coop_own_cell.py --steady, extended, 90 s): once the pool has 40 cells it keeps 10 or more in every
  later read; it FAILS until BUG-1408 is fixed.

* fix/w85-cell-pool: `coop.fix cell.solo_refill` (cellsync_tiles.cpp): h_PopTile notes on a guest every gameplay tile
  object whose population h_Populate refused (cell_logic::RefusedTiles; a tile populated later with the own pool is
  dropped from it); solo_start, when it released no mirror or the guest plays another Cell world
  (cell_logic::solo_refill_due), lets the guest's next tile passes populate the refused tiles still in its list, 4 per pass
  (cell_logic::solo_refill_take), none at or above kPopCap and none while the world scale moves; the host's return stops
  it and purges them as strays. Off: those tiles stay empty (0.7.16.1). A host away in the host's world with mirrors
  released keeps the old way (the mirrors are those tiles' cells). `coop.pool` cell_away: `solo_refill=<tiles>/<starts>`.
  No wire change. own_cell_steady also asks for the pool filled in the first read (~25 s after the load).

## Verification
Lab (shows the cause): `SPORE_DETECTOR=20 python3 tools/lab.py run own_cell --repeat 4 --lane N --retry 0
--no-build`: the line "the guest's own pool fell ... DestroyCell by
{...}" names the caller (0xE7C3E6 = the tile cull). Open: waiting for that run, then the fix.
- Still open (2026-11-23): 20261122-193406 own_cell#2 FAIL named the destroyer: "the guest's own pool fell 175 -> 1 at 30 s: DestroyCell by {'SporeApp.exe+0xA7C3E6': 174}" (the tile cull, 0xE7C3E6), cell_tiles all 0 on the guest at that moment; #1 and #3 PASS, also 20261122-194210 and 20261123-023643. Next: the fix (why the cull takes a guest's own pool).
- Pending night run (fix/w84-tests): `SPORE_DETECTOR=20 python3 tools/lab.py run own_cell --repeat 4 --lane N --retry 0 --no-build` PASS 4 of 4; twin
  `SPORE_LAB_FIXOFF="own_cell=cell.solo_elsewhere" SPORE_DETECTOR=20 python3 tools/lab.py run own_cell --lane M --retry 0 --no-build` FAILS; `SPORE_DETECTOR=20 python3 tools/lab.py run own_cell_steady
  --repeat 2 --lane M --retry 0 --no-build` FAIL expected (the defect; with the DestroyCell callers). The ticket stays
  open for the mod fix.
- fix/w85-cell-pool: open, waiting for the lab run (pending night run: own_cell_steady, own_cell_steady_refill_off, own_cell):
  `SPORE_DETECTOR=20 python3 tools/lab.py run own_cell_steady --repeat 3 --lane N --retry 0 --no-build` must PASS 3 of 3;
  twin `SPORE_DETECTOR=20 python3 tools/lab.py run own_cell_steady_refill_off --repeat 2 --lane M --retry 0 --no-build`
  must FAIL 2 of 2 (the first read 0); `own_cell` PASS. Unit: cell_tests solo_refill (make test).

## History
- 2026-11-21: opened from the third final check of 0.7.16 (fix/w62-flake-cc); diagnostics added to the test.
- 2026-11-23: lab evidence of the 0.7.16 final check added; stays open.
- 2026-11-23: fix/w84-tests (WP-T1 of 0.7.17): own_cell judges the populate on any read; the defect gets own_cell_steady (454c040a). Kind stays bug: a real defect.
- 2026-11-23: fix/w85-cell-pool: cause found (the tiles of entering the own world populated while refused), fix cell.solo_refill; waiting for the night run.
