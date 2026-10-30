---
id: BUG-0292
title: Cell: a guest's own cell position became NaN, then its game crashed in PopulateTile+0x8A
status: open
kind: crash
stage: Cell
reported: 2026-10-30
source: fix/w9-cell-food lab 20261030-012925 cell_new_game
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature: 0x00E7C74A PopulateTile+0x8A (GOG 2024)
docs:
related: BUG-0174
---

## Report
Seen once on fix/w9-cell-food (commit dc71981, the partner tiles of BUG-0174 on), lab 20261030-012925,
test `cell_new_game` (lane 2, GOG 2024): 45 s into the play phase the GUEST's game took a handled fault
at `PopulateTile+0x8A` (0xE7C74A, reading the tile level table with a garbage level: the tile object
was garbage, called from PopulateTiles+0x1EF), stood 2 s, and from then on its own cell's position
was NaN (`pos=(-nan -nan)` in every trace line, the host saw the puppet go and dropped its partner
tiles); 40 s later at a level change (food 108) the game crashed at the same address through
RemoveCellsOfOldScale (`crash-20261030-023236-i4`, kept in the run directory). The host had no fault.
Two reruns of the same test on the next build (lab 20261030-013749, `--repeat 2`) passed with no
fault; not seen on main or other branches (searched every lab log).

Inferred, not verified: a NaN view point makes the active tile range of 0xE5F610 garbage
((int)NaN), PopulateTiles then fills its 511-entry stack array with garbage tiles; so the tile fault
is likely a consequence of the NaN position, not its cause. The guest's tile code is unchanged by
BUG-0174 (its hooks pass through on a guest); what changed for the guest is which host cells it
mirrors (those within 120 m of its own cell). Where the NaN came from is not known.
Reproduce: `tools/lab.py run cell_new_game --lane N` (1 of 4 runs so far).


## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
- 2026-10-30: fix/w9-cell-food: seen once, not reproduced in two reruns; left open.
