---
id: BUG-0083
title: modes::cell_at accepts one slot past the pool's used range (off by one)
status: fixed
kind: bug
stage: Cell
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#61
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-cell
commits: e7ab004, 595e12f, b6039e6, 55b3c2a
tests: tests/cell_tests.cpp slots() (make test)
signature:
docs: docs/re/cell.md#cell-sync-robustness
related: #61
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/61 (full text, discussion).

`modes::cell_at` checks a pool index against the pool's high-water mark with `>` instead of `>=`, so it accepts the slot just past the last slot ever used and reads its first 4 bytes. When the pool is full up to its capacity, that read is past the end of the pool's memory. The index can come straight from the network. Severity: minor (an out-of-bounds read; a crash only if the allocation ends at a page boundary).

### Where
- [mods/coop/modes.cpp#L239-L251](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/modes.cpp#L239-L251), the check at L247:
  ```cpp
  if (!data || size != 0x398 || slot > *(uint32_t*)(g + 0x1C + 0x10))
      return nullptr;
  uint8_t* c = data + (size_t)slot * size;
  return *(uint32_t*)c == idx ? c : nullptr;
  ```
- Pool layout (`+0x10` high-water mark, `+0x18` capacity, live slots `[0, highWater)`): [docs/re/cell.md#L11-L14](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/cell.md#L11-L14), [docs/re/cell.md#L29](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/cell.md#L29)
- The mod's own iteration uses `<`: [mods/coop/cellsync.cpp#L70](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L70)
- Callers with indices from the guest (host side): [mods/coop/cellsync.cpp#L1240](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1240), [L1265](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1265), [L1288](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1288)

### How to reproduce
By reading. In a debugger: on the host in the Cell stage, send `MSG_CELL_HIT` with index `slot = *(u32*)(G + 0x2C)` (high-water mark), generation 0. `cell_at` dereferences the slot past the used range. Making it fault needs a pool whose high-water mark equals its capacity and whose allocation ends at a page boundary.

### Expected
`cell_at` never reads outside `[0, highWater)`, and also never outside `[0, capacity)`.

## Root cause
**What happens**

1. The cell pool `G+0x1C` holds `highWater` slots that were ever used (`+0x10`), inside an allocation of `capacity` slots (`+0x18`). Valid slots are `0 .. highWater - 1`.
2. `cell_at` returns null only for `slot > highWater`, so `slot == highWater` passes and `*(u32*)(data + highWater * 0x398)` is read.
3. When `highWater == capacity`, that address is one object past the end of the pool's allocation. The heap usually has something readable there, so the result is almost always "no match". But it is a read of memory the mod does not own, and it faults if the allocation ends at the end of a committed page.
4. The index reaches `cell_at` from `MSG_CELL_HIT`, `MSG_CELL_BITTEN` and `MSG_CELL_EAT` (host), from the guest's mirror map, and from debug commands. A guest that sends `highWater` as a slot (a stale or corrupt index, or a modified peer) triggers the read on the host.

### Confidence
CONFIRMED by reading (the comparison and the documented pool layout). Not seen as a crash.

## Fix
Fixed on fix/w2-cell (e7ab004): `modes::cell_at` accepts a slot only in `[0, highWater)` and below the
capacity `+0x18` (`cell_logic::slot_in_pool`). `coop.pool fix cellat 0` restores the old bound.

The review suggested:

```cpp
uint32_t hw = *(uint32_t*)(g + 0x1C + 0x10), cap = *(uint32_t*)(g + 0x1C + 0x18);
if (!data || size != 0x398 || slot >= hw || slot >= cap)
    return nullptr;
```
No switch needed: it only rejects indices that can never be a live cell.

## Verification
Unit test `slots()` in tests/cell_tests.cpp (make test): slot == highWater, a full pool and a
highWater above the capacity are rejected with the fix and accepted by the old bound (the same
function with the fix off). Not reproduced in the game (the forged index needs a slot past the
used range holding a matching value); the area tests (cell_coop, cell_meat, cell_spin, cell_leave,
cell_sync, cell_net) pass with the new bound.

The planned test was:

A unit-level check is enough here. Add a `coop.pool at <index>` debug command that calls `cell_at`, and a case in `tools/tests/coop_cell.py`: with the host in the Cell stage, ask for the index `highWater | (gen << 16)`, with `gen` taken from the slot's memory so the compare would match if read. Assert the answer is "no cell". Today it reads the slot, so with a forged generation it can return a pointer; with the fix it returns null.

### Related
- BUG-0006 (`cell_at` reading a freed pool after the Cell stage): [docs/re/cell.md#L529](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/re/cell.md#L529)

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #61.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-cell (bug wave 2); test FAIL without the fix, PASS with it.
- 2026-10-28: released in 0.7.3.0.
