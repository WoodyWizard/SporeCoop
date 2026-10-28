---
id: BUG-0093
title: coop.cell spawn reads outside the cell pool for a slot number that does not exist
status: fixed
kind: tooling
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#71
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-tools
commits: 8f08a4a
tests: tools/tests/coop_cell_spawn_guard.py (lab cell_spawn_guard); tests/cell_tests.cpp spawn_slot (make test)
signature:
docs:
related: #71
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/71 (full text, discussion).

The debug command `coop.cell spawn <slot>` takes the slot from `atoi` and indexes the cell pool with it, with no range or liveness check. A slot above the pool size, a negative number or a free slot makes it read a model pointer and a scale from outside the pool, or from a dead cell, and pass them to `CreateCellObject`. Severity: minor (a debug command, but it crashes the game it is typed into).

### Where
https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/debug.cpp#L83-L93

```cpp
uint32_t slot = (uint32_t)atoi(argv[2]);
uint8_t* src = data + slot * 0x398;          // no check against n or the slot's index
... *(void**)(src + 0x108), *(int*)(src + 0x358) ...
```

### How to reproduce
In the Cell stage: `coop.cell spawn 99999`, or `coop.cell spawn -1`. Expect an access violation (in SporeKit's fault handler, or a game crash inside CreateCellObject).

### Expected
"no such cell" for a slot that is out of range or not a live cell.

## Root cause
**What happens**

1. `list` above checks `i <= n && i < 4096` and that `(*(uint32_t*)c & 0xFFFF) == i` (a live cell).
2. `spawn` does neither, so `coop.cell spawn 99999` reads `data + 99999 * 0x398` (about 92 MB past the pool) and passes the read value as a resource pointer to `CreateCellObject`.

### Confidence
CONFIRMED by reading.

## Fix
`cell_logic::parse_cell_slot` (portable, unit-tested): a decimal number without sign or trailing
text, in [1, highWater) and below 4096. `coop.cell spawn` then checks that the slot holds a live
cell (index low 16 bits == slot) and that the avatar slot is live, else "no such cell". `coop.cell
list` stops below highWater (it read slot == highWater, the BUG-0083 off-by-one).

## Verification
`coop_cell_spawn_guard.py` (lab `cell_spawn_guard`, lane 3): spawn 99999, -1, abc, 0 and a slot
past the live cells must answer "no such cell" with the game running and no fault; a live slot
still spawns.
- FAIL on main's debug.cpp: out/lab/20261028-030358 (the console stops answering at
  `coop.cell spawn 99999`; the host log has `exception 0xC0000005 at 0x74F9A1FF coop.i5.1.dll+0x7A1FF`).
- PASS with the fix: out/lab/20261028-030258 (and the final run of the branch).
Unit: `tests/cell_tests.cpp` spawn_slot (make test).

### Related
- Other debug commands with unchecked input: `coop.npc goherd <hex>` checks the pointer, `coop.cell spawn` does not.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #71.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-tools (wave 4, tests and tooling).
- 2026-10-28: released in 0.7.3.0.
