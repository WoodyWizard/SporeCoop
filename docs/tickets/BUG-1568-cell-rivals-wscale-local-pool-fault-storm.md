---
id: BUG-1568
title: Cell Rivals: local-pool wscale fallback triggers 0x00E4E384 fault storm on guest in Cell->Creature advance
status: open
kind: crash
stage: Cell
reported: 2026-11-25
source: 0.8.1 4-player campaign_rivals (20261125-182926)
role: guest
versions: 0.8.1
fixed_in:
branch: w081/fix-cell-rivals-fault
commits:
tests: cell_rivals_wscale_fault, cell_rivals_wscale_fault_off
signature: 0x00E4E384
docs: docs/re/cell.md
related: BUG-1546, BUG-1552, BUG-1536
---

## Report
During the 4-player `campaign_rivals` run (`out/lab/20261125-182926`) on `rel/0.8.1`:
- Bob's guest Cell hit `cell_rivals: wscale diverged (host=4.722 own=0.5 ratio=9.445) -> guest plays local pool to catch up (BUG-1552)`.
- Within 20 seconds, Bob's cell coordinates drifted from `(60, 217)` to `(5590.6, 7036.3)` at `wscale=0.5` and hit an 801-fault storm at `0x00E4E384` (`j_mem_free+0x724`, read of `0x00000112`, called from `Cell_LocksReset+0x7F` < `PreloadLootTableResource+0xD17` < `Simulator::Cell::SimUpdate+0xCA`), plus 322 more faults during `creature/follow`.

## Root cause
Pending disassembly of `0x00E4E384` and inspection of `mods/coop/cellsync_guest.cpp` (`drop_mirrors_for_scale_solo` / `solo_start`), `mods/coop/cellsync_tiles.cpp`, and `mods/coop/cell_rivals.cpp`.

## Fix
Pending implementation and Wine lab verification.

## Verification
Pending Wine lab ON PASS + OFF FAIL verification.
