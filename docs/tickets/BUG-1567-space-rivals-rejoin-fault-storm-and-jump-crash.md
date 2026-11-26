---
id: BUG-1567
title: Space Rivals: 0x0106CA9F null-empire fault storm on galaxy-view rejoin emigration and 0x00BB0F22 crash on Space jump EnterSolarSystem
status: open
kind: crash
stage: Space
reported: 2026-11-25
source: 0.8.1 4-player campaign_rivals (20261125-182926)
role: guest
versions: 0.8.1
fixed_in:
branch: w081/fix-space-rivals-crashes
commits:
tests: space_rivals_rejoin_jump, space_rivals_rejoin_jump_off
signature: 0x00BB0F22, 0x0106CA9F
docs: docs/re/space.md
related: BUG-1544, BUG-1550, BUG-1555, BUG-1562
---

## Report
During the 4-player `campaign_rivals` run (`out/lab/20261125-182926`) on `rel/0.8.1`:
1. **Guest rejoin in Space Rivals (`ctx=2`, Galaxy view) -> 645-fault storm at `0x0106CA9F` (`SporeApp.exe+0xC6CA9F`, read of `0x00000084`, `eax=0`, `esi=008AC03D` host home planet) + `0x00FDC5FD` (`cAppModeSpace_OnGalaxyEntered+0x1A1D`, read of `0x00000084`)**:
   - Dave rejoined into Space Rivals, immediately emigrated to `9351184` (`008EB010`), relinquished host home star `9093181` (`008AC03D`, setting `mEmpireID = 0xFFFFFFFF`), and jumped in `ctx=2` (`teleport_in_galaxy`), after which `SpaceGameUI_Update+0xAE` (`0x0106CA9F`) faulted 645 times in 10 s reading `[empire+0x84]` on the relinquished host home planet `008AC03D`.
2. **Space Rivals `ui: click jump` -> crash `0xC0000005` at `0x00BB0F22` (`cStarManager_SetHomeStar+0x1762`, read of `0x0000003C`)**:
   - Carol clicked `jump` in Space Rivals (`UFO_SnapToDestination+0xEE` -> `EnterSolarSystem+0xA8` -> `GetEmpireForStar+0xE7` -> `AddStarOwnership+0x124` -> `UpdateAndGetColor+0x406` -> `FindStars+0xA0` -> `0x00BB0F22` read `[0x3C]`) and crashed immediately.

## Root cause
Pending disassembly of `0x00BB0F22`, `0x0106CA9F`, `0x00FDC5FD` and inspection of `mods/coop/space_rivals.cpp`, `mods/coop/spacecoop.cpp`, and `mods/coop/gamefixes.cpp`.

## Fix
Pending implementation and Wine lab verification.

## Verification
Pending Wine lab ON PASS + OFF FAIL verification.
