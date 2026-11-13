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
fixed_in: unreleased
branch: fix/w49-cell2
commits: d7f41e27
tests: cell_levelup_area, cell_levelup_view
signature:
docs: docs/re/cell.md#cell-levelup-partner-area, docs/06-testing.md#testing-cell-c2-0714
related: BUG-0976, BUG-0635
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
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
`cell.view_centre_scale` (coop.cpp `view_pos`): in Cell the host notes a partner's view centre times partner scale /
own scale, where the partner's cell is drawn (cell_drive's BUG-0519 conversion). Off: the pose as it came.
BUG-0975's tile cap untouched (cell_level_drop PASS).

## Verification
Test `cell_levelup_area` (the guest 60 m left of and 40 m below the host, six +-150 changes; per change in the host's
game the cells of its visible box and of the same box round the partner, pooled share removed by the level change's
DestroyCell callers; the partner's view centre against its cell while the scale moves).
FAIL (SPORE_FIX_OFF=cell.view_centre_scale): out/lab/20261113-044709 (centre 17-37 m off the partner's cell while the
scale moved; down changes 52 % of the partner's area against 10 % of the host's own).
PASS: out/lab/20261113-044709-2 (centre 0.7-2.5 m; down 11 % / 20 %, up 55 % / 90 %), out/lab/PASS2.
Earlier runs (out/lab/20261113-034143, -034534, -035120, -041224*) had the guest 5 m from the host (`tp 200 0` was
clamped at the level's edge): the two areas were one, their shares are not evidence either way.
cell_levelup_view (guest screen) and cell_level_drop (BUG-0975's cap untouched): PASS in out/lab/PASS2.

## History
- 2026-11-13: opened.
- 2026-11-13: cause found (the partner's view centre in its own world scale), fixed on fix/w49-cell2.
