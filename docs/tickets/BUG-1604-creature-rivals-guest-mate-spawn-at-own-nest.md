---
id: BUG-1604
title: Creature Rivals: guest cannot summon or spawn Mate at own rival nest to open Creature Editor
status: fixed
kind: bug
stage: Creature
reported: 2026-11-27
source: Discord player report (v0.8.2.3, Smile / Szmajol, SporeCoop-report-20261127-200429.zip)
role: guest
versions: 0.8.2.3
fixed_in: 0.8.2.4
branch: main
commits: 3ccb71f1
tests: rivals_creature, make test
signature: Creature Rivals guest Mating Call never spawns a Mate at own rival nest
switch: rivals.creature_guest_mate
docs:
related:
---

## Report
A player on Discord (`Smile / Szmajol :)`, thread *"Im guest but not summoned loved animal"*, v0.8.2.3 Steam `3010a413`) reported that as a guest in Creature Rivals with their own species (`2933DDB2` / `2345`) at a rival nest, calling a mate never summoned a loved animal / Mate at their nest, preventing them from mating to open the Creature Editor.

## Root cause
1. Disassembly of `cDramaManager::Update` (`0x00D52776`) showed it calls `CreatureGame_NestHerdFor(true)` (`0x00D40AF0`) every 5 s to select or spawn the player's Mate at the player's nest. `CreatureGame_NestHerdFor` filters herds by `cSpeciesManager::PlayerSpecies()`, which on a Creature Rivals guest still returns the host's species, returning the host's nest instead of the guest's assigned rival nest.
2. Even when a Mate was picked or spawned locally on the guest by `cDramaManager::Update` (`0x00D529F4`), `npc.cpp`'s `update_filter()` and `remove_saved_strays()` did not exempt the guest's active Mate (`drama::mate()` or `+0xB58 & 0x10`), destroying it immediately.
3. `creature_rivals.cpp` `h_CursorKind` did not unconditionally preserve the native Mate (Heart) cursor (`native == 9`).

## Fix
- Added `COOP_FIX("rivals.creature_guest_mate", "BUG-1604")` in `mods/coop/creature_rivals.cpp` hooking `SporeCoop::CreatureGame_NestHerdFor` (`0x00D40AF0`) so on a Creature Rivals guest with an assigned rival nest herd (`g_nests[me].herd`), `NestHerdFor(true)` returns the guest's own rival nest herd (tracked via `mate_nest_ov=%u` in `coop.crivals`).
- Unconditionally preserved native Mate (Heart) cursor `9` in `h_CursorKind`.
- Exempted the guest's active Mate (`c == drama::mate()` or `(*(const uint32_t*)((const uint8_t*)c + 0xB58) & 0x10u) != 0`) in `mods/coop/npc.cpp` `update_filter()` and `remove_saved_strays()`.

## Verification
- PASS run: `out/lab/20261127-145438` (`rivals_creature`, PASS, 43 s, 0 faults) plus live Wine verification on lane 2 (`fixture='creature'`, guest swapped to rival nest with `mate_nest_ov=24`, pressing Mating Call immediately spawned `mate 2029BB40 flags=810` at 0.9 m from the guest's own rival nest) and full `make test`.
- no-fail-run: reproduced from Discord report logs (`SporeCoop-report-20261127-200429.zip`) and disassembly of `0x00D52776` / `0x00D40AF0`; twin switch `rivals.creature_guest_mate` controls the `NestHerdFor` hook.
