---
id: BUG-1550
title: Space Rivals: guest homeworld emigration, inviolable homeworld, NPC empire influence and Chronicle finale
status: fixed
kind: feature
stage: Space
reported: 2026-11-25
source: docs/11-rivals.md 11.3.5
role: both
versions: 0.8.0
fixed_in: unreleased
branch: w08/rivals-space
commits: 8e1401bd, 7e36f161
tests: rivals_space, rivals_space_off
signature:
docs: docs/11-rivals.md, docs/re/space.md
related: BUG-0794
---

## Report
Implement full Space Rivals mode (`docs/11-rivals.md` sections 11.3.5, 11.4, 11.7, 11.9) behind `COOP_FIX("rivals.space", "BUG-1550", ...)`:
- R5 guest homeworld emigration: host keeps its original homeworld while every guest emigrates to a suitable nearby unowned terrestrial planet with as many cities as their nation had, receives an EN/RU emigration notice, mirrors the host's original planet as host's rival empire, and persists the assignment cleanly across saves and rejoins without corrupting the host's save.
- R7 homeworld inviolability: player homeworlds cannot be destroyed by a Planet Buster (or Gravitation Wave / city destruction) or captured (system capture, star transfer, Fanatical Frenzy, or city capture progress), while ordinary colonies and ship-vs-ship PvP at War continue working.
- Unified `rivals.h` 4x4 relations (including 60 s alliance break countdown), per-player NPC empire diplomacy (shared diplomacy OFF), Space NPC influence (`TurnAgainst`, `Protect`, `SmoothOver`, earned from missions/trade/gifts and buyable with own Space sporebucks), Space Race progress (money, colonies, stars, allied NPCs, distance to core), and Chronicle Galaxy Core finale (`+250` `kPtsGalaxyCore` bonus once to the first player with EN/RU winner summary while the game continues).

## Root cause
Observed on GOG 2024 `SporeApp.exe` (`dc04aee5`, `docs/re/space.md#space-rivals-r5-r7`):
- Verified `SporeCoop::cEmpire_SetHomePlanet` (`0x00C31820`), `Simulator::cEmpire::CaptureSystem` (`0x00C8D190`), `SporeCoop::cSimPlanetHighLOD_DestroyPlanet` (`0x00FFAEE0`), `Simulator::cSimPlanetHighLOD::DestroyCity` (`0x00FFA7B0`), `SporeCoop::cToolManager_OnUseTool` (`0x01050BC0`), and `SporeCoop::Space_CalculateTerrascore` (`0x00FC2040`).
- Verified superweapon instance IDs in `Spore_Game.package` (Planet Buster `0x05475298`, Fanatical Frenzy `0x1CC65FA3`, Gravitation Wave `0xA43B6F38`) and `cPlanetRecord` fields (`+0x28` planet type, `+0x2C` bit 2 `IsMoon`, `+0xB0/+0xB4/+0xB8` climate floats, `+0x194` tech level).
- Previously Space stage had only the standalone `space_rivalry.cpp` toggle without guest emigration, homeworld protection, unified `rivals.h` relations/influence, Race stats, or Chronicle Galaxy Core finale.

## Fix
Added `mods/coop/space_rivals_logic.h` (portable scoring, emigration store, R7 protection table, influence helpers, Race stats, Chronicle finale) and `mods/coop/space_rivals.{h,cpp}` registered as a `spaceext::Feature` (`"rivals"`) behind `COOP_FIX("rivals.space", "BUG-1550", ...)`:
- R5 emigration: host picks a nearby unowned terrestrial planet scored by moon-class, T-score, climate similarity, and `BaseDistance`, persists assignments in `store.cpp` (`"space.rivals.emig"`), syncs via `MSG_RIVALS_FACTION` (`Stage::Space`, `SK_EMIGRATE`), and guest applies the new homeworld outside frame with cities, relinquishing the host's original planet for `space_allies.cpp` to mirror as host's rival empire. Automatically re-applies on rejoin after loading the host's world.
- R7 homeworld protection: hooks `cToolManager_OnUseTool`, `cSimPlanetHighLOD_DestroyPlanet`, `cEmpire::CaptureSystem`, `TransferStarOwnership`, `cCity::SetCaptureProgress`, and `cSimPlanetHighLOD::DestroyCity` to block superweapons and capture on all players' homeworlds with EN/RU notices.
- Surgical hooks in `space_allies.cpp` (mirror host's relinquished homeworld on guest), `space_rivalry.cpp` (route relation changes and alliance break through `rivals.h`), `space_diplomacy.cpp` (disable shared diplomacy in Space Rivals and earn influence from NPC missions/trade/gifts), and `space_finale.cpp` (skip shared partner core gate/Steve's gift in Space Rivals and award Chronicle Galaxy Core finale bonus once to the first player with EN/RU summary).
- Switch to disable for tests: `coop.fix rivals.space 0` / `SPORE_FIX_OFF=rivals.space`.

## Verification
- Native ASan+UBSan unit tests: `tests/space_rivals_tests.cpp` (66 passed, 0 failed) + full `make unit-cpp unit-net unit-lint unit-tools` (0 errors, 0 warnings; `re_lint`, `docs_lint`, `glyph_lint`, `test_lint`, `state_fields_check` 0 problems).
- Real two-game Wine lab lane 7 (`out/lab/20261125-041524`):
  - `rivals_space` (ON): PASS (90 s, 0 faults, 0 monitor violations; guest emigrated `4587573/00460035 -> 4325393/00420011` with 9 cities and notice, host kept original homeworld, both galaxies mirrored rival empires, rejoin kept persisted emigrated homeworld, Ally -> direct War rejected -> Break countdown -> War + mirror `rel=2` -> Peace, R7 blocked CaptureSystem on both homeworlds and blocked Planet Buster `OnUseTool` + `DestroyPlanet` with cities intact, shared diplomacy OFF + NPC mission earned `+15` inf, buy deducted real sporebucks `50000 -> 49950`, SmoothOver raised host relation `0.00 -> 0.75`, Protect blocked TurnAgainst, TurnAgainst lowered guest relation `0.00 -> -1.5`, Race progress reported distinct distances to core `321.6` vs `342.1` + F9 Rivals UI rows, Galaxy Core finale awarded `+250` once with EN/RU summary while game continued).
  - `rivals_space_off` (`SPORE_FIX_OFF=rivals.space`): FAIL cleanly (56 s, 0 crashes: `fix=0 active=0`, guest did not emigrate).

## History
- 2026-11-25: opened.
- 2026-11-25: implemented Space Rivals (`BUG-1550`) with R5 emigration, R7 homeworld protection, unified `rivals.h` relations/influence, Race stats, and Chronicle Galaxy Core finale; verified in ASan+UBSan unit tests and Wine lab lane 7 (`out/lab/20261125-041524`).
