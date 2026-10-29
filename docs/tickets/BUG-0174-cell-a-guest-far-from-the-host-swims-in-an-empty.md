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
Observed: Spore's Cell stage is single-player: PopulateTiles (0xE7D370) fills and keeps only the
tiles around the player's own view (`G+0x4128`) and culls cells outside it (0xE5CDE0 / 0xE78C00,
docs/re/cell.md#cell-new-players-food). In co-op the host's game is the one world: it spawns and
simulates the cells, the guests show mirrors of them, and the guest's own population is off (so
the players do not get different food). Away from the host's view nothing exists at all, on either
game: on GOG 2024 the guest's screen has no food from 23-45 m from the host (fix/w7-eat, 2026-10-29).
Player impact: two new players who swim apart get "only one of us can eat" (BUG-0279).

## Fix
Not fixed yet. Plan (owner asked to keep it for a later wave, 2026-10-29):
1. Preferred: the host populates and keeps tiles around every partner too. Find how PopulateTiles
   picks its centre and the active tile set (`G+0x4128`, the tile list), call it for each guest's
   position (or keep a second tile set per partner), stop the cull from removing cells near a
   partner, and let cellsync's host_tick send each guest the cells of its own area (today: within
   120 m of the host's avatar). Watch: the pool size cap (pool=~420 cells), the host's frame time,
   and the bandwidth per guest.
2. Fallback: a guest far from the host (no host cells around it) lets its own game populate locally
   (food only, or food and NPCs), marked as local-only; when the host's area overlaps again, the
   local objects are removed before the mirrors arrive. Simpler, but the two players then see
   different objects for a while (a desync by design; eating local food must still count into the
   shared food total).
Test: `coop_cell_new_game.py --apart 70` (players 70 m apart) must find food within one screen of
the guest and count the guest's meals on the host; a census/sync check that the two games agree
where both have objects. Switch: `coop.fix cell.partner_tiles off` (or the fallback's name).

## Verification

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w3-cell (wave 3) checked it: still as described (cellsync.cpp host_tick sends cells
  within 120 m of the host's avatar; the guest's game populates nothing). Left open: a fix is a
  redesign of the host's pool (PopulateTiles 0xE7D370 and the active tiles follow `G+0x4128`, the
  host's view; the host would have to populate and keep tiles around every partner and send each
  guest its own area), not a bug fix of this wave.
- 2026-10-29: fix/w7-eat: measured on GOG 2024 (level 2): no food on the guest's screen from 23-45 m
  from the host (not ~100 m); likely what two new players reported (BUG-0279). Measure with
  `coop_cell_new_game.py --apart 70`; PopulateTiles and the cull: docs/re/cell.md#cell-new-players-food.
- 2026-10-29: root cause and the fix plan written down for a later wave (owner's request).
