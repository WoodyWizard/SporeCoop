---
id: BUG-0143
title: 43 game symbols the code uses (several hooked) have no checked signature on any build
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#122
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-sys
commits: 1022c9b
tests: make test (tools/re_lint.py)
signature:
docs: data/symbols/overrides_dc04aee5.sym
related: #122
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/122 (full text, discussion).

`overrides_dc04aee5.sym` and docs/05-re-notes.md 5.1.1 say that every symbol the mod uses was ported and checked in the GOG 2024 disassembly (prologue, `ret N`, callers). 43 of the game symbols the code asks for are not in either overrides file. They come only from the generated `march2017_dc04aee5.sym` (ModAPI names: an address and "high"/"medium", no convention, argument count or return type). Several of them are hooked, so a wrong detour signature would corrupt the stack. `tools/re_lint.py` checks only that a name exists, so nothing flags these.

### Where
- Claims:
  - https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/data/symbols/overrides_dc04aee5.sym#L6-L9
  - https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/05-re-notes.md#L22-L23
- re_lint checks presence only: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/re_lint.py#L31-L44
- Hooked without a checked signature:
  - `App::cAppSystem::PreInit`, `App::cAppSystem::Init`, `App::cSporeApp::Init`, `App::cSporeApp::Shutdown`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/gamehooks.cpp#L79-L82
  - `Simulator::cCreatureGameData::AddEvolutionPoints`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L310-L311
  - `Simulator::Cell::ShouldNotAttack`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L902-L903
  - `Simulator::Cell::GetModelKeyForCellResource`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/modes.cpp#L428-L431
  - `UTFWin::UILayout::Load`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/sui.cpp#L52-L53

### How to reproduce
**How to reproduce / check**

List the names the code uses that are in `march2017_dc04aee5.sym` but in neither overrides file. A short script over `mods/**/*.cpp` and `sporekit/*.cpp` for quoted symbol names gives exactly the 43 above.

### Expected
As the files and docs claim, every symbol the code uses has an overrides entry with a confidence word and its convention, `ret N` and return type, checked in the GOG 2024 disassembly.

## Root cause
**All 43 names**

The first place the code uses each name is shown. Every one is present only in `march2017_dc04aee5.sym`, and none is in `overrides_dc04aee5.sym` or `overrides_922af7c7.sym`.

| Symbol | First use |
|---|---|
| App::ICheatManager::Get | sporekit/cheats.cpp:28 |
| App::IGameModeManager::Get | mods/coop/modes.cpp:619 |
| App::Property::SetValueVector3 | mods/coop/tribe.cpp:361 |
| App::cAppSystem::Init (hooked) | sporekit/gamehooks.cpp:80 |
| App::cAppSystem::PreInit (hooked) | sporekit/gamehooks.cpp:79 |
| App::cSporeApp::Init (hooked) | sporekit/gamehooks.cpp:81 |
| App::cSporeApp::Shutdown (hooked) | sporekit/gamehooks.cpp:82 |
| App::cViewer::GetCameraToPoint | mods/coop/modes.cpp:619 |
| EditorRequest_ctor | mods/coop/coop.cpp:560 |
| Editors::EditorRequest::Submit (hooked) | mods/coop/coop.cpp:560 |
| GeneralAllocator::new_ | mods/coop/coop.cpp:565 |
| Pollinator::cAchievementsManager::Get | mods/coop/space_finale.cpp:831 |
| Sim_StarManager_GetEmpire | mods/coop/space_allies.cpp:1271 |
| Simulator::Cell::CreateCellObject | mods/coop/modes.cpp:439 |
| Simulator::Cell::GetCurrentAdvectInfo | mods/coop/modes.cpp:469 |
| Simulator::Cell::GetDamageAmount | mods/coop/cellsync.cpp:868 |
| Simulator::Cell::GetModelKeyForCellResource (hooked) | mods/coop/modes.cpp:429 |
| Simulator::Cell::ShouldNotAttack (hooked) | mods/coop/cellsync.cpp:873 |
| Simulator::Cell::cCellDataReference_::Create | mods/coop/cellsync.cpp:883 |
| Simulator::GetCachedColorFromId | mods/coop/space_allies.cpp:1296 |
| Simulator::GetCurrentContext | mods/coop/world.cpp:334 |
| Simulator::GetPlayerEmpire | mods/coop/space_rivalry.cpp:875 |
| Simulator::GetPlayerEmpireID | mods/coop/space_rivalry.cpp:876 |
| Simulator::SpawnUFO | mods/coop/squadron.cpp:168 |
| Simulator::cCreatureGameData::AddEvolutionPoints (hooked) | mods/coop/game.cpp:310 |
| Simulator::cCreatureGameData::GetEvolutionPoints | mods/coop/game.cpp:613 |
| Simulator::cGameNounManager::GetData | mods/coop/space_allies.cpp:1298 |
| Simulator::cGamePersistenceManager::Get | mods/coop/world.cpp:936 |
| Simulator::cGameTimeManager::Get | mods/coop/space_finale.cpp:835 |
| Simulator::cSimulatorPlayerUFO::Get | mods/coop/space_rivalry.cpp:874 |
| Simulator::cSpaceTrading::Get | mods/coop/space_trade.cpp:1090 |
| Simulator::cStarManager::Get | mods/coop/space_contracts.cpp:963 |
| Simulator::cStarManager::GetEmpireForStar ("medium") | mods/coop/spacecoop.cpp:1074 |
| Simulator::cStarManager::GetPlanetRecord | mods/coop/spacecoop.cpp:1069 |
| Simulator::cTerraformingManager::Get | mods/coop/spacecoop.cpp:925 |
| UTFWin::GetAllocator | mods/coop/sui.cpp:49 |
| UTFWin::ITextEdit::Create | mods/coop/sui.cpp:43 |
| UTFWin::IWindowManager::Get | mods/coop/sui.cpp:41 |
| UTFWin::Image::GetImage | mods/coop/sui.cpp:47 |
| UTFWin::UILayout::GetContainerWindow | mods/coop/sui.cpp:46 |
| UTFWin::UILayout::Load (hooked) | mods/coop/sui.cpp:45 |
| UTFWin::UTFWinObject::new_ | mods/coop/sui.cpp:48 |
| wstring_copy | mods/coop/space_allies.cpp:1292 |

