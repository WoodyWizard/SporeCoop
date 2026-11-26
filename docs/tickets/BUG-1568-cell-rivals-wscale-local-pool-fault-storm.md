---
id: BUG-1568
title: Cell Rivals: local-pool wscale fallback triggers 0x00E4E384 fault storm on guest in Cell->Creature advance
status: fixed
kind: crash
stage: Cell
reported: 2026-11-25
source: 0.8.1 4-player campaign_rivals (20261125-182926)
role: guest
versions: 0.8.1
fixed_in: 0.8.1
branch: w081/fix-cell-rivals-fault
commits: 9cb1fd5f
tests: cell_rivals_wscale_fault, cell_rivals_wscale_fault_off
signature: 0x00E4E384
docs: docs/re/cell.md
related: BUG-1546, BUG-1552, BUG-1536
---

## Report
During the 4-player `campaign_rivals` run (`out/lab/20261125-182926`) on `rel/0.8.1`:
- Bob's guest Cell hit `cell_rivals: wscale diverged (host=4.722 own=0.5 ratio=9.445) -> guest plays local pool to catch up (BUG-1552)`.
- Within 20 seconds, Bob's cell coordinates drifted from `(60, 217)` to `(5590.6, 7036.3)` and `(8187.9, 10249.0)` at `wscale=0.5` and hit an 801-fault storm at `0x00E4E384` (`j_mem_free+0x724`, read of `0x00000112`, called from `0x00E4DDD4` < `0x00E4EBED` < `0x00E59EFF` (`Cell_LocksReset+0x7F`) < `0x00E7F46A` (`Simulator::Cell::SimUpdate+0xCA`)), plus 322 more faults at the same `0x00E4E384` address while still in Cell during `creature/follow` before quitting to the Galaxy menu.

## Root cause
Two interacting bugs in GOG 2024 `SporeApp.exe` and Cell Rivals:

1. **War rival puppet launch under `wscale` divergence**: When Alice (host) advanced to `food=1000` (`wscale` surged `0.5 -> 4.722 -> 6793 -> 15000`), BUG-1552 switched guests to local-pool mode (`g_scale_solo=true`), but `modes.cpp:cell_drive` kept scaling Alice's puppet on guests by `k = host_scale / own_scale` (making Alice's puppet up to ~999 m wide). Carol (Allied) and Dave (Neutral) skipped `(avatar, alice_puppet)` in `h_Pair`, while Bob (at War with Alice) allowed `o_Pair(avatar, alice_puppet)`. The resulting penetration impulse catapulted Bob's avatar across the map at ~658 m/s into `(8187.9, 10249.0)`.
2. **Engine stack buffer overflow at `0x00E4E320` -> `0x00E4E384` (`docs/re/cell.md#cell-fluid-viewbox-overflow`)**: `Simulator::Cell::SimUpdate` builds `ViewBoxes` (`0x016B3C88..0x016B3C9C`) as `cam.x +- 10.0f` and `cam.y +- 7.5f`, then calls `0x00E59EE0(dt)` -> `0x00E4EBC0` -> `0x00E4E8B0` -> `0x00E4E320` -> `0x00E4E210`. `0x00E4E320` allocates a 9-int (`3x3`) stack buffer at `[esp+0x0C..0x2F]` directly below its return address and `outCounts` parameter `[esp+0x38]`. `0x00E4E210` computes `half_cols = ceil((maxX - minX) * 0.05f - 2^-16)` and `half_rows = ceil((maxY - minY) * (1/15) - 2^-16)`. Once `|cam.x| >= 8182.4` (straddling `2^13 = 8192`) or `|cam.y| >= 4088.7` (straddling `2^12 = 4096`), float32 rounding yields `maxX - minX = 20.000488`, pushing `half_cols` to 2 and writing `5 * 3 = 15` (`eax=0x0000000F`) ints into the 9-int buffer. That clobbers `outCounts` with a grid index (`0x00000112`), causing `0x00E4E384: mov [edi+ecx], edx` (`esi=0xFFFFFFB4`) to fault every frame until leaving Cell.

## Fix
Gated behind `COOP_FIX("cell.rivals_wscale_solo_safe", "BUG-1568", ...)`:
- Added portable helpers in `mods/coop/cell_rivals_logic.h` (`puppet_scale_collision_unsafe`, `clamp_puppet_size_for_scale`, `sanitize_fluid_viewbox`) with unit tests in `tests/cell_rivals_tests.cpp`.
- In `mods/coop/cellsync_guest.cpp`, exposed `scale_solo_active()` and `last_host_scale()`.
- In `mods/coop/cellsync_tiles.cpp` (`h_WorldScale`) and `mods/coop/cell_rivals.cpp`, clamp over-scaled rival puppets (`kSize`, `kTargetSize`) before `UpdateCells` and track over-scaled owners per frame.
- In `mods/coop/cellsync.cpp`, wrapped `HandleCollisionPair` with `h_PairSafe` to skip `(avatar, puppet)` collisions when `rivals_wscale_puppet_unsafe(pup)` is true, and hooked `0x00E59EE0` (`resolve_fluid_step` via `SimUpdate+0xC5` with prologue check) with `h_FluidStep` to run `sanitize_fluid_viewbox` on `ViewBoxes` (`0x016B3C88`) so `0x00E4E210` never exceeds 9 cells at any finite coordinate.

## Verification
- `make -j$(nproc)`, `make unit-cpp unit-net`, `python3 tools/re_lint.py`, `python3 tools/docs_lint.py`, `python3 tools/glyph_lint.py`, `python3 tools/tests/test_lint.py`: PASS (including `cell_rivals_tests` `test_wscale_solo_safety_and_fluid_viewbox`).
- Wine lab on lane 5 (`out/lab/20261125-194346`):
  - `cell_rivals_wscale_fault` (fix ON): PASS (`max_drift=0.7 m`, `puppets_clamped=194`, `puppet_skips=239`, `fluid_clamps=0->163`, 0 faults on host and guest).
  - `cell_rivals_wscale_fault_off` (`SPORE_FIX_OFF=cell.rivals_wscale_solo_safe`): FAIL (`max_drift=4209.9 m`, `puppets_clamped=0`, `puppet_skips=0`, `fluid_clamps=0->0`, 179 caught faults / 477-fault storm at exact `0x00E4E384`, `eax=0000000F`, `esi=FFFFFFB4`).
- Wine lab regression on lane 5 (`out/lab/20261125-195022`): `rivals_cell` PASS (0 faults on host and guest).
