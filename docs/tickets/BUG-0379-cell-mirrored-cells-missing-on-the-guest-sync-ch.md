---
id: BUG-0379
title: cell: mirrored cells missing on the guest (sync-check c missing) in cell_pause, cell_far_guest, cell_leave, campaigns
status: open
kind: bug
stage: Cell
reported: 2026-11-01
source: lab log gates report over recorded runs (feat/w30-lab-gates, 2026-11-01)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
`lab.py gates`: Cell c missing in 31 passed results (cell_pause 6/6, cell_far_guest 8/14, cell_leave 3/23, cell_sap 3/31, guest_editor_hold 3/3, campaign_short 12).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
