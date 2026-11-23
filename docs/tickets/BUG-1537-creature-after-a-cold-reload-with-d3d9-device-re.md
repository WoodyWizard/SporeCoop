---
id: BUG-1537
title: Creature: after a cold reload with D3D9 device resets, a guest's nest terrain sits 1.3-3.0 m lower than the host's (flooded lowlands and sea-monster kills on host dry land)
status: open
kind: desync
stage: Creature
reported: 2026-11-23
source: discord
role: guest
versions: 0.7.16
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/tribe.md#tribe-terrain-stamps
related: BUG-0329, BUG-0332, BUG-0465, BUG-1088, BUG-1216, BUG-1528
---

## Report
In a 2-player Creature session (coop 0.7.16, SporeKit 0.3.18, Steam / GOG 2024 `3010a413`, both on AMD GPUs), after the guest restarted Spore and reloaded the host's shared Creature world, large lowland/nest regions appeared flooded as a swamp on the guest while remaining normal dry land on the host ("part of a world is a literal swamp for me, sometimes nests spawn in water, sometimes i don't see nests in water"). Five minutes later, while standing 13 m from the host on dry land, the guest was eaten by the sea monster ("my friend was eaten by nothing"). Full private triage note: `out/research/dc-report-20261123-cell-crash-and-terrain.md` in the main checkout (not in git).

## Root cause
Observed (measured `GetHeightAt = |pos| - agl` from host and guest `sporekit.log` traces + mark screenshots):
* During the guest's first process (19:13-20:22, including mid-session world-share reloads at 19:26 and 20:01), host and guest terrain radii matched across 184 paired points (mean delta `+0.02 m`; at nest 2 `(475, -167, -63)` both read `506.83 m`).
* After the guest restarted Spore and loaded the host's Creature share cold at 20:24:28 (run 2), ground height at nest-stamped areas (where 136 `ApplyStamp` calls from `0x0106AD74` had run live at 19:13/19:16) dropped by `1.3-3.0 m`, while non-nest areas still matched the host (`500.06` vs `500.39 m`, `502.32` vs `502.43 m`):
  * Nest 2 `(474, -167, -63)`: dropped from `506.83 m` (guest run 1) / `506.41 m` (host) to `505.55 m` (`-1.28 m`).
  * Southwest nest area `(154..235, -395, -287..-218)`: host `506.01-507.05 m` (dry grass in host screenshot) vs guest `504.27-504.70 m` (`-1.74` to `-2.35 m`, below sea level `~505 m`).
  * Point `(373, -331, -71)`: host `506.79 m` vs guest `503.75 m` (`-3.04 m`).
* Two candidate mechanisms occurred right at the guest's cold load (20:24:28-20:24:36) and need a lab check to isolate:
  1. **Loading while minimized / D3D9 device lost + resets mid-load:** `world: loading` ran at 20:24:28.420 while `Present` had stopped reaching SporeKit (Alt-Tab / fullscreen minimized, `18277 ms` hitch), followed by **three** D3D9 `device Reset` calls at `20:24:29.632` (mid `Galaxy menu -> Loading -> Creature`), `20:24:35.109`, and `20:24:35.844`. On the first Creature frame (`20:24:29.980`) the sphere was ungenerated (`rad=399.98 m`). Because `Terrain::cTerrainSphere` builds its heightmap and stamps on D3D9 GPU render targets, device loss/reset mid-generation can drop or corrupt stamp passes.
  2. **BUG-0332 class (`live ApplyStamp` vs cold load rebuild) in Creature stage:** host (19:16:15) and guest run 1 (19:13:42) both ran the 136 `0x0106AD74` nest stamps live, whereas guest run 2 performed a fresh cold rebuild from the save's stamp list.
* Note also a diagnostic gap: `sync-check` logged `OK` throughout because `BUG-1216` compares NPCs by `agl` over each game's own terrain, hiding a 2-3 m absolute `GetHeightAt` divergence.

## Fix
Not fixed. Target: **0.7.17.1** (not 0.7.17).
Planned investigation and fix:
* Reproduce in lab to separate (1) cold Creature load after live `0x0106AD74` stamps (`BUG-0332` class) from (2) `load_slot` with D3D9 device loss / `device Reset` during load.
* Defer automatic `world::load_slot` while the window is minimized / `Present` is missing (wait until the D3D9 device is active).
* If Creature shares `BUG-0332`'s live-vs-load stamp split, evaluate extending `tribe.host_reload`-style alignment to Creature entry after the stamp burst.
* Add an absolute `GetHeightAt` / terrain-radius check (or warning) in `sync-check` so multi-metre terrain divergence is caught immediately.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened for 0.7.17.1 from Discord report (coop 0.7.16, Steam 3010a413); height drop quantified from paired host/guest logs.
