---
id: BUG-1602
title: Civilization: null UILayout dereference crash in UTFWin::UILayout::FindWindowByID (0x00810658) on City Planner Anthem button
status: fixed
kind: crash
stage: Civ
reported: 2026-11-27
source: AI playtest (v0.8.2.3, Civ Rivals City Planner)
role: both
versions: 0.8.2.3
fixed_in: 0.8.2.4
branch: main
commits: 634d50f2
tests: rivals_civ_spec_sw, make test
signature: 0xC0000005 read at 0x00000018 in UTFWin::UILayout::FindWindowByID (0x00810658)
switch: game.uilayout_find_null
docs:
related:
---

## Report
Clicking the Anthem / Music Editor button inside the Civilization City Planner crashed Spore with `0xC0000005` reading address `0x00000018` at `0x00810658` (`UTFWin::UILayout::FindWindowByID`).

## Root cause
`UTFWin::UILayout::FindWindowByID` (`0x00810650`, `__thiscall void*(UILayout*, uint32_t id, bool recursive)`) immediately dereferenced `this + 0x18` without checking whether `this` was null or unreadable when the City Planner Anthem transition tore down or failed to initialize its layout.

## Fix
- Added symbol `UTFWin::UILayout::FindWindowByID` to `data/symbols/overrides_dc04aee5.sym`.
- Added `COOP_FIX("game.uilayout_find_null", "BUG-1602")` in `mods/coop/gamefixes.cpp` hooking `UTFWin::UILayout::FindWindowByID` (`0x00810650`) to return `nullptr` cleanly when `layout` is null or unreadable, plus a `coop.uilayoutguard [probe]` diagnostic command.

## Verification
- PASS run: `out/lab/20261127-001053` (`rivals_civ_spec_sw`, PASS) and live `coop.uilayoutguard probe` verification confirming null/unreadable `UILayout*` returns `nullptr` with 0 crashes; full `make test` PASS.
- no-fail-run: crash reproduced in AI playtest on Civ City Planner Anthem button; twin switch `game.uilayout_find_null` guards the exact `0x00810658` dereference.
