---
id: BUG-1064
title: Cell: on a level-up the host's game removes about 15 points more of the cells round a partner than round its own cell
status: fixed
kind: bug
stage: Cell
reported: 2026-11-13
source: fix/w49-cell labs 20261113-021621, 20261113-022529-2 (BUG-0976 analysis)
role: host
versions: 
fixed_in: 0.7.14
branch: fix/w49-cell2, fix/w49-cell3
commits: d7f41e27, 5f75d936, 58c4aad0, 19ea35f8
tests: cell_levelup_area, cell_levelup_view
signature:
docs: docs/re/cell.md#cell-levelup-partner-area, docs/re/cell.md#cell-levelup-cull-measured, docs/06-testing.md#testing-cell-c2-0714, docs/06-testing.md#testing-cell-levelup-area-apart
related: BUG-0976, BUG-0635, BUG-1080
---

## Report
Seen while closing BUG-0976 (cell_levelup_view, lane 4, GOG 2024). The guest 30 m from the host, the food +150 / -150.
On the UP changes the share of the guest's screen that is gone 5 s later was higher than the share of the host's own
screen in 10 of 11 up changes (guest / host: 91/77, 75/75, 95/83, 93/67, 94/43, 79/62, 89/88, 96/91, 89/79, 72/50,
85/62 %; labs 20261113-021621 runs 1-4 and 20261113-022529-2 has 95/87, 93/82, 14/78). Every one of those cells was
gone in the HOST's game too (`coop.pool alive` of the guest's screen keys on the host: 0 of 284 and 0 of 240+ gone only
on the guest), and the host's wipe destroyed none of them (`wipe_near_lost` 0): something else in the host's game
removes the old-level cells round a partner more than round its own cell. A game alone loses 43-91 % of its screen
on an up change anyway (the game's own), so the extra is about 15 points. Down changes, pooled per run: guest / host
21/21, 48/25 (one change 70 % against 20 %), 26/12 % (labs 20261113-021621 runs 1-2, 20261113-022529-2): also more
round the partner in most runs, all of it gone in the host's game too, none by the host's wipe.

How to reproduce: `tools/lab.py run cell_levelup_view --lane N --repeat 4`, compare the per-change lines.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lab 20261113-044709, `coop.pool areas`: the cells of the host's visible box and of the same box round the
partner in the host's game, the DestroyCell caller of each one gone, and how far the partner's view centre was from its
cell: 17-37 m while the scale moved): the extra loss round the partner comes from
the game's cell cull 0xE5F8C0 (DestroyCell from 0xE7C080, return address 0xE7C3E6: a cell without a parent whose tile
IsTileActive refuses) and on up changes also more of the old tier shrinking away (0xE7C03C). The host's IsTileActive
hook keeps a tile inside any player's box, the boxes built every frame from the view centres. coop.cpp noted a
partner's centre (`views::note`) from the pose of its state, which is in the PARTNER's world scale (BUG-0519); a level
change rescales each world over ~3.5 s at its own moment, so during the change the centre was off the partner's place
by the ratio of the two scales and the cells round the partner fell outside every box. Not the flush of the partner
lists (C1's guess): tiles do not own cells, the cull reads only the boxes.

The rest (fix/w49-cell3; observed, 22+ runs, lanes 2 / 3 / 6 / 7, `coop.pool areas` with per-cull and per-ring
diagnostics): not a co-op loss, the test's own measurement.
* Observed: the guest was 1-8 m from the host at the marks in every lab of fix/w49-cell2 (044709-2, 071833, 123921,
  123921-2, 162634-2; the one `coop.pool tp -60 -40` right after the start did not hold). The cells were assigned to
  the host's area first, so the "partner's area" was only the band of its box outside the host's box: the outer cells,
  which a down change (positions x 3 about the origin, the view box fixed in size) pushes out of the doubled range
  first, while the overlap belonged to the host's area and was kept by both boxes. Down changes, labs
  20261113-165909 .. -165915: the cull took 0 % of the cells inside 3/6 of the box half, then host 3 / 15 / 31 / 53 %,
  partner 2 / 13 / 49 / 75 % per sixth; 48 % of the partner's marked cells were in the outer two sixths, 37 % of the
  host's.
* Observed: with the guest moved 70 m (world scale 0.5) / 30 m (1.5) away before each change and the host's camera
  on its cell, the partner's area lost no more than the host's beyond noise: pooled per run partner minus host
  -38 .. +9 points on the down changes (mostly far below 0), -14 .. +17 on the up changes, 20 runs (labs
  20261113-170542 .. -171723). One area alone loses 2-57 % of its cells in one down change, the host's as much as
  the partner's: IsTileActive (0xE5F810 / 0xE5F610, objdump) rounds the doubled box outwards to whole tiles, so what
  survives a rescale depends on where a box's centre sits in the tile grid. The game's own, round the host too.
* Observed, negative: no tile pass skipped by the tile cap on these changes and the partner boxes the cull read
  always built in the same frame (no stale box); ShouldCullCell's +0x36C is a parent CELL (pool G+0x1C), not a tile
  (CORRECTION in docs/re/cell.md and the .sym), so the flush of the partner tile lists does not expose cells to it.
* Observed: during a down change the noted partner centre trails the drawn partner cell by 0.5-2.6 m; a box on the
  drawn cell would have kept 130 of 217 culled partner cells in that frame (labs 20261113-164346 .. -164352). A fix
  for it (`cell.view_centre_drawn`, the partner's box on its puppet cell) took those culls to ~0 but did not change
  the pooled loss (labs 20261113-165344 .. -165350 together, -170542 .. -170548 apart, on / off alike): those cells
  leave the range a few frames later anyway. Not shipped.
* Observed: a still cell's camera stays where the rescale left it (7-40 m off the host's cell in 2 of 8 runs): the
  host's "own area" was then not round its cell. The test puts the camera on the cell (`coop.pool camsnap`) and does
  not pool a mark with the view more than 10 m off.
BUG-1080 (folded in) is the same measurement: "the few cells round the partner" were that band.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
The cause in co-op: `cell.view_centre_scale` (fix/w49-cell2, below). The rest needs no game change (Root cause);
fix/w49-cell3 corrected the test: `coop_cell_levelup_area.py` moves the guest apart before each change, puts the
host's camera on its cell (new test command `coop.pool camsnap`), pools only marks with the areas apart and the
host's view on its cell, and asserts the shares by default (`--print-only` prints them): the partner's area may lose
at most 25 points (up) / 15 points (down) more than the host's (up: mean +2.4, sd 9.2 over 20 runs; down: max +9).
`coop.pool areas` prints the cull diagnostics (`cull`, `rings`, the tile-cap skips) and the host log the first culls.

fix/w49-cell2 (before): PARTIAL. `cell.view_centre_scale` (coop.cpp `view_pos`): in Cell the host notes a partner's view centre times partner
scale / own scale, where the partner's cell is drawn (cell_drive's BUG-0519 conversion). Off: the pose as it came.
It fixes a measured error (the centre 17-37 m off the partner's cell while the scale moved, 0.7-3.6 m with it) but NOT
the whole symptom: with it the down changes still removed more round the partner in 3 of 5 runs (28 / 16 %, 87 / 24 %,
74 / 22 %; 11 / 20 % and 22 / 12 % in the others). The rest is not analysed (BUG-1080 was opened for it and folded in
here). BUG-0975's tile cap untouched (cell_level_drop PASS).

## Verification
Test `cell_levelup_area` (the guest 60 m left of / 40 m below the host, six +-150 changes; `coop.pool areas`).
The centre (the cause fixed): FAIL (SPORE_FIX_OFF=cell.view_centre_scale) out/lab/20261113-044709 (37.2 m, printed by
the test version of then; its area shares failed: down 52 % / 10 %), out/lab/20261113-162634 (36.9 m, the check
fails; shares down 49 / 23 %); PASS out/lab/20261113-071833 (1.6 m),
out/lab/20261113-123921 (3.0 m), out/lab/20261113-123921-2 (3.6 m), out/lab/20261113-162634-2 (2.0 m; shares down 25 / 10 %).
The area shares (the symptom, still open): with the fix down changes 11 / 20 % (out/lab/20261113-044709-2), 28 / 16 %
(out/lab/20261113-071833), 87 / 24 % (out/lab/20261113-123921), 74 / 22 % (out/lab/20261113-123921-2), 25 / 10 % (out/lab/20261113-162634-2): printed by the
test, asserted with --strict only. Up changes: the partner never lost more than the host beyond 0 points.
Runs before 20261113-044709 had the guest 5 m from the host (`tp 200 0` clamped at the level's edge): no evidence.
cell_levelup_view PASS (guest-only loss 0 of 170, wipe_near_lost 0) and cell_level_drop PASS: out/lab/20261113-071833.

Final (fix/w49-cell3, the reworked test, the shares asserted): PASS out/lab/20261113-171719 (2 repeats),
out/lab/20261113-171721 (2), out/lab/20261113-171723 (2): 6 of 6; FAIL with SPORE_FIX_OFF=cell.view_centre_scale
out/lab/20261113-171725 (centre 15.6 m; up +20 points) and out/lab/20261113-171219 (15.7 / 21.7 m, 2 of 2). Earlier
versions of the margins (10 / 20 points; now 15 / 25): the same PASS runs. Regression (fix/w49-cell3): cell_levelup_view
PASS out/lab/20261113-172237, cell_level_drop PASS out/lab/20261113-172239, cell_flush_handover PASS
out/lab/20261113-172241, play_cell --quick PASS out/lab/20261113-172243.

## History
- 2026-11-13: opened.
- 2026-11-13: fix/w49-cell2: one cause found and fixed (the partner's view centre in its own world scale); the
  down-change loss round the partner remains in some runs: open.
- 2026-11-13: fix/w49-cell3: the remaining down-change loss measured with the areas apart: the test's guest had stood
  next to the host (its partner area was the band round the host's box); apart, no excess beyond noise in 20 runs, the
  rest is the game's own whole-tile cull. Test asserts the shares; fixed (the co-op cause: cell.view_centre_scale).
- 2026-11-14: released in 0.7.14.
