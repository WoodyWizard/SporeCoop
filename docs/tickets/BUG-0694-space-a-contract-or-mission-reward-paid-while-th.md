---
id: BUG-0694
title: Space: a contract or mission reward paid while the player is outside Space (no empire, no inventory) is lost
status: fixed
kind: bug
stage: Space
reported: 2026-11-07
source: K2a audit of BUG-0423 (0.7.11)
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2a-state-b
commits: 1391b0ac 149ac2b6
tests: space_reward_away
signature:
docs: docs/re/host-centric.md#host-centric-away-credits
related: BUG-0423
---

## Report
From the K2a audit of BUG-0423. space_contracts.cpp pays a contract result (K_RESULT) by setting g_rewarded first and then add_money / give_tool, which do nothing without an empire / an inventory (the player outside Space: in the galaxy menu, an editor, a load); spacecoop.cpp K_MISSION skips each tool without an inventory and still shows the notice. The reward is consumed and lost.

## Root cause
Code (K2a audit), confirmed in the lab: on_result spent the contract's id first; give_tool / add_money did nothing without an inventory / empire; spacecoop K_MISSION skipped tools without an inventory.

## Fix
`space.reward_owed`: what cannot be paid now is owed (money, tool by hash or key) and paid in the first Space frame of the same Space world (space_contracts.cpp owed_tick, spaceext::owe_tool_key for missions). The contracts' inventory() now follows the `coop.space away` test hook as spacecoop's does.

## Verification
* FAIL with `SPORE_FIX_OFF=space.reward_owed`: out/lab/20261107-230154.
* PASS: out/lab/20261107-225634 (out/lab/20261107-224108 and out/lab/20261107-225307 passed before the inventory followed the away hook: they do not count).

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state-b): fixed.
