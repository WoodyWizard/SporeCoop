---
id: BUG-1537
title: Creature: after a cold reload with D3D9 device resets, a guest's nest terrain sits 1.3-3.0 m lower than the host's (flooded lowlands and sea-monster kills on host dry land)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-23
source: discord
role: guest
versions: 0.7.16
fixed_in: 0.7.17.1
branch: rel/0.7.17.1
commits: 45e02271, 473beb20, 6bace456
tests: load_active_device, load_active_device_off, sync_terrain_check_off
signature:
docs: docs/re/tribe.md#tribe-terrain-stamps
related: BUG-0329, BUG-0332, BUG-0465, BUG-1088, BUG-1216, BUG-1528
---

## Report
In a 2-player Creature session (coop 0.7.16, SporeKit 0.3.18, Steam / GOG 2024 `3010a413`, both on AMD GPUs), after the guest restarted Spore and reloaded the host's shared Creature world, large lowland/nest regions appeared flooded as a swamp on the guest while remaining normal dry land on the host, and the guest was eaten by the sea monster while standing on dry land on the host.

## Root cause
Observed (measured `GetHeightAt = |pos| - agl` from host and guest `sporekit.log` traces + lab check `20261124-124925`):
* During the guest's first process (including mid-session world-share reloads), host and guest terrain radii matched across 184 paired points (mean delta `+0.02 m`).
* After the guest restarted Spore and loaded the host's Creature share cold while minimized across three D3D9 `device Reset` calls (`18277 ms` hitch during `Galaxy menu -> Loading -> Creature`), ground height at nest-stamped areas dropped by `1.3-3.0 m` (`-1.28 m` to `-3.04 m`), while non-nest areas still matched the host.
* Lab check (`20261124-124925`, `load_active_device`) confirmed that both cold Creature load and mid-session Creature reload with an active D3D9 device produce `0.00 m` terrain height divergence (`terrain_warns=0, terrain_max_m=0.00`), isolating the D3D9 device-loss / minimized load path (`Terrain::cTerrainSphere` builds its heightmap and stamps on D3D9 GPU render targets).
* Diagnostic gap: `sync-check` logged `OK` throughout because `BUG-1216` compares NPCs by `agl` over each game's own terrain, hiding a 2-3 m absolute `GetHeightAt` divergence.

## Fix
Two switches (`mods/coop/share_logic.h`, `mods/coop/world.cpp`, `mods/coop/net/diagmsg.h`, `mods/coop/diag_sync.cpp`):
* `world.load_active_device`: subscribe to `SK_EV_DEVICE_LOST` / `SK_EV_DEVICE_RESET` in `world.cpp` and check `IsIconic(hwnd)`, `g_device_lost`, `IDirect3DDevice9::TestCooperativeLevel()` (vt slot 3), and a `1.5 s` post-Reset settle window in `via_menu_tick()` before firing `load_now` (extending `g_via_menu.since` while deferred so the wait does not time out). Added `coop.worldfix devbusy 0|1` test helper and `loads delayed for graphics device` status counter.
* `sync.terrain_check`: added `dg::terrain_diff()` using existing NPC `(x, y, z, agl)` digest fields (no wire change) and log rate-limited `sync-check: terrain height differs` warnings in Creature when absolute ground-radius divergence reaches `>= 1.0 m`. Added `coop.diag sync force terrain` test helper and `terrain_warns=` / `terrain_max_m=` fields in `coop.diag sync`.

## Verification
* Unit tests: `core_tests` (`share_one_planet` `load_device_blocker`) and `net_tests` (`test_diag_terrain_diff`).
* FAIL without fixes: `out/lab/20261124-130047` (`load_active_device_off` with `SPORE_FIX_OFF=world.load_active_device`: guest loaded immediately while `devbusy=1`, `loads 2->4`, `delayed 0->0`; `sync_terrain_check_off` with `SPORE_FIX_OFF=sync.terrain_check`: 2.5 m guest terrain drop went unflagged, `flagged=False, warns=0->0`).
* PASS with fixes: `out/lab/20261124-124925` (`load_active_device`: clean cold and reloaded Creature terrain matched with `terrain_max_m=0.00`, queued load stayed in galaxy menu while `devbusy=1` (`delayed 0->1`), loaded cleanly once `devbusy=0`, and forced `-2.5 m` terrain shift logged `sync-check: terrain height differs` with `warns=0->1`).

## History
- 2026-11-23: opened for 0.7.17.1 from Discord report (coop 0.7.16, Steam 3010a413); height drop quantified from paired host/guest logs.
- 2026-11-24: fixed via `world.load_active_device` and `sync.terrain_check` on `rel/0.7.17.1`; verified with PASS (`20261124-124925`) and both FAIL OFF-twins (`20261124-130047`).
- 2026-11-24: released in 0.7.17.1.
