---
id: BUG-1586
title: Tribe Rivals: guest crashes when selling a tribe tool hut because h_CommunityEditorSell called vtable +0x0C instead of cSpatialObject::Cast (+0xB8)
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-26
source: Discord player crash report (SporeCoop-report-20261127-051014.zip, v0.8.2)
role: guest
versions: 0.8.2, 0.8.2.1
fixed_in: 0.8.2.2
branch: main
commits: ef9e4658
tests: tribe_rivals_tests, rivals_tribe_wipe, rivals_tribe
signature: 0xC0000005 in CommunityEditor_Drop+0x89 / Simulator::cTribe::GetToolByType+0x27 after guest sells tribe building
docs: docs/re/tribe.md
related: BUG-1580, BUG-1532
---

## Report
Player crash report on Discord (`SporeCoop-report-20261127-051014.zip`, `v0.8.2`, Steam GOG-2024 build): whenever the Guest sold a building (tool hut) in Tribe Rivals, the Guest crashed (`crash-20261127-050826.txt` in `CommunityEditor_Drop+0x89` -> `GeneralAllocator_FreeInternal` and `crash-20261127-051013.txt` in `Simulator::cTribe::GetToolByType+0x27` with `BUG-0359 tripwire: non-exe vtable` on freed `cTribeTool` still referenced by guest tribe `+0x36C`).

## Root cause
- In `SporeCoop::CommunityEditor_Sell` (`0x00D09D70`), the second parameter `spatial` is a `cSpatialObject*`, whose `Cast(uint32_t typeId)` method is at vtable offset **`+0xB8`** (`0x0116D858` = `cTribeTool`, verified in disassembly at `0x00D09D85..0x00D09DBC`, and matching `player_rules.cpp:614` / `civ.cpp:194`).
- `BUG-1580` added `h_CommunityEditorSell(editor, spatial)` in `mods/coop/tribe_rivals.cpp` calling `vcall<void*>(spatial, 0xC / 4, 0x17F243Bu)` (the `Object`/`cGameData` `Cast` offset instead of `cSpatialObject`'s `+0xB8`). Calling vtable `+0x0C` on `cSpatialObject*` invoked the wrong virtual method, corrupting/freeing the `cTribeTool` and leaving a dangling freed pointer inside `cTribe +0x36C`, immediately crashing the Guest.

## Fix
- `mods/coop/tribe_rivals_logic.h` & `mods/coop/tribe_rivals.cpp` (`rivals.tribe_sell_cast`):
  - Replaced the wrong `0xC / 4` vcall in `h_CommunityEditorSell` with `vcall<void*>(spatial, kSpatialCastVtableSlot, kTribeToolTypeId)` (`0xB8 / 4`, `0x0116D858u`), matching `CommunityEditor_Sell`'s own cast.
  - Added guest diagnostic counter (`g_tools_sold++`) and `coop.tribe rivals test_sell <kind>` command to exercise `h_CommunityEditorSell` on a live `cTribeTool`'s `cSpatialObject*` and probe `cTribe::mTools`.
- `tests/tribe_rivals_tests.cpp` & `tools/tests/coop_rivals_tribe_wipe.py`: added unit check for `community_sell_cast_slot` and live Wine lab Phase 4 test running `test_sell 1` on Guest.

## Verification
- FAIL run (reproduces exact guest crash with `SPORE_FIX_OFF=rivals.tribe_sell_cast`): `20261126-202332:rivals_tribe_wipe` (`crash-20261126-202410-i2.dmp` + `.txt` on Guest instance 2).
- PASS runs (fix ON): `20261126-202421:rivals_tribe_wipe,rivals_tribe` (2/2 PASS, 0 faults, guest `tools_sold` incremented cleanly), plus `make test` (all 10 suites).
