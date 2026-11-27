---
id: BUG-1589
title: Civilization: guest crashes during world reset in cCity::GetBoundingRadius (+0x25C) when mpCityWalls is null
status: fixed
kind: crash
stage: Civ
reported: 2026-11-26
source: Discord player crash report (SporeCoop-report-20261127-061420.zip, v0.8.2.2)
role: guest
versions: 0.8.2.2
fixed_in: 0.8.2.3
branch: main
commits: 344117cf
tests: rivals_civ, civ_planner, make test
signature: 0xC0000005 read of 0x0000025C at cCity::GetBoundingRadius+6 (GOG 0x00BD95C6, Steam 0x00E395C6) with eax=0 after GalaxyMenu_SimReset
switch: game.city_walls_null
docs: docs/re/civ.md
related: BUG-1427, BUG-1576
---

## Report
Player crash report on Discord (`SporeCoop-report-20261127-061420.zip`, `v0.8.2.2`, Steam `3010a413` build): Guest in Civilization stage left to galaxy menu to load host's Civ world (`Капта.spo`). Immediately after `GalaxyMenu_SimReset` tore down the world (`06:14:19.279`), the game crashed 18 ms later (`06:14:19.297`) with `0xC0000005` reading `0x0000025C` (`eax = 0`) at `0x00E395C6` (labeled `SporeCoop::cCity_ShowSpecialtyBox+0x296` = GOG `0x00BD95C6`) via `UTFWin::cWindowManager::ProcessMessage` -> `0x00B7CC70` -> `0x00B7CCC3`.

## Root cause
- Disassembly of GOG 2024 `SporeApp.exe` (`dc04aee5`) at `0x00BD95C0` (`cCity` spatial vtable `0x014686A0` slot `+0x70` = `cCity::GetBoundingRadius`, `this = city + 0x120`):
  - `0x00BD95C0: mov eax, [ecx+0x204]` (reads `cCity::mpCityWalls` at `city + 0x324`)
  - `0x00BD95C6: fld dword ptr [eax+0x25C]` (no null check!)
  - `0x00BD95CC: ret`
- During Civ world reset / leaving to the galaxy menu, `GalaxyMenu_SimReset` destroys `cCityWalls` and nulls `cCity::mpCityWalls`, while a UTFWin rollover/modal teardown callback (`0x00B7CC70`) still holds a `cCity*` spatial pointer and calls `spatial->GetBoundingRadius()` (whose return value it immediately pops with `fstp st(0)` at `0x00B7CCC5`).

## Fix
- `data/symbols/overrides_dc04aee5.sym`: added verified symbol `SporeCoop::cCity_GetBoundingRadius=0x00BD95C0`.
- `mods/coop/gamefixes.cpp` (`game.city_walls_null`): hooked `cCity::GetBoundingRadius` with `city_radius_guard`, checking `readable(city_spatial + 0x204, 4)` and `walls && readable(walls + 0x25C, 4)` and returning `0.0f` when `mpCityWalls` is null or unreadable; added `coop.cityradius [probe]` console command and `status()` counter.

## Verification
- PASS runs: `out/lab/20261126-212912` (`rivals_civ`: PASS, 0 faults) and `out/lab/20261126-213147` (`civ_planner`: PASS, 0 faults) plus `make test` (all 10 suites including `re_lint`, `static_guard_lint`, `tickets.py check`).
- no-fail-run: crash occurs on a race between `GalaxyMenu_SimReset` nulling `cCity::mpCityWalls` and a pending UTFWin rollover callback; exact faulting instruction verified from `crash-20261127-061419.txt` (`eax=0`, read `0x0000025C` at `0x00BD95C6`) and probeable via `coop.cityradius probe`.
