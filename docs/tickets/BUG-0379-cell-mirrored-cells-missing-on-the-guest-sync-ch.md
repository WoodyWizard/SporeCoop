---
id: BUG-0379
title: cell: mirrored cells missing on the guest (sync-check c missing) in cell_pause, cell_far_guest, cell_leave, campaigns
status: fixed
kind: bug
stage: Cell
reported: 2026-11-01
source: lab log gates report over recorded runs (feat/w30-lab-gates, 2026-11-01)
role: 
versions: 
fixed_in: 0.7.3.4
branch: fix/w31-cell-gate
commits: 4713a99 f906d51 f84df60 b9cde0e 3fb4045 7da457c
tests: cell_sync_check cell_pause cell_particles cell_churn
signature: sync-check: MISMATCH ... c: N missing on the guest / cell food: host X, guest Y
docs: docs/06-testing.md#testing-cell-sync-check docs/REPORTING.md#reporting-support-sync-check
related: BUG-0348 BUG-0295 BUG-0298 BUG-0364
---

## Report
`lab.py gates`: Cell c missing in 31 passed results (cell_pause 6/6, cell_far_guest 8/14, cell_leave 3/23, cell_sap 3/31, guest_editor_hold 3/3, campaign_short 12).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Per test (docs/06-testing.md#testing-cell-sync-check):
- cell_pause 6/6 (observed): the test itself removes the guest's mirrors 12 times while its pause
  menu is open; BUG-0348's fix holds new cells while the broadphase's pending list is near full, so
  the guest really lacks the host's cells until it resumes ("2 objects compared"). Made by the test,
  not a player's desync. cell_particles 1/87 and cell_churn remove mirrors the same way.
- cell_far_guest 8/14, guest_editor_hold 3/3, cell_leave 3/23: all from 2026-10-30 fix/w9-* runs
  before BUG-0295 / BUG-0298; not reproduced on 15539ad (lab 20261101-092453 and the x3 below).
- cell_sap 3/31: one run with 1332 caught faults (BUG-0337 before its fix).
- campaign_short (observed in 20261031-181517 #3, inferred for the rest): with four players the
  guest held exactly the 900 cells it was sent ("904 objects compared"), but the check required the
  900 nearest to the guest's own position while the host picks them around its view centre of the
  guest (a state late): cells near rank 900 were "missing". The check compared the wrong set.
- cell food total 5 (observed, campaign_short 20261031-103900 #3 and the lab): the host sends its
  total every 0.5 s and builds its digest a round trip after the guest's; while food comes in, both
  checks 3 s apart caught the guest one (different) meal behind. Not a desync: the totals were equal
  a moment later.

## Fix
- `diag.total_lag`: a guest total the host had within 3 s before asking counts as equal.
- `diag.cell_margin`: in a capped pool only the nearest 80 % of the 900 cells must be on the guest.
- Tests: cell_pause, cell_particles and cell_churn mark their mirror removal rounds `LAB-MARK
  sync-force`; cell_pause then asks for a check of its own after the resume (must be OK).

## Verification
cell_sync_check: FAIL with `SPORE_FIX_OFF=diag.total_lag` in lab 20261101-100218 (and with both off
in 20261101-095302): `cell food: host 340.0, guest 338.0`, 3 MISMATCH in phase A. PASS: 20261101-100440
cell_sync_check#3 (#1 and #2 failed only a precondition, how crowded the pool got, now printed
instead; their sync-checks had no MISMATCH) and 20261101-111447 cell_sync_check#1-#3 (3 of 3, gates on). `diag.cell_margin` has no failing lab run in two players (20261101-095952: the send was
capped in only a few rounds); its evidence is net_tests test_diag_compare (3 cells missing with
cell_sure 1, none with 0.8) and campaign_short 20261031-181517 #3.
cell_pause with the old test: the gate's MISMATCH in 6/6 recorded runs (e.g. 20261031-144455);
with the marks: passed in 20261101-092453 and 3 of 3 in 20261101-100440 (gates on).
cell_far_guest 3/3 and guest_editor_hold 3/3 in 20261101-100440 with the gates on (no MISMATCH):
their old `c missing` is not reproduced on 15539ad.

## History
- 2026-11-01: opened.
- 2026-11-01: analysed and fixed on fix/w31-cell-gate (check changes, test marks).
- 2026-11-02: released in 0.7.3.4.
