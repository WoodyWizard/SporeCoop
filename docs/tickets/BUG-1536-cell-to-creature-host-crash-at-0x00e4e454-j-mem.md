---
id: BUG-1536
title: Cell to Creature: host crash at 0x00E4E454 (j_mem_free+0x7F4) when Swarm updates a lingering Cell effect against the freed fluid/particle pool
status: fixed
kind: crash
stage: Cell, Creature
reported: 2026-11-23
source: discord
role: host
versions: 0.7.16
fixed_in: 0.7.17.1
branch: rel/0.7.17.1
commits: b6ed7a52, 473beb20, 6bace456
tests: cell_fluid_dtor, cell_fluid_dtor_off
signature: 3010a413:SporeApp.exe j_mem_free+0x7F4
docs: docs/re/cell.md#cell-fluid-dtor
related: BUG-0338, BUG-1269, BUG-1436
---

## Report
A host's game (coop 0.7.16, SporeKit 0.3.18, Steam / GOG 2024 `3010a413`, Windows 10) crashed right after evolving from Cell to Creature (146 ms after `stage: Cell -> Creature`, frame 25644).

## Root cause
Observed (disassembly of EP1 `SporeApp.exe`, Windows minidump, and live Wine lab repro `20261124-124229`):
* Exception: `0xC0000005` read at standard VA `0x00E4E454` (symbolised `j_mem_free+0x7F4`), main thread, called from `0x00E51891` (`CellFluid_Sample+0x41`) under `Swarm::cEffectsManager` update.
* On Cell stage init (`0x00E81E0B`), the game registers static Cell fluid-field object `0x015A839C` (vtable `0x014852B8`) with `Swarm::cEffectsManager` under ID `0x01010003` (`vt+0x88`). On entering the Cell world (`0x00E80F57`), `CellFluidPool_Alloc` (`0x00E4ECA0`) allocates the `0x30580`-byte soft-body particle pool and fluid velocity grid and stores it in `[cCellGame+0x4108]`.
* On leaving Cell (`cCellGameMode::OnModeExit` `0x00E81BA0` -> `CellWorld_Shutdown` `0x00E7F9B0` at `0x00E81BDB`), the game frees `[cCellGame+0x4108]` via `0x00E4DC60` (`operator delete`) without nulling `[cCellGame+0x4108]` or clearing the static Cell bounds `0x016B3C88..0x016B3C9C`. Later, `cCellGameMode::Shutdown` (`0x00E82014..0x00E82079`) runs `~cCellGame` (`0x00E63F90`) without clearing `[0x016B3C04]` (`Cell_GetConsequenceTrait` `0x00E5C9D0` still reads `[0x016B3C04]+0x5190` after Cell).
* Any already-running Swarm particle effect created in Cell keeps its cached pointer `[esi+0x1b0] = 0x015A839C`. On the next Swarm update after leaving Cell:
  1. `vt+0x18` (`CellFluid_Contains` `0x00E5CA60`) checks only the stale static bounds `0x016B3C88..0x016B3C9C` and returns `1`.
  2. `vt+0x24`/`+0x28` (`CellFluid_Sample` `0x00E51850`, and likewise `vt+0x20` `CellFluid_Apply` `0x00E51800`) dereferences stale `[0x016B3C04]+0x4108` and calls `0x00E4E3A0`, faulting at `0x00E4E454` (`movss xmm2, [ecx + edx*4 + 0x1c210]`) on freed memory.

## Fix
`game.cell_fluid_dtor` (`mods/coop/gamefixes.cpp`, symbols in `data/symbols/overrides_dc04aee5.sym`, `docs/re/cell.md#cell-fluid-dtor`):
* Hook `CellFluidPool_Alloc` (`0x00E4ECA0`), `CellWorld_Shutdown` (`0x00E7F9B0`), and `cCellGame_dtor` (`0x00E63F90`) to track `g_cell_fluid_alive` without clearing `[0x016B3C04]`.
* Guard `CellFluid_Contains` (`0x00E5CA60` -> returns `0`), `CellFluid_Apply` (`0x00E51800` -> no-op), and `CellFluid_Sample` (`0x00E51850` -> zeroes `out[0..2]` and returns `out`) while the Cell fluid pool is not alive.
* Added `coop.cellfluid [probe]` diagnostic/test command exercising `vt+0x18` + `vt+0x24` on the static fluid-field object.

## Verification
* FAIL without fix (`SPORE_FIX_OFF=game.cell_fluid_dtor`): `out/lab/20261124-130047` (`cell_fluid_dtor_off`: probe after Cell exit entered freed pool with `contains=1 guarded=0`; live Wine `0xC0000005` at `0x00E4E454` also reproduced in `out/lab/20261124-124229`).
* PASS with fix: `out/lab/20261124-124925` (`cell_fluid_dtor`: `hooked=1 alive=1` in Cell, `alive=0 contains=0 vel=(0.000 0.000 0.000) guarded=2` after leaving Cell, 0 faults).

## History
- 2026-11-23: opened for 0.7.17.1 from Discord report (coop 0.7.16, Steam 3010a413); root cause confirmed from EP1 disassembly and minidump.
- 2026-11-24: fixed via `game.cell_fluid_dtor` on `rel/0.7.17.1`; verified with PASS (`20261124-124925`) and FAIL OFF-twin (`20261124-130047`).
- 2026-11-24: released in 0.7.17.1.
