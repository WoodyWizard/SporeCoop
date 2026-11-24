---
id: BUG-1513
title: Cell: a guest's own poison clouds hurt it with friendly fire or Rivals on (CELL-13)
status: fixed
kind: bug
stage: Cell
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: feat/w81-cell
commits: 17bc2248, e89fe984
tests: cell_own_cloud (coop_cell_audit_0717.py --part own_cloud); twin cell_own_cloud_off
switch: cell.own_cloud
signature:
docs: docs/re/cell.md#cell-audit-0717
related: 
---

## Report
External static audit of 0.7.15.5, item CELL-13 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-uifx-cell.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-L1 (feat/w81-cell). A behaviour change gets a switch and a twin.

## Root cause
Static reading (CELL-13), confirmed in cellsync_host.cpp / cellsync_combat.cpp: the host makes a guest's clouds (puppet_poison, +0x110) and mirrors them to every guest, the owner too, as flag 4; the owner's DamageCell hook refused a product cloud on its own cell only while players do not hurt each other, so with all.friendly_fire on (default off) or Rivals a guest's own clouds' mirrors hurt and knocked out its own cell (its own local cloud is culled at once).

## Fix
`cell.own_cloud`: the host notes each partner cloud's player (g_partner_clouds: index -> id) and sets state flag 8 for the clouds of the guest it sends to (write_cell per destination, cell_logic::state_flags; an older guest reads only 1/2/4); the guest never takes damage from a mirror flagged its own. Counter own_cloud_refused. Off: 0.7.16.1. Wire: a new bit of MSG_CELL_STATE's flags (cell family, no lock change; docs/04-netcode.md).

## Verification
Open, waiting for the lab run: pending night run: cell_own_cloud (PASS expected: friendly fire on, the guest's puppet grafted with poison in the host's game, the guest put onto its own clouds' mirrors 6+ times: hp never lower, never knocked out), cell_own_cloud_off (FAIL expected). Unit test: audit_0717 (state_flags).
0.7.17 release lab: PASS: cell_own_cloud (lab 20261124-020506); FAIL (fix off): cell_own_cloud_off (lab 20261124-013553).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-L1), branch feat/w81-cell.
- 2026-11-23: implemented on feat/w81-cell with its lab test and twin (switch cell.own_cloud), waiting for the night run.
- 2026-11-24: released in 0.7.17.
