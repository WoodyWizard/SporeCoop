---
id: BUG-1512
title: Cell: partner positions in the partner's own world scale for revive, regeneration and respawn during a rescale (CELL-11)
status: open
kind: bug
stage: Cell
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in:
branch: feat/w81-cell
commits: 17bc2248, e89fe984
tests: cell_partner_scale (coop_cell_audit_0717.py --part scale); twin cell_partner_scale_off
switch: cell.partner_scale
signature:
docs: docs/re/cell.md#cell-audit-0717
related: 
---

## Report
External static audit of 0.7.15.5, item CELL-11 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-uifx-cell.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-L1 (feat/w81-cell). A behaviour change gets a switch and a twin.

## Root cause
Static reading (CELL-11), confirmed in coop.cpp partners(): it returned the partner's pose in ITS world scale while puppets.cpp view_pos (BUG-1064) scales by s.scale / own; cellsync's nearest_partner compared it with the own cell (the revive hold, the regen's 12 m, the respawn spot). The scales differ for ~3 s after a level change on one game before the other (BUG-0511).

## Fix
`cell.partner_scale` (coop.cpp partners(), cell_logic::partner_scale_k): in Cell a partner's pose is brought into our world scale; any other stage and bad scales unchanged. Counter partner_scaled. Off: 0.7.16.1.

## Verification
Open, waiting for the lab run: pending night run: cell_partner_scale (PASS expected: the guest 8 m away, the host's level change: for 5 s the host's partner_dist (nearest_partner) agrees with the drawn partner within 25 %), cell_partner_scale_off (FAIL expected by the scale ratio). Unit test: audit_0717 (partner_scale_k).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-L1), branch feat/w81-cell.
- 2026-11-23: implemented on feat/w81-cell with its lab test and twin (switch cell.partner_scale), waiting for the night run.
