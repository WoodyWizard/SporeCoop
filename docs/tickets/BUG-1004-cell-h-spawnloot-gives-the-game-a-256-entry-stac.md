---
id: BUG-1004
title: Cell: h_SpawnLoot gives the game a 256-entry stack list it fills without a bound
status: open
kind: crash
stage: Cell
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-18
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
Audit 2026-11-12 for 0.7.14, finding WLD-18. Not seen at runtime, found by code reading (main efd2e0ba). On the host
(and a solo guest) every loot drop of the foreground pool goes through `h_SpawnLoot`, which passes the game a stack
array `uint32_t buf[256]` as the "new cells" list. The game's SpawnLoot (0xE771D0) appends every cell it creates,
recursion included, with no bound (docs/re/cell.md, SpawnLoot row); a loot table that makes more than 256 cells in
one drop writes past the array on the stack.

## Root cause
Observed in code: mods/coop/cellsync.cpp 2888 `uint32_t buf[256]`, 2891 the call with `buf` / `&n`, 2898 the bound
`i < 256` only when reading back. The game's own loot tables make a few cells per drop (6 seen per kill, docs/re
cell.md BreakOrSwallow row); inferred: only a modded loot table (or a repeat entry chain) reaches 256. Severity: low
(hardening).

## Fix idea
Pass a heap buffer sized from the pool's free count, or let the hook pass no list and diff the pool's live slots
(`pool_live()` before and after, the indices from the pool walk), or a 4096-entry static buffer (the pool holds 4096
slots, so the game can never write more). No switch needed.

## How to test
Hard to make FAIL with the game's own data. A unit-style check: a test command that calls `h_SpawnLoot` with a fake
loot table entry of 300 cells (type 1, count 300) on the host in Cell; main: a stack overrun (the lab's crash check),
fixed: 300 cells listed. `no-fail-run` is acceptable if the fake table cannot be built.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
