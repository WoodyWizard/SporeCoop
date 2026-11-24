---
id: BUG-1431
title: CELL-1: Cell: the host acknowledges a guest's meal before its food is applied, the guest's food dips for 0.5 s (BUG-0051 again)
status: fixed
kind: desync
stage: Cell
reported: 2026-11-22
source: audit
role: guest
versions: 0.7.16.1
fixed_in: 0.7.16.1
branch: fix/w63-life
commits: 75d69f4a, 39f2f060
tests: cell_food_ack (coop_cell_food_ack.py), cell_food_ack_shared; twin cell_food_ack_off
signature:
docs: docs/06-testing.md#testing-w63-life-knobs
related: BUG-0051, BUG-0633, BUG-0239
---

## Report
External static audit of 0.7.15.5, item CELL-1 (out/research/ext-audit-verify-uifx-cell.md): a regression of BUG-0051 by BUG-0633's cell.food_in_step: a guest's food dips by one meal for up to 0.5 s (over a level line: a level down and up). About 3 % of a guest's meals at 60 fps, 6 % at 30 fps, every meal while the host's cell step does not run (a pause under 0.5 s).

## Root cause
Inferred from code (cellsync.cpp frame_receive, cellsync_host.cpp host_tick, cell_logic.h Meals::diff): the host set `g_food_ack[from]` when it read the guest's MSG_CELL_EAT / MSG_CELL_FOOD, while the food itself was only queued for the next cell step (`food_in_step`). host_tick's total of that frame (every 0.5 s) went out without the meal and with its number; the guest popped every unacked meal up to it and applied host_total + on_way - local = -meal; the next total gave it back.

## Fix
The acknowledgement goes through the same queue as the food, after it (`ack_meal`): it becomes visible only with the food. A queued ack of a player who left meanwhile is skipped. Switch `coop.fix cell.ack_in_step off`: at once as 0.7.16. Test knob `coop.pool foodhold <secs>` (host): the food queue waits up to 2 s, as while the cell step stands. No message changed.

## Verification
Open, waiting for the lab run. PASS expected: cell_food_ack and cell_food_ack_shared (three rounds: the host's queue held 1.5 s, the guest eats 10: its food read every ~0.1 s never falls below the meal, then the host's total has it). FAIL expected: cell_food_ack_off (SPORE_FIX_OFF=cell.ack_in_step: a dip of 10 for about a second in every round). The Cell food total is the party's in both DNA modes (only the spent count is per player), so own and shared DNA behave alike.

- 2026-11-23 (0.7.16.1 final check): cell_food_ack and cell_food_ack_shared PASS in 20261123-152921 (aad722ac).

## History
- 2026-11-23: closed on the 0.7.16.1 final check lab evidence.
- 2026-11-22: opened (audit item); fixed on fix/w63-life with its test and twin, waiting for the lab run.

- 2026-11-22: renumbered from BUG-9316 at the 0.7.16.1 merge (the id came from the local fork branches pr-*); commits before the merge name BUG-9316.
- 2026-11-23: released in 0.7.16.1.
