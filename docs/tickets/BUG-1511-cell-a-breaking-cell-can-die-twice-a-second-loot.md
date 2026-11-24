---
id: BUG-1511
title: Cell: a breaking cell can die twice (a second loot drop) (CELL-9)
status: fixed
kind: bug
stage: Cell
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: feat/w81-cell
commits: 17bc2248, e89fe984
tests: cell_breaking_kill (coop_cell_audit_0717.py --part breaking); twin cell_breaking_kill_off
switch: cell.breaking_dying
signature:
docs: docs/re/cell.md#cell-audit-0717
related: 
---

## Report
External static audit of 0.7.15.5, item CELL-9 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-uifx-cell.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-L1 (feat/w81-cell). A behaviour change gets a switch and a twin.

## Root cause
Static reading (CELL-9), confirmed in cellsync.cpp: BreakOrSwallow sets the break animation (+0x18C 7/8/9) and posts event 5 (loot, DestroyCell) without +0x113 (docs/re/cell.md:139, :454); the MSG_CELL_HIT gate refused only kDying / kDead / players, and MSG_CELL_EAT's `alive` the same, so a guest's ACT_KILL or a type-6 chunk hit at hp 0 in the 0.1-0.3 s between the host's lethal bite and event 5 ran KillCell on the breaking cell.

## Fix
`cell.breaking_dying` (cellsync.cpp breaking(), cell_logic::breaking_state): a cell in a break / swallow state is dying for MSG_CELL_HIT (refused, traced "breaking") and for a kill of MSG_CELL_EAT (not alive). Counter breaking_refused. Off: 0.7.16.1.

## Verification
Open, waiting for the lab run: pending night run: cell_breaking_kill (PASS expected: the host breaks one creature alone (control), then another one with the guest's kill sent at once: no more meat than the control and the creature gone), cell_breaking_kill_off (FAIL expected when the second KillCell drops a second loot; the audit saw no runtime effect: if the twin passes, its run closes the ticket as `no-fail-run:`). Unit test: audit_0717 (breaking_state).
0.7.17 release lab: PASS: cell_breaking_kill (lab 20261124-020506); FAIL (fix off): cell_breaking_kill (lab 20261123-131931).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-L1), branch feat/w81-cell.
- 2026-11-23: implemented on feat/w81-cell with its lab test and twin (switch cell.breaking_dying), waiting for the night run.
- 2026-11-24: released in 0.7.17.
