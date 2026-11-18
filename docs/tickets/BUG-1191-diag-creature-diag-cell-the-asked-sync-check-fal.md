---
id: BUG-1191
title: diag_creature, diag_cell: the asked sync-check falls into the 20 s share-quiet window, the next one comes after the test's 25 s wait
status: fixed
kind: test
stage: Tools
reported: 2026-11-16
source: final check 20261116-025154
role: 
versions: 0.7.14.1
fixed_in: 0.7.15
branch: fix/w51-t1
commits: 1318d38d, 2393823a
tests: diag_creature, diag_cell (tools/tests/coop_diag.py)
signature:
docs: docs/06-testing.md#testing-w51-t1-triage
related: BUG-0754
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
41.7, about 30 s after the asked check: past the test's 25 s. Lab 20261116-085443 (with the fix) on lane 9: diag_cell's
OK came after 19 s and 4 asks, diag_creature's after 6 s and 2. Not a 0.7.15 change: v0.7.14.1 (a detached worktree of
the tag, built, lane 9) fails both the same way, lab 20261116-090440 ("FAIL the host logs a sync-check OK in Cell" /
"... in Creature"); the ledger's earlier passes were starts that reached the ask after the quiet window.

## Fix
coop_diag.py asks the check again every 5 s until the host's OK of this stage is logged, at most 45 s (the 20 s quiet
window plus a check); the check itself is unchanged (an OK with the stage name in the host's log, then the guest's).
The test prints how many asks it took and after how many seconds.

## Verification
* FAIL: lab 20261116-025154 (final check), lab 20261116-082959 (rel/0.7.15 on lane 9), lab 20261116-090440 (v0.7.14.1
  on lane 9): both tests "FAIL the host logs a sync-check OK in Cell / Creature".
* PASS: lab 20261116-085443 (Cell: the OK after 19 s, 4 asks; Creature: after 6 s, 2 asks); diag_cell PASS again in lab
  20261116-091534. diag_creature's sync-check OK came in every run with the fix (labs 20261116-091534, 20261116-092559, 20261116-092942: after
  6-11 s, 2-3 asks), but those runs failed its other gate "one check costs under 1 ms" (host 3.853 / 1.138 / 3.029 ms):
  wall time of the check at a machine load average of 26-30 (other agents' labs, 12 games); the unchanged rel/0.7.15
  failed it the same way in lab 20261116-082959 (host 5.669 ms), the idle final check measured 0.046 ms. Not this
  change; the final check runs on an idle machine.

## History
- 2026-11-16: opened.
- 2026-11-16: fixed on fix/w51-t1.
- 2026-11-18: released in 0.7.15.