**What happens**

Nothing is known to be wrong today. The risk is that a typed call or a detour relies on a calling convention, argument count or return type that nobody checked. A missing argument corrupts the stack far from the call (CLAUDE.md section 2). Examples where the exact ABI matters:
- `ITextEdit::Create` is called as `stdcall(int, void*)`. Only `IButton::Create` is verified as `ret 8`.
- `SpawnUFO(type, &pol, key)`, `CreateCellObject` (10 arguments), `EditorRequest_ctor`/`Submit`, `GetEmpireForStar` (a "medium" name match), `GeneralAllocator::new_` (6 arguments).
- Every hooked one above: the detour must match exactly.

### Confidence
CONFIRMED (the list was computed from the files), and a second reviewer verified it. Low severity: GOG 2024 is ModAPI's own March2017 layout, and the hooks run on every lane every day, so gross mismatches would show. The finding is about unchecked claims.

## Fix
Fixed on fix/w3-sys (1022c9b). On today's code 39 names (4 of the 43 were ported since: the
cAppSystem / cSporeApp hooks) were used from `march2017_dc04aee5.sym` only. Each was checked in the
GOG 2024 exe with llvm-objdump (prologue, the ret and ret N, the stack offsets read, how ecx is used)
against every call and detour of the code and added to `overrides_dc04aee5.sym` (31 verified, 8
likely: CreateCellObject, GetCurrentAdvectInfo, GetDamageAmount, SpawnUFO, cGameNounManager::GetData,
GetEmpireForStar, Image::GetImage, UTFWinObject::new_: the ret and the argument count match, the
types come from the callers). Every hooked one (AddEvolutionPoints, ShouldNotAttack,
GetModelKeyForCellResource, UILayout::Load, EditorRequest::Submit) matches its detour. No mismatch
found. `tools/re_lint.py` now fails when a used name of a supported build is only in a generated
file. Not done: re_lint does not parse the convention / ret N out of the note (free text).

## Verification
Test: `make test` (re_lint).
* FAIL on main's symbol files with the new re_lint: 39 problems ("unchecked symbol ...").
* PASS on fix/w3-sys: 0 problems.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #122.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-sys (1022c9b); tests named above, lab run out/lab/20261028-011605 (smoke and diag: out/lab/20261028-012842).
- 2026-10-28: released in 0.7.3.0.
