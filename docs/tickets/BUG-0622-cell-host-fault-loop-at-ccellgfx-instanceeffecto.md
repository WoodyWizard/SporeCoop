---
id: BUG-0622
title: Cell: host fault loop at cCellGFX::InstanceEffectOnCell+0xE03 while loading Cadian.spo (38169 faults, cell_gone_play on rel/0.7.9)
status: open
kind: crash
stage: Cell
reported: 2026-11-06
source: lab rel/0.7.9 out/lab/20261106-110218 cell_gone_play#1 (run by L4)
role: 
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
In rel/0.7.9 1e155b53, lab 20261106-110218 cell_gone_play#1 (lane 7, under heavy load: both games ready after 512 s), the host faulted at 11:02:39.825, 0.04 s after `stage: Galaxy menu -> Loading 9652F8AD 'Cadian.spo'`: `exception 0xC0000005 at 0x00E67643 Simulator::Cell::cCellGFX::InstanceEffectOnCell+0xE03 (data 0x11990004) tid=340`, then `0x00E4DDF0 j_mem_free+0x190`, 38169 faults in the log in all. Runs #2 and #3 of the same test did not fault. Not analysed. Signature not seen before (tickets.py find InstanceEffectOnCell: none).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened.
