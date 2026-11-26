---
id: BUG-1567
title: Space Rivals: 0x0106CA9F null-empire fault storm on galaxy-view rejoin emigration and 0x00BB0F22 crash on Space jump EnterSolarSystem
status: fixed
kind: crash
stage: Space
reported: 2026-11-25
source: 0.8.1 4-player campaign_rivals (20261125-182926)
role: guest
versions: 0.8.1
fixed_in: 0.8.1
branch: w081/fix-space-rivals-crashes
commits: e8129a70
tests: space_rivals_rejoin_jump, space_rivals_rejoin_jump_off
signature: 0x00BB0F22, 0x0106CA9F
docs: docs/re/space.md
related: BUG-1544, BUG-1550, BUG-1555, BUG-1562
---

## Report
During the 4-player `campaign_rivals` run (`out/lab/20261125-182926`) on `rel/0.8.1`:
1. **Guest rejoin in Space Rivals (`ctx=2`, Galaxy view) -> 645-fault storm at `0x0106CA9F` (`SporeApp.exe+0xC6CA9F`, read of `0x00000084`, `eax=0`, `esi=008AC03D` host home planet) + `0x00FDC5FD` (`cAppModeSpace_OnGalaxyEntered+0x1A1D`, read of `0x00000084`)**:
   - Dave rejoined into Space Rivals while Alice was in Galaxy view (`ctx=2`), immediately emigrated to `9351184` (`008EB010`), relinquished host home star `9093181` (`008AC03D`, setting `mEmpireID = 0xFFFFFFFF`), and jumped in `ctx=2` (`teleport_in_galaxy`), after which `SpaceGameUI_Update` cursor update (`0x0106CA9F`) faulted 645 times in 10 s reading `[empire+0x84]` on the relinquished host home star `008AC03D`, plus 2 faults at `0x00FDC5FD` on Galaxy click.
2. **Space Rivals `ui: click jump` -> unhandled crash `0xC0000005` at `0x00BB0F22` (`cStarManager_SetHomeStar+0x1762`, read of `0x0000003C`)**:
   - Carol (allied with Alice) clicked `jump` in Space Rivals (`UFO_SnapToDestination+0xEE` -> `EnterSolarSystem+0xA8` -> `GetEmpireForStar+0xE7` -> `AddStarOwnership+0x124` -> `UpdateAndGetColor+0x406` -> `FindStars+0xA0` -> `0x00BB0F22` read `[0x3C]`) targeting Alice's relinquished home star `9093181` (`008AC03D`) and crashed immediately.

## Root cause
Both bugs share the same root cause in `mods/coop/space_rivals.cpp` (`relinquish_host_home_now()`) plus two unguarded Spore engine dereferences verified via `objdump` of GOG 2024 `SporeApp.exe`:
1. **Orphaned tech-5 star left by `relinquish_host_home_now()`**: relinquishing the host's original home star cleared `orig_star + 0x54` (`mEmpireID = 0xFFFFFFFF`) and `orig_star + 0x5C` home bits (`&= ~7`), but left `orig_star + 0x4C` (`cStarRecord::mTechLevel`) and `orig_rec + 0x194` (`cPlanetRecord::mTechLevel`) at `5` (`TechLevel::Empire`) and left `orig_star` in `SpaceNearStars` (`cSimulatorSpaceGame + 0x30`, which also risks `0x00C32817` `cEmpire_SpawnVisitorUFO` on null empire). Because the `All rivals` preset sets `space.allies = 0`, `space_allies` never claims `orig_star` for a rival mirror empire (and even with `space.allies = 1`, the star stays orphaned before mirror apply and during save holds).
2. **Bug A (`0x0106CA9F` and `0x00FDC5FD`)**: in Galaxy view (`ctx == 2`), both `SpaceGameUI_Update+0xA9` -> `0x0106C960` (`0x0106CA71..0x0106CA9F`) and `cAppModeSpace_OnGalaxyEntered+0x1A13` (`0x00FDC5CD..0x00FDC5FD`) fetch the hovered star via `0x00FFF780`, check `star->GetTechLevel() == 5`, call `cStarManager::GetEmpire(star->GetEmpireID())` (`0x00BA94C0`), and immediately read `[emp + 0x84]` (`mPoliticalID`) with no null check before calling `MissionManager::HasMissionFor` (`0x00AE9B90`). With `mEmpireID == 0xFFFFFFFF`, `GetEmpire` returns `nullptr`, causing a 65 Hz SEH-caught fault storm on hover and faults on click.
3. **Bug B (`0x00BB0F22`)**: Carol was allied with Alice, so `spacecoop::jump(0xFF)` targeted Alice's relinquished home star `9093181`. Inside `cAppModeSpace::EnterSolarSystem` (`0x00FDD61D`), `star->GetTechLevel() == 5` triggers `cStarManager::GetEmpireForStar(star)` (`0x00BB14D0`), which allocates a new NPC `cEmpire` (`mColorID = 0`, `mHomeStarID (+0xB0) = 0xFFFFFFFF`) and calls `cEmpire::AddStarOwnership(emp, star)` (`0x00C34300` at `0x00BB15B2`) **before** `cEmpire::SetHomeStar(emp, star)` (`0x00C30D70` at `0x00BB15BA`). Because `orig_rec` already has `mTechLevel == 5` and existing `cCivData`, `AddStarOwnership` calls `0x00C73940` -> `cEmpire::UpdateAndGetColor` (`0x00C32E30`). On the slow color-selection path (`0x00C33202..0x00C33231`), `mHomeStarID == 0xFFFFFFFF` skips `GetStarRecord`, passes `nullptr` to `cStarRecord::GetPosition()` (`0x005C66D0` -> `0x0000003C`), and crashes inside `cStarManager::FindStars` -> `0x00BB0F10` -> `0x00BB0F22` (`movss xmm0, [esi]`).

