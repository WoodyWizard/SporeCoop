---
id: BUG-0174
title: Cell: a guest far from the host swims in an empty pool
status: fixed
kind: bug
stage: Cell
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in: 0.7.3.2
branch: fix/w9-cell-food
commits: f9f1861, 5e77f1b, 1b61757, 8b1a7b5, 1511d0b, ebf5ee2, a41caec, 12366b7, 7f76bd5, ac4a302, ab5f584, 3914f6d, 924507c, dc71981, 4241133, 6f9d26d, 6b23ebd, de617d3, c4b73fd, 1b46016, 21dad8b
tests: cell_far_guest
signature:
docs: docs/re/cell.md#cell-partner-tiles, docs/known-issues.md, docs/06-testing.md
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
Done (fix/w9-cell-food, the preferred plan 1, switch `coop.fix cell.partner_tiles`): on the host,
cellsync.cpp hooks PopulateTiles 0xE7D370, PopulateTile 0xE7C6C0 and IsTileActive 0xE5F810. After the
game's own pass, one more PopulateTiles call per partner with a tile list of ours and the view boxes
(0x16B3C88) moved onto the partner's cell (no background tiles); a tile two lists share is populated
once; a gameplay tile or cell inside any player's box is kept; our lists' deletes are our own code
(the game's erase is bound to the host's list, which crashed the first run), with a guard for the
tile pool's 512 slots. The players' positions are the host's "view centres" (`view_centres.h`, at most
8, reusable by other stages and by Rivals in 0.8). Each guest gets the cells within 120 m of its own
cell (before: of the host's). Details: docs/re/cell.md#cell-partner-tiles.

Original plan (owner asked to keep it for a later wave, 2026-10-29):
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
Test `cell_far_guest` (`coop_cell_new_game.py --diet carn --minutes 0 --probe 0 --apart 70 --far-probe 4`:
a new game from the wizard, the guest holds W away from the host for 70 s, then eats food of its diet
where it is), lane 2, final code (6fe1871):
* FAIL with `SPORE_FIX_OFF=cell.partner_tiles`: lab 20261030-015715: 0 food on the guest's screen at
  every sample past 50 m (to 172 m), 0/4 eaten. (Also lab 20261030-004115 on an earlier build.)
* PASS twice: lab 20261030-015012 (`--repeat 2`): 15-28 food cells on the guest's screen at every
  sample, at 131-151 m 12/12 of its food are cells of the host's world, 4/4 eaten by its own game and
  applied on the host, no faults; tile pool 60-71/512. (Earlier builds: 20261030-003727, -004501.)
* Related tests on lane 2: cell_coop, cell_meat, cell_sync OK (20261030-004900), cell_evolve_poll
  (`coop.pool food 1000`: the level-change rescale) and cell_leave OK (20261030-012925),
  cell_new_game OK twice (20261030-013749); final build: cell_new_game, cell_evolve_poll, cell_coop,
  cell_meat OK (20261030-020044). One cell_new_game run
  (20261030-012925) had a guest NaN position and crash, BUG-0292 (not reproduced since).
After the merge with main (de617d3: one view centres module with BUG-0280's `views`, the partners
from its registry; 1b46016: shared tiles hand over after a level change), lane 2:
* lab 20261030-021422: cell_far_guest, cell_sync, cell_evolve_poll, cell_coop, cell_meat, cell_leave,
  npc_far, tribe_far all OK (8/8).
* lab 20261030-023719 (`--repeat 3`): cell_far_guest 3/3 OK, cell_evolve_poll 3/3 OK, cell_new_game
  1/3 (the guest's meat probe 2/6 and 0/6; the same probe failed with the fix off too, lab
  20261030-030946 #3: BUG-0293). No fault in any run.

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
- 2026-10-30: fix/w9-cell-food: fixed (host populates around every partner, `cell.partner_tiles`); two
  crashes on the way recorded in docs/re/cell.md#cell-partner-tiles (the erase bound to the host's
  list, the full tile pool, the world scale step refilling the host's list from every tile).
- 2026-11-01: released in 0.7.3.2.
