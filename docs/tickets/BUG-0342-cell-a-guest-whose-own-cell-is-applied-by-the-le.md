---
id: BUG-0342
title: Cell: a guest whose own cell is applied by the level rebuild at the world load is a ghost (cannot eat, bite or be hit)
status: fixed
kind: bug
stage: Cell
reported: 2026-10-31
source: player reports 2026-10-31 (host and guest marks)
role: guest
versions: 0.7.3.1
fixed_in: 0.7.3.2
branch: fix/w25-reports
commits: 2a4420a c8f91ff 3974bf8
tests: cell_own_load (tools/tests/coop_cell_own_load.py)
signature:
docs: docs/re/cell.md#cell-ghost-puppet-address, docs/re/creations.md#creations-own-cell-rebuild-timing
switch: cell.own_rebuild cell.puppet_index
related: BUG-0345, BUG-0341
---

## Report
Host on GOG 2024, guest on Steam (both Windows, 0.7.3.1), a new world in Cell. Marks of both
players in the same session: the host "the guest got its own cell from another world and is
basically a ghost which cannot interact with anything" (about 5 min after the load), later "my
friend is a ghost"; the guest "I was a ghost (go through everything, can't eat, can't attack or
get attacked)".

## Root cause
CORRECTION (2026-10-31, fix/w25-reports): the cause, observed (docs/re/cell.md#cell-ghost-puppet-address).
h_Pair (cellsync.cpp) drops every collision pair with a partner's puppet, and `modes::cell_is_puppet`
asked only a map keyed by the cell's address. The own cell's level rebuild (RebuildLevel 0xE7FD00)
frees every cell and makes the avatar first, often at the address the puppet had; the puppet is made
again elsewhere and its old entry is never looked at again. Our own cell was then "a puppet": the game
handled none of its collision pairs (`own_pairs` 0, good runs 15-70 per plant) although the broadphase
held them and Collisions' tests passed (`coop.pool collide`: `puppet=1` for our cell in both ghost
runs of 12 with the old check). The timing findings below (first frames of a load vs 5 s later) only
changed how often the avatar landed on the old address.

Earlier notes (kept for the record):

## Fix
* `cell.puppet_index` (modes.cpp, the cause): a puppet is the cell at the address AND its pool index
  (`cell_at`: index and generation); entries whose index no longer leads to their address are dropped
  whenever a puppet is made.
* `cell.own_rebuild` (earlier, fix/w22-reports): the own cell's rebuild waits until the loaded Cell
  world ran 5 s and 150 frames (kept: it made the reload case rare before the cause was known).

## Verification
`tools/tests/coop_cell_own_load.py` (suite `cell_own_load`), the first-load check is a failing check now
(the guest's own game eats 2 of 3 plants it swims into, AvatarEat, the plant gone on both machines):
* FAIL with `SPORE_FIX_OFF=cell.puppet_index`: lab 20261031-145028 (#2 of 4: 0 of 3 plants); direct
  runs out/w25/off*_*.txt of the worktree: 2 of 12 ghosts, `collide: ... puppet=1` in both.
* PASS: lab 20261031-145439 (6 of 6 across lanes 5 and 6) and 20261031-144455; direct runs 6 of 6
  (out/w25/on*_*.txt).
* Cell regression, lab 20261031-144455 on lanes 5/6: cell_coop, cell_own_load, cell_reload_join,
  creations_cell, cell_pause OK; cell_bite_ring failed once at bite 2 (`skip=gfx`: the creature had
  just lost its body off screen, the test's timing, not this change) and passed 2 of 2 in lab
  20261031-144913.

## History
- 2026-10-31: opened from the host's and guest's report zips (marks, cell trace lines).
- 2026-10-31: CORRECTION recorded; not reproduced on lanes 5/6 (coop_cell_own_load.py passes on main); the reload part is BUG-0349.
- 2026-10-31: reload case reproduced and fixed (cell.own_rebuild); first-load case reproduced in 2 of 4 runs, still open.
- 2026-10-31 (fix/w23-reports): first-load case measured, cause still open. `coop_cell_own_load.py`
  with diagnostics, lanes 5/6, 34 runs: the guest did not eat after the first load in 12 of 34
  (0 of 3 plants; batches of 3 per lane from 0/3 to 3/3), after the reload in 1 of 34 (with
  cell.own_rebuild on: the reload fix is not complete either). In the
  failing runs, NOT the cause (observed): the cell model data (16 parts, radius 1.259 like the good
  runs), the broadphase (no pending, nothing removed or unsorted, pairs 60-120 of 4096, the avatar's
  object id 0 with its own four endpoints and bounds around its position, the plant's bounds inside
  the avatar's on both axes), the avatar's scale (+0x58 0.55) and rotation matrix (+0x5C, finite),
  flags +0x48 = 6 as in good runs, the level (-1 / plants 0, scale_diff 2 like the partner).
  Seen: `coop.pool` own_pairs (HandleCollisionPair calls with our cell, hook h_Pair) stayed 0 for 30
  frames while the plant overlapped; not checked whether own_pairs is non-zero in good runs, so it
  does not yet tell the broadphase pair from the narrow test in Collisions 0xE7F260 (GetIfNotDeleted
  of both, then the bounding spheres from 0xE6D790). The mirrors-in-pool lead (pool 340-700 at the
  rebuild in good and bad runs alike) does not separate the cases. Tools for the next step:
  `coop.pool sapme [c<key>]`, `own_pairs=` in `coop.pool`, `pairs=` / `pairs_full=` in `coop.pool sap`.
- 2026-10-31: cause found and fixed on fix/w25-reports (cell.puppet_index).
- 2026-11-01: released in 0.7.3.2.
