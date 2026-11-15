---
id: BUG-0693
title: Civ / Tribe: a guest's purchase, planner building, gift or superweapon request is dropped without a word while the host is away from the stage
status: fixed
kind: bug
stage: Civilization
reported: 2026-11-07
source: K2a audit of BUG-0423 (0.7.11)
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2a-state-b
commits: f1d45292 1391b0ac
tests: civ_away_buy
signature:
docs: docs/re/host-centric.md#host-centric-away-credits
related: BUG-0423
---

## Report
From the K2a audit of BUG-0423. civ::on_message returns at once when the host's Civ module is not active (the host in an editor, a menu, a load), so a guest's MSG_CIV_CMD (buy a vehicle, a planner building, a gift, a superweapon request) is dropped, and the guest's senders (request_buy, the planner's AddBuilding send, the gift and superweapon sends, send_cmd) never check that the host is away: the guest clicks and nothing happens, or its planner building is gone at the next reconciliation. Tribe send_cmd the same.

## Root cause
Observed: civ::on_message / tribe::on_message returned when the host's game was not in the stage: MSG_CIV_CMD / MSG_TRIBE_CMD dropped. A command replayed in the first frame after a load is rejected (`rejected command kind 2`, the city list not built yet, out/lab/20261107-223030).

## Fix
`civ.cmd_hold`, `tribe.cmd_hold`: net/held_cmds.h holds the commands (32, 120 s) and runs them 3 s after the host is back in the same save (modes::current_world); the guest is told its command waits. Unit test: tests/validate_tests.cpp test_held_cmds.

## Verification
* FAIL with `SPORE_FIX_OFF=civ.cmd_hold`: out/lab/20261107-225053.
* PASS: out/lab/20261107-224108, out/lab/20261107-225634.

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state-b): fixed.
- 2026-11-08: released in 0.7.11.
