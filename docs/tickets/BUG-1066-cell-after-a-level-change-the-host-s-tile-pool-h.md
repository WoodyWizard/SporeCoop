---
id: BUG-1066
title: Cell: after a level change the host's tile pool holds 15-16 live tiles that no tile list names (the tile check's 20 warnings used up in two changes)
status: fixed
kind: bug
stage: Cell
reported: 2026-11-13
source: fix/w49-cell labs 20261113-024334, 20261113-025004-2 (cell_flush_handover)
role: host
versions: 
fixed_in: unreleased
branch: fix/w49-cell2
commits: d7f41e27, 58c4aad0
tests: cell_levelup_area, cell_flush_handover
signature:
docs: docs/re/cell.md#cell-levelup-partner-area, docs/06-testing.md#testing-cell-c2-0714
related: BUG-1000
---

## Report
Seen in cell_flush_handover (3 players, the host with two partner lists, food +-150), with and without
`cell.flush_handover`: right at the first level change the host's tile check logs `check after the frame between
(level 2): pool live 85, list entries 70, dead entries 0`, then `our deletes: live 39, entries 24`, `the host's pass:
live 32, entries 17`: 15 (later 16) live tile objects of the pool G+0x38 are in no list (neither the host's nor a
partner's), dead entries 0. The check stops after 20 warnings (`check_warns=21` after the second change), so later
real mismatches (BUG-1000 part 1) are no longer logged. Not seen in a two-player session in these runs.
How to reproduce: `tools/lab.py run cell_flush_handover --lane N`, the host's game log.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (labs 20261113-033642 / -2, `coop.pool tilestat unshared=`, and C1's log: pool live 55, entries 40 at the
first frame, then 15 freed by our flush): `food_drain` (BUG-0633: the food of a partner or of the shared total
applied inside the step) runs in h_WorldScale right BEFORE the game's WorldScaleStep, after the check that flushes
our tiles on a moving scale. Its AddFoodPoints sets the target scale, so the very step rescales with our tiles in the
pool: 0xE6DB70 refills the host's list from every tile object (the last of a key wins), our shadow tiles of shared
keys replaced the host's own populated tiles there, h_PopTiles took ours out again (15 `unshared` per change) and
the host's own tiles of those keys stayed live in no list, never deleted (a leak of tile pool slots; RemoveCellsOfOldScale
repopulates every tile object, those too). Two-player sessions have it as well (the guest's food). Real, not only
bookkeeping.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
`cell.scale_food_flush` (cellsync.cpp h_WorldScale): the flush of our tiles before a moving-scale step runs after the
food drain. Off: before it. `coop.pool tilestat` prints `unshared=` and `scale_flushes=`.

## Verification
Test `cell_levelup_area` (its tile pool check: `coop.pool tilestat` check_warns, the host's count of the game's tile
pool G+0x38 against the lists, after six level changes).
FAIL (SPORE_FIX_OFF=cell.scale_food_flush): out/lab/20261113-071833-2 (21 mismatches, 80 of our tiles taken out of the
host's list); also out/lab/20261113-034534-2 (21, 72) and out/lab/20261113-033642-2 (unshared 15 per change).
PASS: out/lab/20261113-071833, out/lab/20261113-123921, out/lab/20261113-162634-2 (0 mismatches, 0 unshared);
cell_flush_handover (3 players, the check no longer used up) PASS out/lab/20261113-035120, out/lab/20261113-123921.

## History
- 2026-11-13: opened.
- 2026-11-13: cause found (the flush ran before the food drain that starts the rescale), fixed on fix/w49-cell2.
