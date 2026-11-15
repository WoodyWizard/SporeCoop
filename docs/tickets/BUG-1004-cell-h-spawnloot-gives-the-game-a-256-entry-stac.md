---
id: BUG-1004
title: Cell: h_SpawnLoot gives the game a 256-entry stack list it fills without a bound
status: fixed
kind: crash
stage: Cell
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-18
role: host
versions: 
fixed_in: 0.7.14
branch: fix/w49-cell
commits: 23b91609, bb9e3b4a, cbd1da51
tests: cell_loot_big
signature:
docs: docs/re/cell.md#cell-loot-alert, docs/06-testing.md#testing-cell-c1-0714
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
cellsync.cpp h_SpawnLoot: the list given to the game is a static one of 4096 entries (the cell pool's size; the game's
recursion passes it on, never asks the hook again), read back up to that bound. Switch `coop.fix cell.loot_list off`
(unsafe: the old 256-entry stack array). Test command `coop.pool lootbig <n>` (host: the next foreground drop's table
makes n cells, or its repeat entry repeats n times). RE of the table: docs/re/cell.md#cell-loot-alert.

## Verification
Test `cell_loot_big` (one drop of the fixture's break table repeated 300 times: 600 cells).
FAIL (SPORE_FIX_OFF=cell.loot_list): out/lab/20261113-023529 (a fault at 0x8329032C on the host: the return address
overwritten by cell indices).
PASS: out/lab/20261113-023438-2, out/lab/20261113-023819.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-cell (package C1).
- 2026-11-14: released in 0.7.14.
