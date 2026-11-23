---
id: BUG-1536
title: Cell to Creature: host crash at 0x00E4E454 (j_mem_free+0x7F4) when Swarm updates a lingering Cell effect against the freed fluid/particle pool
status: open
kind: crash
stage: Cell, Creature
reported: 2026-11-23
source: discord
role: host
versions: 0.7.16
fixed_in:
branch:
commits:
tests:
signature: 3010a413:SporeApp.exe j_mem_free+0x7F4
docs: docs/re/cell.md
related: BUG-0338, BUG-1269, BUG-1436
---

## Report
A host's game (coop 0.7.16, SporeKit 0.3.18, Steam / GOG 2024 `3010a413`, Windows 10) crashed right after evolving from Cell to Creature (146 ms after `stage: Cell -> Creature`, frame 25644). Full private triage note: `out/research/dc-report-20261123-cell-crash-and-terrain.md` in the main checkout (not in git; `crash-20261123-190359.dmp` and `SporeCoop-report-20261123-190552.zip`).

## Root cause
Observed (disassembly of EP1 `SporeApp.exe` + minidump `crash-20261123-190359.dmp`):
* Exception: `0xC0000005` read of `0x045CCC68` at runtime `0x00FDE454` (base `0x00590000`, standard VA `0x00E4E454`, symbolised `j_mem_free+0x7F4`), main thread. Registers: `ecx=045B0008`, `eax=000000DC` (220), `edx=00000294` (660), `esi=0000000B` (11), `edi=00000000` (0).
* Stack scan (rebased to `0x00400000`): `0x00E51891` (`0x00E51850+0x41`, symbolised `Simulator::Cell::DNAForFood+0x481`) <- `0x00AA9132` <- `0x00AAEA16` <- `0x00AB142E` <- `0x00AB17EF` <- `0x00AB2078` <- `0x00A83B18` <- `0x00A72A87` <- `0x00A6D218` (`Swarm::cEffectsManager` update).
* On Cell stage init (`0x00E81E0B`), the game registers static Cell fluid-field object `0x015A839C` (vtable `0x014852B8`) with `Swarm::cEffectsManager` under ID `0x01010003` (`vt+0x88`).
* On Cell stage shutdown (`0x00E82014..0x00E82079`), the game unregisters `0x01010003` (`RegisterSurface(0x01010003, 0, nullptr)`), calls `~cCellGame` (`0x00E63F90`) and `delete cCellGame` (`0x00F47220`), freeing `cCellGame+0x4108` (the soft-body particle pool and fluid velocity grid). It does **not** clear `ds:0x016B3C04` (`cCellGame::_ptr`) and does **not** clear the static Cell bounds `0x016B3C88..0x016B3C9C`.
* Any already-running Swarm particle effect created in Cell keeps its cached pointer `[esi+0x1b0] = 0x015A839C` even after `0x01010003` is unregistered. On the first Swarm update after `Cell -> Creature`:
  1. `0x00AA90EA` calls `vtable+0x18` (`0x00E5CA60`), which checks only the stale static bounds `0x016B3C88..0x016B3C9C` and returns `1`.
  2. `0x00AA912x` calls `vtable+0x24`/`0x28` (`0x00E51850`), which reads stale `[0x016B3C04]` -> `[ecx+0x4108]` (`0x045B0008`, freed memory) and calls `0x00E4E3A0`.
  3. `0x00E4E454` executes `movss xmm2, [ecx + edx*4 + 0x1c210]` (`0x045B0008 + 0xA50 + 0x1C210 = 0x045CCC68`, valid in-bounds grid indices `esi=11, edi=0`) against freed/unmapped memory and faults.

## Fix
Not fixed. Target: **0.7.17.1** (not 0.7.17).
Planned fix:
* Track Cell fluid pool lifetime (set on Cell init `0x00E81D63`, cleared on Cell shutdown `0x00E82060`, or check `game::mode_id() == modes::kCell && cell_alive`). Do **not** null `[0x016B3C04]` itself, because `Cell_GetConsequenceTrait` (`0x00E5C9D0`) reads `[0x016B3C04]+0x5190` after Cell.
* Guard vtable `0x014852B8` methods (`0x00E5CA60` returns `0` when Cell is shut down; `0x00E51850` and `0x00E51800`, which both dereference `[0x016B3C04]+0x4108`, return zeroed output when Cell is shut down) behind a `COOP_FIX` switch.
* Also verify `effects.cpp` stops all relayed Cell Swarm effects immediately on leaving Cell.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened for 0.7.17.1 from Discord report (coop 0.7.16, Steam 3010a413); root cause confirmed from EP1 disassembly and minidump.
