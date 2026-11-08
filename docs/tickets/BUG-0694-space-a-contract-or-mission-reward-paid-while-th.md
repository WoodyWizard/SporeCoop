---
id: BUG-0694
title: Space: a contract or mission reward paid while the player is outside Space (no empire, no inventory) is lost
status: open
kind: bug
stage: Space
reported: 2026-11-07
source: K2a audit of BUG-0423 (0.7.11)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0423
---

## Report
From the K2a audit of BUG-0423. space_contracts.cpp pays a contract result (K_RESULT) by setting g_rewarded first and then add_money / give_tool, which do nothing without an empire / an inventory (the player outside Space: in the galaxy menu, an editor, a load); spacecoop.cpp K_MISSION skips each tool without an inventory and still shows the notice. The reward is consumed and lost.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
