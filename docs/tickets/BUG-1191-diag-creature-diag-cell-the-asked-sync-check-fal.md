---
id: BUG-1191
title: diag_creature, diag_cell: the asked sync-check falls into the 20 s share-quiet window, the next one comes after the test's 25 s wait
status: open
kind: test
stage: Tools
reported: 2026-11-16
source: final check 20261116-025154
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
The 0.7.15 final check (lab 20261116-025154): diag_creature (lane 3, warm reset 16 s) and diag_cell (lane 5, warm
reset 19 s) both "FAIL the host logs a sync-check OK in <stage>", while the guest's OK came and the host's log had
"sync-check: OK with player 1 'Bob' (Creature ...)" when the test printed its lines right after (`coop.diag`:
"checks 2 ok 1 differs 0 mismatch 0").

## Root cause
Observed: coop_diag.py asks one check (`coop.diag sync now`) and waits 25 s for the host's OK. The host does not
compare a guest within 20 s after it asked for a world share with it (BUG-0754 `diag.share_quiet`, kShareQuiet); such a
check counts (`checks 2`) but is "plays elsewhere, not compared" (logged only at log_detail 2), and the next one comes
`g_sync_interval` = 30 s later. The guest's game log of diag_creature: the share's load at s 9.4, the guest's OK at s
41.7, about 30 s after the asked check: past the test's 25 s. Inferred: with A9's faster warm reset (BUG-1112, settle
waits instead of fixed sleeps) the test reaches its `sync now` sooner after the reset's share, inside the quiet window;
before, the fixed sleeps put it after the window more often.

## Fix
coop_diag.py asks the check again every 5 s until the host's OK of this stage is logged, at most 45 s (the 20 s quiet
window plus a check); the check itself is unchanged (an OK with the stage name in the host's log, then the guest's).
The test prints how many asks it took and after how many seconds.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
