---
id: BUG-1037
title: Space: a mission reward tool owed while outside Space is never paid while the host's session has Space contracts off
status: open
kind: bug
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-10
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-owed-tool-contracts-off
related: BUG-0694, BUG-0768
---

## Report
Code audit for 0.7.14, finding SPC-10. Not seen at runtime, found by code reading.
A partner's mission reward tool (K_MISSION) that this game cannot take now (outside Space, no inventory) is owed (BUG-0694) in space_contracts' list, but the list is paid only by the contracts feature's tick. With the host's session setting Space contracts off (F3, BUG-0768) the owed tool is never given.

## Root cause
Observed in code (main efd2e0ba), confidence high:
* spacecoop.cpp:1611 K_MISSION: spaceext::owe_tool_key (space_contracts.cpp:1526-1535) pushes into space_contracts' g_owed.
* g_owed is paid only by owed_tick, called from space_contracts' tick (space_contracts.cpp:1204-1207).
* spaceext::tick_all (space_ext.cpp:225-232) skips a feature that is not live (space_ext.cpp:223, feature_allowed: the host's setting kSpaceContracts).
Player reach: the host has Contracts off, a shared mission ends while the guest is in an editor or on the way out of Space. Rare.

## Fix idea
owed_tick is called from spacecoop's own tick (or from tick_all regardless of the feature switch), so owed rewards do not depend on the contracts setting. Switch: none needed (or `coop.fix space.owed_always off`).

## How to test
New small case in coop_space_contracts.py: settings Space contracts off, `coop.space contracts owe` style test command (the existing debug `result given; owed` path at space_contracts.cpp:1490) while the guest is in an editor, back to Space; assert the guest game's inventory has the tool. Main: FAIL.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
