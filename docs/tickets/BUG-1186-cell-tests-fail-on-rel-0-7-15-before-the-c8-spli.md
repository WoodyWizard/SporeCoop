---
id: BUG-1186
title: Cell tests fail on rel/0.7.15 before the C8 split too: cell_swallow_gone, cell_unknown_model, cell_editor_timed, cell_sap (and cell_kill_once once)
status: open
kind: test
stage: Cell
reported: 2026-11-15
source: lab
role: 
versions: 
fixed_in:
branch:
commits:
tests: cell_swallow_gone, cell_unknown_model, cell_editor_timed, cell_sap, cell_kill_once
signature:
docs:
related: BUG-1183, BUG-0827, BUG-0455, BUG-0237, BUG-0337, BUG-0632
---

## Report
Found by the Cell test set of C8 (BUG-1183, feat/w51-cellsplit) on lanes 6 / 7, machine load average 20-30 from other
agents' lab runs. The same tests fail on the build of rel/0.7.15 7a733c60 itself (the base of the split, built in a
detached worktree), with the same check, so they are not from the split:

| test | split build (adddaa5b) | base build (7a733c60) | failed check |
|---|---|---|---|
| cell_swallow_gone | FAIL 20261115-212528, FAIL 20261115-224549 | FAIL 20261115-224549-base | every creature the guest swallowed whole is gone in the host's game (2 of 3) |
| cell_unknown_model | FAIL 20261115-212528, FAIL 20261115-224549 | FAIL 20261115-224549-base | the guest mirrors the cell once its model is known (-1 -> -1 created, both copies) |
| cell_editor_timed | FAIL 20261115-212528, FAIL 20261115-224549 | FAIL 20261115-224549-base | the guest's cell editor opens (the mate was touched) |
| cell_sap | FAIL 20261115-212528 (623 cells), FAIL 20261115-224549 (668) | FAIL 20261115-224549-base (834) | 1. the host holds over 1100 cells |
| cell_kill_once | FAIL 20261115-212528 (kill 2 broke twice), OK 20261115-224549 | FAIL 20261115-224549-base (kill 4 broke twice) | the host's game broke the creature once |

(Run dirs under out/lab/ of feat/w51-cellsplit; `-base` is the base tree's run copied there.) The lab's history before
this: cell_sap 3 of 10 on rel/main, cell_swallow_gone 1 of 6, cell_unknown_model 1 of 7, cell_editor_timed 1 of 7,
cell_kill_once 0 of 7. Twice in a row on both builds makes swallow_gone, unknown_model and editor_timed look like
regressions on rel/0.7.15 (wave B / C, for example the B1 switch retirement or C2 / C3), or tests that fail under
this load; cell_sap's 1100-cell precondition is load-dependent (known flake).

## Root cause
Not analysed (outside C8). Next: run these on v0.7.14 and on rel/0.7.15 on an idle machine; if they pass on v0.7.14 and
fail on rel/0.7.15 idle, bisect rel/0.7.15.

## Fix

## Verification

## History
- 2026-11-15: opened (feat/w51-cellsplit), evidence above.
