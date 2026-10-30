---
id: BUG-0296
title: Cell, 4 players: guest crash in DestroyCell called by coop (dc04aee5:SporeApp.exe+0x681FE6, cCellGFX::AddPreloadedTexture+0x134)
status: open
kind: crash
stage: Cell
reported: 2026-10-30
source: lab 20261030-035318 campaign#3 i37
role: 
versions: main 1d51511
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
