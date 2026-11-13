---
id: BUG-1080
title: Cell: the first level down after a partner moved away culls most of the few cells round the partner in the host's game
status: open
kind: bug
stage: Cell
reported: 2026-11-13
source: fix/w49-cell2 labs 20261113-071833, -044709-2, -041224-2 (cell_levelup_area)
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Seen while fixing BUG-1064 (cell_levelup_area, lanes 2 / 3, GOG 2024, with `cell.view_centre_scale` on: the partner's
view centre within 0.8-2.5 m of its cell). The guest 60 m left of / 40 m below the host, +-150 food changes. On the
second change (the first level down) the host's game removed most of the few cells in the box round the partner: 10 of
10 (lab 20261113-071833, the cull 0xE7C3E6), 5 of 7 (lab 20261113-044709-2, 4 of them by 0xE78B6E, the room a hatching
cell clears), 16 of 17 (lab 20261113-041224-2, guest 5 m away), against 21-24 % of the host's own area. Later down
changes were even (21 / 24 %, 19 / 13 %). Pooled over the down changes 28 % against 16 % (lab 20261113-071833).
How to reproduce: `tools/lab.py run cell_levelup_area --lane N`, the per-change lines of change 2.

## Root cause
Not analysed. Inferred: the area round the partner holds few cells before the first down change (it was populated at
the up level only by the host's tiles; the partner's own list had 0 tiles in some trace lines at level 2), so most of
them are cells without a parent at the tile edge.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
