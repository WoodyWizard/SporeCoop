---
id: BUG-1084
title: Tribe: 3 caught faults on a guest at 0xC23469 (Citizen_HoldBundle, null bundle) while the host is in the Tribe end's City Hall editor
status: open
kind: crash
stage: Tribe
reported: 2026-11-13
source: lab
role: guest
versions: rel/0.7.14
fixed_in:
branch:
commits:
tests: campaign_tribe_end (lab fault check)
signature: 0xC0000005 at 0x00C23469 SporeCoop::Citizen_HoldBundle+0x9 (data 0x34, edi 0) < Citizen_SetHandheldItem+0xF8D < BT_HealTick+0x30FD
docs:
related: BUG-0684, BUG-1076
---

## Report
fix/w49-camp, campaign_tribe_end lab 20261113-142311 (lane 5): the Tribe end the players' way reached Civ, the lab failed
the run on Dave's (extra2, a guest) caught faults: 3 at 14:48:53, while the host was in the Tribe end's City Hall /
vehicle editors (Dave's trace: host_away=1, Tribe). `exception 0xC0000005 at 0x00C23469 SporeCoop::Citizen_HoldBundle+0x9
(data 0x00000034) ... edi=00000000`, called from Citizen_SetHandheldItem+0xF8D < +0xFF2 < ? < BT_HealTick+0x30FD:
BUG-0684's signature (fixed for Civ: a gather node's null bundle), now in Tribe. The game survived (caught).
Reproduce: `lab.py run campaign_tribe_end`; the faults come in the guest's Tribe while the host edits.

## Root cause
Not analysed (outside fix/w49-camp's package). Inferred: the same null bundle as BUG-0684, in the guest's own Tribe
simulation while the host's game stands in its editor (no snapshots of members then).
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
