---
id: BUG-1066
title: Cell: after a level change the host's tile pool holds 15-16 live tiles that no tile list names (the tile check's 20 warnings used up in two changes)
status: open
kind: bug
stage: Cell
reported: 2026-11-13
source: fix/w49-cell labs 20261113-024334, 20261113-025004-2 (cell_flush_handover)
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
Not analysed. Inferred: the tiles the level change's rescale 0xE6DB70 put into the host's list and h_PopTiles took
out again as ours (`g_tiles_unshared`) when they belong to a partner list that the flush then rebuilt, or tiles a
hand-over populated in a list deleted in the same pass (BUG-1000 part 2), left in the pool.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
