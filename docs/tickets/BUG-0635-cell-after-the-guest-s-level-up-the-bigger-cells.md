---
id: BUG-0635
title: Cell: after the guest's level-up the bigger cells near it vanish and others appear (mirrors freed by the rebuild, made again)
status: fixed
kind: desync
stage: Cell
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-02, M0ng0r-03
role: guest
versions: 0.7.8
fixed_in: unreleased
branch: fix/w46-cell-p0
commits: 47969057 1180b8f8
tests: cell_levelup_view (coop_cell_levelup_view.py)
signature:
docs: docs/re/cell.md#cell-levelup-view
related: BUG-0604, BUG-0511, BUG-0519
switch: cell.levelup_keep
---

## Report
Discord report 2026-11-06 (0.7.8), discord-2026-11-06/M0ng0r-02 and M0ng0r-03 (the guest, mark at s 438): "after
growing, bigger cells near the guest disappear and new ones spawn".

## Root cause
Not BUG-0604 (fixed unreleased 0.7.9): that one is plain water (`screen: creatures=0` for minutes from a NaN camera).
Here the guest's screen keeps cells after its level-up at s 408.1 (`creatures=10`, then 5, then 2 at s 410 / 420 / 430)
and no NaN line appears in the log.
Observed (the guest's log): `[s 408.054] tiles: ... level 8; cell pool 237/4096` (442 at s 398), then the pool and the
mirrors churn (466/467 -> 473/475 -> 423/429). The guest levels up on its own each time (levels 6..10 at s 277, 368,
408, 528, 578).
Inferred: the guest's level change (RemoveCellsOfOldScale / RebuildLevel) frees the big mirrors nearby and the host's
next state makes them again at other places or sizes: cells vanish and reappear. Related: BUG-0511 (host scale in
MSG_CELL_STATE, fixed 0.7.4), BUG-0519 (puppet scale, fixed 0.7.5).

## Fix
Observed cause (Ghidra + lab, docs/re/cell.md#cell-levelup-view): a level change that moves the world scale tier
runs RemoveCellsOfOldScale 0xE7CD10 in each game, which destroys every cell outside THAT game's visible box
(0xE5CDE0) and populates the tiles again. The guest's game keeps its mirrors (h_Destroy, not touched), but the
host's wipe destroyed the host's cells on the guest's screen (82-163 per wipe), and the next state made other
cells there: the guest lost its whole screen on every change, a single player (the host's own view) keeps most of
it on a level down. Not the rebuild of BUG-0604 / 0511 / 0519.

`cell.levelup_keep` (cellsync.cpp h_Wipe / h_Destroy, host): inside the host's wipe a cell whose sphere touches a
partner's visible box (+-15 x +-11 around the partner's cell, the box +-10 x +-7.5 with a margin for its camera) is
kept, as the game keeps the host's own view. `coop.fix cell.levelup_keep off`: 0.7.9. A level-up still replaces
most of the screen, in both games alike (the cells of the old tier leave in a single player's game too).

## Verification
* FAIL (`SPORE_FIX_OFF=cell.levelup_keep`): out/lab/20261106-220609: the guest lost 100% of the cells on its screen
  on each of 6 changes (host 0-36% on a level down, ~90% up; average 100% vs 42%).
* PASS: out/lab/20261106-220831 (guest 60%, host 52%; 121-170 cells kept per wipe), 20261106-221058 (29% vs 24%).
* Earlier runs with one-level steps (no wipe at all) and an on-screen measure: 20261106-215819, -220056, -220333
  (the test was changed to three-level steps and to cells removed from each game's world).

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-06: fixed in fix/w46-cell-p0 (cell.levelup_keep): the host's level-change wipe keeps the cells on a partner's screen; FAIL 20261106-220609, PASS 20261106-220831, -221058.
