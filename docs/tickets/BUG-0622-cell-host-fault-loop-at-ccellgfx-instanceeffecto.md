---
id: BUG-0622
title: Cell: host fault loop at cCellGFX::InstanceEffectOnCell+0xE03 while loading Cadian.spo (38169 faults, cell_gone_play on rel/0.7.9)
status: not-reproduced
kind: crash
stage: Cell
reported: 2026-11-06
source: lab rel/0.7.9 out/lab/20261106-110218 cell_gone_play#1 (run by L4)
role: host
versions: 0.7.9 1e155b53
fixed_in:
branch:
commits:
tests:
signature: 0xC0000005 at 0x00E67643 (Cell mode entry effect preload, 0xE67610) then 0x00E4DDF0
docs: docs/re/cell.md#cell-preload-fault
related:
---

## Report
In rel/0.7.9 1e155b53, lab 20261106-110218 cell_gone_play#1 (lane 7, under heavy load: both games ready after 512 s), the host faulted at 11:02:39.825, 0.04 s after `stage: Galaxy menu -> Loading 9652F8AD 'Cadian.spo'`: `exception 0xC0000005 at 0x00E67643 Simulator::Cell::cCellGFX::InstanceEffectOnCell+0xE03 (data 0x11990004) tid=340`, then `0x00E4DDF0 j_mem_free+0x190`, 38169 faults in the log in all. Runs #2 and #3 of the same test did not fault. Not analysed. Signature not seen before (tickets.py find InstanceEffectOnCell: none).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (disassembly GOG 2024, docs/re/cell.md#cell-preload-fault): the faulting loop is game code of the Cell
mode's entry, 0xE67610 cdecl(key): `Cell::GetData(key)` returns a list {count, items (inline from +8), stride
0x1C}, each item's effect goes to `cCellGFX::AddPreloadedEffect` (0xE66280); the mode init calls it for
`Cell::GetGlobalsData()+0x3C` and `+0x40`, the fault was in the second (return 0xE81AFC). Registers: list esi
0x1198FEC8, items edx = esi + 8, index edi 11 (ebx = 11 x 0x1C): the read 0x11990004 is the first byte past a
page, the count said more than 11 items, the block ended there. The next 38168 faults (the game's own handler
continued; the Cell world never came up: "pool block 00000000 unmapped while the mode reads Cell") follow from it.

Which 0.7.9 code ran before the fault (observed in the log, 12 s after boot, the first load of the process): no
co-op session (it started 4 minutes later), so cell.camera_heal (a session tick) and everything of cellsync did not
run; the effects relay (world_xform) counted 0 calls for the whole process ("effects hooks ... 0 calls"); BUG-0307's
contact hooks were not in that tree (1e155b53); X3's int3 gate (sporekit/hooks.cpp, kit.fix hookfirst) only acts on
a hook made after the first frame, and none was: the lab's cold start sends no key before the load (the input
hooks are made at the first injected key, twoinst.py sends `key enter` only after the guest joined) and the mod's
only late hooks (modes.cpp ensure_model_hook: Cell::SimUpdate, Cell::GetModelKeyForCellResource) are made at the
first mirror or puppet cell, i.e. in a session; the call chain (0xE81AFC < 0xE81CBF < 0x8856A0 < 0x75DB01 < 0xEF2250 < 0x92D211) is all
game code, no detour in it. Inferred: a game-side stale or torn Cell data list under the extreme load of that run
(both games ready only after 512 s; "perf: hitch 5017 ms" at boot), not a 0.7.9 change.

## Fix
No change: no cause found in the mod; the fault is in the game's own resource data at the Cell mode entry, before
any co-op code of 0.7.9 had run. A guard on 0xE67610 (skip the preload when the list's items do not fit its block)
was not added: no evidence of the list's real size, and the data was already bad (heap faults followed).

## Verification
no-fail-run: not reproducible: 10 cold first Cell loads from the galaxy menu on rel/0.7.9 + BUG-0307 (828055b8 and
fix/w45-int3) without a fault: cell_gone_play x4 cold (lab 20261106-123649, lane 3), cell_gone_play x3 + cell_coop x3
cold (lab 20261106-130235, lane 2, games ready after 49-66 s while lane 3 ran play_civ), cell_coop x3 cold (INT3
report). The only sighting: 20261106-110218 #1.

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-int3): analysed (game code, no 0.7.9 code on the path), not reproduced in 10 cold loads; not-reproduced.
