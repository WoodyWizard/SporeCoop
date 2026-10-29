---
id: BUG-0174
title: Cell: a guest far from the host swims in an empty pool
status: open
kind: bug
stage: Cell
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0279
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* The host simulates and sends only the cells around its own cell (its active tiles, docs/re/cell.md
  5; `host_tick` sends cells within 120 m of the host's avatar), the guest spawns none of its own. A
  guest more than ~100 m away meets no NPCs and no food until it comes back (play_cell seed 9,
  2026-10-23: partner 211 m away at the end, the guest's 538 local cells were all around the host;
  the pair was over 60 m apart half the session). Not unfair in itself (no NPC there to hunt either
  player), but the guest eats less there. A fix needs the host to populate and keep tiles around
  each partner (PopulateTiles 0xE7D370 around `G+0x4128` is the host's view only).
* Reproduce: `SPORE_LANE=N python3 tools/tests/play_cell.py --minutes 10 --seed 9` and watch
  `partner` distances and the guest's `local_gameplay_cells` vs where its cell is.

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w3-cell (wave 3) checked it: still as described (cellsync.cpp host_tick sends cells
  within 120 m of the host's avatar; the guest's game populates nothing). Left open: a fix is a
  redesign of the host's pool (PopulateTiles 0xE7D370 and the active tiles follow `G+0x4128`, the
  host's view; the host would have to populate and keep tiles around every partner and send each
  guest its own area), not a bug fix of this wave.
- 2026-10-29: fix/w7-eat: measured on GOG 2024 (level 2): no food on the guest's screen from ~45 m
  from the host (not ~100 m); likely what two new players reported (BUG-0279). Measure with
  `coop_cell_new_game.py --apart 70`; PopulateTiles and the cull: docs/re/cell.md#cell-new-players-food.