## Fix
Implemented three layered `COOP_FIX` fixes referencing `BUG-1567`:
1. **`rivals.space_relinquish_clean`** (`mods/coop/space_rivals.cpp`, `mods/coop/space_rivals_logic.h`): `relinquish_host_home_now()` lowers `orig_star + 0x4C` and tech-5 planets' `+0x194` from `5` to `1` (`TechLevel::Creature`, preserving `cCivData` at `+0x15C` so `space_allies::add_planet(keep_host_home)` continues to work intact when `space.allies = 1`), removes any extra tech-5 colonies on `orig_star` from `SpacePlayerData`, clears loaded `cPlanet` political IDs, and rebuilds `SpaceNearStars` (`0x00FE6A50`). Added read-only `coop.space rivals star <id>` inspection subcommand.
2. **`space.null_empire_galaxy`** (`mods/coop/gamefixes.cpp`): hooks `Simulator::cStarManager::GetEmpire` (`0x00BA94C0`); when it returns `nullptr` to verified return addresses `0x0106CA9C` (`SpaceGameUI_Update+0xA9` -> `0x0106C960`) or `0x00FDC5F8` (`cAppModeSpace_OnGalaxyEntered+0x1A13`), returns a static sentinel with `+0x84 = 0xFFFFFFFE` (non-wildcard, since `0x00AE9480` treats `0xFFFFFFFF` as a wildcard), so `0x00AE9B90` returns `false` cleanly.
3. **`space.empire_color_homestar`** (`mods/coop/gamefixes.cpp`): hooks `cEmpire::UpdateAndGetColor` (`0x00C32E30`, resolved and byte-verified from `Simulator::cEmpire::AddStarOwnership+0x11F` -> `0x00C73940+0x9E`); when `mColorID` is unset and `mHomeStarID` is unset or dangling, temporarily supplies a valid owned star from `emp->mStars (+0x88)` (restoring `mHomeStarID` afterwards so `SetHomeStar` runs normally) or assigns a valid palette ID (`0x53DBCF3 + pol % 12`) if `mStars` is empty.

## Verification
- Native ASan+UBSan unit tests (`tests/space_rivals_tests.cpp`): 137/137 passed, including clean relinquish invariant and `UpdateAndGetColor` fallback tests (`make unit-cpp unit-net unit-lint`, `test_lint.py` 0 problems).
- Two-game Wine lab (`out/lab/20261125-195253`, lane 4):
  - `space_rivals_rejoin_jump` (fixes ON): **PASS** (45 s, 0 faults, 0 crashes; guest emigrates in `all_rivals` preset with `space.allies=0`, `h_star0` cleanly lowered to `star_tech=1 planet_tech=1`, survives Galaxy-view rejoin + hover, forms Alliance, and jumps into `h_star0` cleanly).
  - `space_rivals_rejoin_jump_off` (`SPORE_FIX_OFF=rivals.space_relinquish_clean,space.null_empire_galaxy,space.empire_color_homestar`): **FAIL** as expected (reproduces orphaned `star_tech=5 planet_tech=5`, `0x00C32817` near-star visitor fault in `out/lab/20261125-194954`, and exact `0x00BB0F22` unhandled crash in `crash-20261125-195418-i8.txt`).
- Isolated guard check (`SPORE_FIX_OFF=rivals.space_relinquish_clean` with engine guards ON, lane 4): guest jumped into orphaned tech-5 `h_star0` with `space.empire_color_homestar` ON and entered the system with 0 crashes and 0 faults.
- Regressions (`out/lab/20261125-195440`, lane 4): `rivals_space` **PASS** (67 s) and `rivals_designs_jump` **PASS** (58 s).
