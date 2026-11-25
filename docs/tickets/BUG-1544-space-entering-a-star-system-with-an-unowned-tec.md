---
id: BUG-1544
title: Space: entering a star system with an unowned tech-5 planet crashes in cEmpire::IsContactedByPlayer (0xC30A70, null this)
status: fixed
kind: crash
stage: Space
reported: 2026-11-24
source: GitHub #222
role: both
versions: 0.7.15.5, 0.7.16.1, 0.7.17.1
fixed_in: 0.7.99.1
branch: rel/0.7.99.1
commits: 68e4bf4d
tests: space_null_empire, space_null_empire_off
switch: space.null_empire_contact
signature: 0x00C30A70
docs: docs/re/space.md#space-null-empire-contact
related:
---

## Report
Entering a specific star system in Space (both host and guest, and also offline after reloading the save) crashed immediately with an unhandled access violation reading `0x00000084` at rebased `0x00C30A70` (`SporeApp.exe`, Steam build `3010a413`).

## Root cause
Observed in the crash report, minidump, and disassembly of `SporeApp.exe`:
- The planet record (`mTechLevel == 5` at `+0x194`) belonged to a star whose `StarManager::GetEmpire(star->mEmpireID)` returned `nullptr` (either `mEmpireID == 0xFFFFFFFF` or a missing empire).
- `Simulator::GetMainSpeciesImageKey` (`0x01065F10`, called from `UI::SpaceGameUI::FillStarTooltipPlanetInfo` during star system entry) resolves `star->GetEmpireID()` -> `StarManager::GetEmpire(id)` and passes the result straight to `SporeCoop::cEmpire_IsContactedByPlayer` (`0x00C30A70`) with no null check, faulting at `mov ecx, [ecx+0x84]` (`0x00C30A70`). Eight instructions later (`0x0106D171..0x0106D185`), the game itself checks both `empireID != 0xFFFFFFFF` and `GetEmpire != nullptr` for tech-5 planets.
- Observed in Wine lab verification: after surviving `0x00C30A70`, `SporeCoop::SpaceGameUI_Update` (`0x010743D0`, nearest March2017 label `UI::SpaceGameUI::Load+0xE0A`) hits a second null-empire dereference every frame at `0x0107453A` reading `[eax+0x84]` after `cPlanet::GetEmpire()` (`0x00C71FD0`) returns `nullptr` for the active tech-5 planet.

## Fix
Added `space.null_empire_contact` in `mods/coop/gamefixes.cpp`:
1. Hooked `SporeCoop::cEmpire_IsContactedByPlayer` (`0x00C30A70`) so `this == nullptr` returns `false` (letting `GetMainSpeciesImageKey` fall back to the standard unknown-species icon and `0x00AE98E1` pick the uncontacted string).
2. Hooked `SporeCoop::cPlanet_IsPlayerOwned` (`0x00C72510`) so when called from `SporeCoop::SpaceGameUI_Update` (`0x010743D0..+0x200`) on a planet whose `cPlanet::GetEmpire()` (`0x00C71FD0`) is `nullptr`, it returns `true`, branching to `0x0107454E` to hide the NPC empire HUD badge (`0x3FEA475`) instead of dereferencing `nullptr+0x84`.
Switch: `coop.fix space.null_empire_contact off` restores the unguarded calls for tests.

## Verification
- FAIL (`space_null_empire_off`, `SPORE_FIX_OFF=space.null_empire_contact`): `out/lab/20261124-175943` (probe faulted at `0xC30A70`, 253 host faults).
- PASS (`space_null_empire`): `out/lab/20261124-175943` (`guarded=4`, `ui_guarded=94`, 0 host faults, 0 guest faults).

## History
- 2026-11-24: opened from GitHub #222.
- 2026-11-24: fixed in `68e4bf4d` on `rel/0.7.99.1`, verified ON PASS + OFF FAIL in `20261124-175943`.
- 2026-11-24: released in 0.7.99.1.
