---
id: BUG-0338
title: Cell guest: fault at 0x00E4E384 (advect grid query, garbage output pointer) after the host packed 1100 cells in one column
status: open
kind: crash
stage: Cell
reported: 2026-10-31
source: lab 20261031-075645-fix-w20-cell cell_sap#1
role: guest
versions: 0.7.3.1+fix/w20-cell
fixed_in:
branch:
commits:
tests:
signature: dc04aee5:0x00E4E384 (FUN_00e4e320+0x64, symbolised j_mem_free+0x724) under Simulator::Cell::SimUpdate+0xCA
docs:
related: BUG-0337
---

## Report
Found while testing BUG-0337 (`coop_cell_sap.py`, fix on, lab 20261031-075645-fix-w20-cell
cell_sap#1, lane 6): the host put 1100 of its cells on one 40 m line next to its own cell
(`coop.pool column 1100`); Bob (a guest 0-50 m from the host, mirroring the pack) faulted 161
times at the same place, `exception 0xC0000005 at 0x00E4E384 (data 0x0000010D)`, esp=0012D2B0,
EDI=FFED2E01, ESI=FFFFFFB4, calls `00E4DDD4 00E4EBED 00E59EFF 00E50917 00E7F46A` (SimUpdate+0xCA).
The second run of the same test had no fault; never seen in any other lab log.
How to reproduce: `SPORE_LANE=N python3 tools/tests/coop_cell_sap.py` with the guests left next to
the host (the test now sends them 200 m away to keep this out of BUG-0337's test).

## Root cause
Not analysed. Observed (Ghidra, GOG 2024): 0xE4E320 copies the cells of up to 9 grid squares
(FUN_00e4e210 fills an int[9] on its stack) of the 20 x 15 advect grid around the camera
(0xE507F0, 0xE4DDC0, 8 per square) into two output arrays; the fault is the store to the second
output array, whose pointer (0x10D) is garbage: the caller's frame was overwritten. Inferred: an
overrun of one of these stack arrays when many cells stand in one grid square.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-31: opened.
