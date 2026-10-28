---
id: BUG-0092
title: star lookup by id reads past the end of the star table
status: fixed
kind: crash
stage: Space
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#70
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-space
commits: 8604dd3, 48a56fc
tests: space_stale (coop_space_stale.py; --without-fix must fail)
signature:
docs: docs/re/space.md#space-stale-lookups, docs/06-testing.md#testing-space-stale
related: #70
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/70 (full text, discussion).

`spacecoop::star_record` is the safe replacement for the game's `GetStarRecord`, used for every star id that comes from the network: partner places, rosters, colonies, raids. It checks the sector number but not the index inside the sector. For an index past the sector's array it reads heap memory beyond the array and treats it as a star pointer. Only `IsBadReadPtr` and the id compare at +0x70 stop it. Severity: minor (hardening; a wrong record or an access to a guard page is unlikely but possible).

### Where
- https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L309-L331
- The same table read with the sector's end pointer, in `nav_entries`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L1019-L1030

### How to reproduce
`coop.space jumpto 0x00000FFF` (sector 0, index 4095), or any network id with a large index: the function reads `arr[4095]`. Usually it returns null by luck of the id compare. It is not visible to a player in normal play, because ids normally come from the same galaxy.

### Expected
Ids outside the table return null without reading outside the array.

## Root cause
**What happens**

```cpp
uint32_t sec = id >> 12, idx = id & 0xFFF;
if (!b || e < b || sec >= (uint32_t)((e - b) / 20) || IsBadReadPtr(b + sec * 20, 4))
    return nullptr;
void** arr = *(void***)(b + sec * 20);
if (!arr || IsBadReadPtr(arr + idx, 4))      // no check against the sector's end
    return nullptr;
void* r = arr[idx];
```

Each 20-byte sector starts with `{begin, end, ...}` of its record array (as `nav_entries` and `space_map::nearest_star` read it). `idx` can be up to 0xFFF while a sector holds far fewer stars. `arr + idx` then points into whatever heap block follows. That memory is normally readable, so `arr[idx]` is an arbitrary value. It is dereferenced through `IsBadReadPtr(r, 0x74)` and `F<uint32_t>(r, 0x70)`. `IsBadReadPtr` on arbitrary addresses can also touch another thread's stack guard page, a known Windows pitfall.

### Confidence
CONFIRMED by reading. No crash or wrong result seen.

## Fix
Fixed in `spacecoop::star_record`: it reads the sector's end pointer (+4) and returns null for an
index at or past it, before reading the array. `coop.space starfix 0` restores the old read;
`coop.space starrec <id>` reports the record, the sector size and whether the lookup read outside
the array (tests). The other table walkers (`nav_entries`, `space_map::nearest_star`, contracts,
allies) already stop at the sector's end.

## Verification
Done (2026-10-27, lane 6, GOG 2024). `coop_space_stale.py` looks up the own star, then the id one
past the last star of its sector (00460035 has a sector of 61 stars, so 0046003D), and
`jumpto` to it.
- FAIL with `--without-fix` (starfix 0): lab run 20261027-183320 space_stale_nofix: `read_outside=1`, and
  the host's fault log shows two caught access violations in IsBadReadPtr (kernel32+0x1B997,
  data=0x00000131, 0046003D on the stack): the word past the array was 0x131, not a pointer, and
  `IsBadReadPtr(r, 0x74)` touched it. This is the game-side evidence of the bug.
- PASS with the fix: 20261027-183320, 20261027-183607, 20261027-190159: no record, nothing read, "no such star", no fault.

The original plan:

A test command case in `tools/tests/coop_space_jump.py` (or a small `coop_space_starid.py`):
1. `coop.space jumpto <id>` with an id whose index is just past the last star of sector 0 (read the sector size from `coop.space empire near` / the table).
2. Assert it answers "no such star" and the game's active star is unchanged.
3. Also assert that `star_record` of a real id still works.

To make the test fail on main deterministically, add a debug read-out `coop.space starrec <id>` that reports whether the index was inside the array. Main reports a read outside it.

### Related
- docs/re/space.md 7.9 (cStarManager +0xC8 sectors) and 7.3 (the jump uses this lookup).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #70.
- 2026-10-27: ticket opened from the issue.
- 2026-10-27: fixed on fix/w1-space (wave 1, Space).
- 2026-10-28: released in 0.7.3.0.
