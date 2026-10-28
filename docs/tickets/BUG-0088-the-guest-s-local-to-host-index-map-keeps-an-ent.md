---
id: BUG-0088
title: the guest's local-to-host index map keeps an entry for every mirror it recreates
status: fixed
kind: bug
stage: Cell
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#66
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-cell
commits: e7ab004, 595e12f, b6039e6, 55b3c2a
tests: tools/tests/coop_cell_sync.py (suite cell_sync)
signature:
docs: docs/re/cell.md#cell-sync-robustness
related: #66
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/66 (full text, discussion).

When the guest recreates a mirror cell because the old local cell is gone, it adds the new local index to `g_host_of` but never removes the old one. The map grows for the whole Cell stage, and every hook that asks "is this cell a mirror?" looks into it. Severity: minor (memory growth, plus a rare wrong match after a 16-bit generation wrap).

### Where
- [mods/coop/cellsync.cpp#L1024-L1035](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1024-L1035):
  ```cpp
  auto& m = g_mirrors[hidx];
  ...
  uint8_t* c = m.local ? modes::cell_at(m.local) : nullptr;
  if (!c) {
      uint32_t idx = modes::cell_create(p, ref_for(inst), level, key, &q);
      c = modes::cell_at(idx);
      if (c) {
          m.local = idx;            // the old m.local is dropped here
          g_host_of[idx] = hidx;    // but g_host_of[old m.local] stays
  ```
- Users of the map: `mirror_key` [L390-L396](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L390-L396), `h_DamageCell` [L428-L437](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L428-L437), `h_Destroy` [L603-L629](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L603-L629), the stray purge [L1069-L1078](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1069-L1078)

### How to reproduce
1. Host and guest in the Cell stage, same pool, for a long session (or with packet loss, see #10).
2. Compare `g_host_of.size()` with `g_mirrors.size()` (needs a debug line in `coop.pool`; today `coop.status` prints only `mirrors=`). The difference grows with every recreated mirror (`created` in `cell(guest):` keeps rising while `mirrors=` stays flat).

### Expected
`g_host_of` holds exactly one entry per live mirror: its size equals the number of mirrors with a local cell.

## Root cause
**What happens**

1. A mirror's local cell can disappear without the mod's own removal path: the game destroys it in a path `h_Destroy` lets through, `RemoveCellsOfOldScale`, a rebuild, or a size-0 removal.
2. The next state from the host finds `cell_at(m.local) == nullptr` and creates a new local cell. `m.local` is overwritten. The entry `g_host_of[old local] = hidx` is not erased; only `forget_mirrors` (stage leave, session end) clears the map.
3. Each recreation leaves one entry. With #10 (lost state packets recreate mirrors), this is many entries per minute on a lossy link.
4. Pool indices carry a 16-bit generation, so an old entry matches a new cell only after the same slot was reused 65 536 times. Then that unrelated cell (food, a guest-local cell, the guest's own mate) would be taken for a mirror: its bites would go to the host, its destruction would be reported as eaten, and the stray purge would spare it.

### Confidence
CONFIRMED by reading. The generation-wrap mismatch is theoretical. The growth is certain but small (8 bytes plus node overhead per entry).

## Fix
Fixed on fix/w2-cell (e7ab004): before a recreated mirror's `m.local` is overwritten, its old entry
is erased from `g_host_of`. `coop.status` (guest) prints `host_of=`. Old behaviour (tests):
`coop.pool fix hostof 0`.

The review suggested:

Before overwriting `m.local`, erase the old entry: `if (m.local) g_host_of.erase(m.local);`. No switch needed.

## Verification
`coop_cell_sync.py`: `coop.pool zap 4` destroys 4 mirror cells outside the mod's removal path (as
the game does); the next state recreates them; then `host_of` must equal `mirrors`. This one asserts
the mod's table on purpose (the bug is in it). FAIL with `SPORE_CELL_FIX=0` (out/lab/20261027-231147): host_of 284,
mirrors 280. PASS (out/lab/20261027-231004, out/lab/20261027-231523): 377 = 377.

The planned test was:

Add the size of `g_host_of` to the guest's `coop.status` line (`host_of=`). In `tools/tests/coop_cell.py`, after the mirrors have settled, destroy a few mirrors on the guest through the game (a debug command that calls DestroyCell on a mirror's local index, so the next state recreates it). Assert `host_of` equals `mirrors` afterwards. Today it is larger by the number of recreated mirrors; with the fix they are equal. This one asserts a mod table on purpose: the bug is in that table.

### Related
- #10 (lost state packets delete and recreate mirrors, which multiplies this)

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #66.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-cell (bug wave 2); test FAIL without the fix, PASS with it.
- 2026-10-28: released in 0.7.3.0.
