---
id: BUG-1508
title: Cell: partners' poison clouds are suppressed on the host while the host is knocked out (CELL-6)
status: open
kind: bug
stage: Cell
reported: 2026-11-23
source: audit
role: host
versions: 0.7.16.1 and earlier
fixed_in:
branch: feat/w81-cell
commits: 17bc2248, e89fe984
tests: cell_ko_partner_clouds (coop_cell_ko_partner_clouds.py); twin cell_ko_partner_clouds_off
switch: cell.ko_partner_clouds
signature:
docs: docs/re/cell.md#cell-audit-0717
related: 
---

## Report
External static audit of 0.7.15.5, item CELL-6 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-uifx-cell.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-L1 (feat/w81-cell). A behaviour change gets a switch and a twin.

## Root cause
Static reading (CELL-6), confirmed in cellsync_combat.cpp: puppet_poison lends a partner's puppet the avatar index (G+0x411C) for the game's poison step, so inside that step avatar() is the puppet. h_Cloud's BUG-0799 check `(g_downed && maker == avatar())` then refused every live partner's cloud for the 10 s of the host's own knockout (counted ko_clouds_blocked), and clouds_made counted the partners' clouds as the host's.

## Fix
`cell.ko_partner_clouds` (cellsync_combat.cpp our_avatar_cloud): a puppet maker is never "our avatar" in h_Cloud's knockout check and in clouds_made; a knocked-out partner's puppet stays refused by ko_puppet (BUG-0799). Counter partner_clouds_down (cell_audit line). Off: 0.7.16.1.

## Verification
Open, waiting for the lab run: pending night run: cell_ko_partner_clouds (PASS expected: the guest's puppet grafted with a poison part in the host's game, the host knocked out: the game's cloud maker still makes the guest's clouds over 8 s), cell_ko_partner_clouds_off (FAIL expected: none while the host is out). No unit test (one condition in a hook).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-L1), branch feat/w81-cell.
- 2026-11-23: implemented on feat/w81-cell with its lab test and twin (switch cell.ko_partner_clouds), waiting for the night run.
