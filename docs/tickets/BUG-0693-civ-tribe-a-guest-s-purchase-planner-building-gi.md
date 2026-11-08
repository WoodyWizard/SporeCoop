---
id: BUG-0693
title: Civ / Tribe: a guest's purchase, planner building, gift or superweapon request is dropped without a word while the host is away from the stage
status: open
kind: bug
stage: Civilization
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
From the K2a audit of BUG-0423. civ::on_message returns at once when the host's Civ module is not active (the host in an editor, a menu, a load), so a guest's MSG_CIV_CMD (buy a vehicle, a planner building, a gift, a superweapon request) is dropped, and the guest's senders (request_buy, the planner's AddBuilding send, the gift and superweapon sends, send_cmd) never check that the host is away: the guest clicks and nothing happens, or its planner building is gone at the next reconciliation. Tribe send_cmd the same.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
