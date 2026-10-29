---
id: BUG-0126
title: coop.space empire contact leaks one x87 register per call (ApplyRelationship called as void)
status: fixed
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#105
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-space
commits: 66d7a30, 4c74ae4
tests: space_map_input
signature:
docs: docs/re/space.md#space-w3-trips-clicks-defenders
related: #105
switch: space.fpu
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/105 (full text, discussion).

`coop.space empire contact` calls `cRelationshipManager::ApplyRelationship` through `thiscall<void>`. That function returns a float in ST0. A caller declared `void` never pops ST0, so every call leaves one value on the x87 register stack. After 8 calls the stack is full: every later `fld` in the process (game code and mod code) gives the x87 "indefinite" NaN, and floats go NaN game-wide.

### Where
- The call: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L1136
- `data/symbols/overrides_922af7c7.sym` L160: `Simulator::cRelationshipManager::ApplyRelationship=0x00D06280 ; verified thiscall(mgr, polA, polB, u32 eventId, float scale) ret 0x10, float in ST0` https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/data/symbols/overrides_922af7c7.sym#L160
- `data/symbols/overrides_dc04aee5.sym` L198 (same body, delta 0): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/data/symbols/overrides_dc04aee5.sym#L198
- docs/re/space.md L1211 ("thiscall `ret 0x10`, float ST0").
- The other calls do it right with `thiscall<float>`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_diplomacy.cpp#L417 and #L940
- Used by the diplomacy test: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_space_diplomacy.py#L175

### How to reproduce
**How to reproduce / check**

On a lane in Space with NPC empires nearby:
1. Run `coop.space empire near 8` to list stars of NPC empires.
2. Run `coop.space empire contact <StarID>` 8 times (any empire star).
3. Look at the ship or any float the game shows (`coop.status` positions, the UI), or read the FPU tag word with a debugger after the command.

Expected failure: NaN positions or values. Before the 8th call you can see the x87 TOP and tag word moving by one per call.

### Expected
The command leaves the FPU state as it found it.

## Root cause
**What happens**

The x86 ABI returns floats in ST0, and the caller must pop it. With a `void` return type, clang emits no `fstp` after the call. I checked this with clang for i686-w64-windows-gnu. MSVC code (the game) and clang code both assume an empty x87 stack at function boundaries.
- 1 leaked value: 7 registers are left, which usually goes unnoticed.
- 8 leaked values: stack overflow on every load. The invalid-operation exception is masked, so the results are silently NaN (positions, health, money maths, the renderer's float code).

### Confidence
CONFIRMED by reading both sides (the call site and the `.sym` signature), and a second reviewer verified it. Low severity: only a devkit / test command uses it. A lane that runs the diplomacy test repeatedly without a restart accumulates leaked values.

## Fix
Fixed on fix/w3-space (66d7a30): `empire contact` calls ApplyRelationship as `thiscall<float>` and drops the result, as space_diplomacy.cpp does. `coop.space fpufix 0` restores the old void call (tests). New read-out `coop.space fpu` (fnstenv between frames: TOP, tag word).

Suggested in the review:

Change the call to `thiscall<float>(rel_apply, ...)` and discard the result, as `space_diplomacy.cpp` does. A grep (or a re_lint rule) for game calls declared `void` whose `.sym` comment says "ST0" would catch the next one.

## Verification
Done (2026-10-28, lane 6, GOG 2024): `coop_space_map_input.py` (suite `space_map_input`), three `empire contact` calls then `coop.space fpu`. `--without-fix` FAILS: `fpu top=0 -> top=5` after 3 calls (scratch log `map_input_nofix.log` (not kept)). Runtime finding: the tag word still read FFFF ("empty") while TOP had moved by 3, so the check looks at TOP too. With the fix PASS (top=0, scratch log `map_input_pass1.log` (not kept)).

Planned in the review: Planned test (must FAIL without the fix and PASS with it):

In `coop_space_diplomacy.py` (or a small new test), run `empire contact` 9 times, then assert a game float the other machine can see, for example the ship's position from `coop.status` being finite on both machines.
- FAILS on main: NaN after the 8th call.
- PASSES with the fix.

### Related
None of #7-#82.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #105.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-space (66d7a30): float return popped; test space_map_input FAIL without / PASS with.
- 2026-10-28: fix/w3-space lab runs on lane 6: 20261028-010933 (14/14: the four new wave 3 Space tests and space_map, space_stale, space_fleet, space_puppet, space_rivalry, space_contracts, space_jump, space_guest_ui, space_allies, space_diplomacy), smoke 20261028-013951 (13/13; `ui` skipped: exclusive, needs a lane 1-5).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: log paths of an agent's scratch folder replaced by the log names (BUG-0215: no paths outside the repository).
- 2026-10-28: switch: space.fpu (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
