---
id: BUG-1037
title: Space: a mission reward tool owed while outside Space is never paid while the host's session has Space contracts off
status: fixed
kind: bug
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-10
role: guest
versions: 
fixed_in: unreleased
branch: fix/w49-space2
commits: daac928c
tests: space_owed_contracts_off
signature:
docs: docs/re/space.md#space-restore-window, docs/known-issues.md#issue-owed-tool-contracts-off
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
spaceext::Feature has `tick_off` (every frame while the host's session settings switch the feature off);
space_contracts pays its owed rewards (BUG-0694, also spacecoop K_MISSION tools) there. `coop.space contracts owed`
prints the counts. Switch `coop.fix space.owed_always off` (0.7.13).

## Verification
`coop_space_owed_contracts_off.py` (suite `space_owed_contracts_off`, fixture space, ~1 min): space.contracts off by
the host (settings force), the guest away, a reward tool owed (`contracts result`), back in Space: the guest game's
inventory HasTool.
* FAIL (SPORE_FIX_OFF=space.owed_always): out/lab/20261113-022716 (no tool)
* PASS: out/lab/20261113-022655, out/lab/20261113-023651-2

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-space2 (owed rewards paid also while contracts are off).
