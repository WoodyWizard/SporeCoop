---
id: BUG-1186
title: Cell tests fail on rel/0.7.15 before the C8 split too: cell_swallow_gone, cell_unknown_model, cell_editor_timed, cell_sap (and cell_kill_once once)
status: fixed
kind: test
stage: Cell
reported: 2026-11-15
source: lab
role: 
versions: 
fixed_in: unreleased
branch: fix/w51-cell
commits: 07164f0d, 2fb72dd8, 8407195d, 572c5d34
tests: cell_swallow_gone, cell_unknown_model, cell_editor_timed, cell_sap, cell_kill_once, labstart_selftest.py
signature: swallows_marked=2 of 3 (the first swallow inside the hatch input lock); created -1 -> -1; mate gone_by=0x00E7C3E6; host cells < 1130 with every guest in its slot
docs: docs/06-testing.md#testing-cell-rel0715, docs/re/cell.md#cell-mate-cull, docs/archive/known-issues-2026.md#known-cell-rel0715-fails
related: BUG-1183, BUG-0827, BUG-0455, BUG-0237, BUG-0337, BUG-0632, BUG-1112, BUG-0768, BUG-0792
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
First each test on v0.7.14.1 (a detached worktree of the tag, `make`) and on rel/0.7.15 (57cff006) at the same time,
lanes 6 / 7, load 30 (labs 20261115-231325 of fix/w51-cell and 20261115-231325-v07141, the v0.7.14.1 tree's run copied there): v0.7.14.1 swallow_gone OK, unknown_model FAIL, editor_timed
FAIL, sap FAIL; rel FAIL on all four. So one 0.7.15 regression and three old failures:

* cell_swallow_gone, 0.7.15 regression of A9 (BUG-1112). Observed: the guest's trace said `input_lock=1` from its Cell
  entry (s 6.1) to after s 10.1, its first bite (s 9.8) and swallow ran in that window, `swallows_marked=2` of 3 and
  the first creature was never destroyed on the guest; the old start (`SPORE_START_FIXED=1`) passes (lab
  20261115-232022). The A9 settle ended 0.3 s after the guest was in (frames move, time runs) where the old fixed 4 s
  had covered most of the ~5.5 s hatch. Inferred: the game's executor does not reach SwallowWhole for a hatching
  avatar (no hook on why).
* cell_unknown_model, test bug since 0.7.12 (BUG-0768, 7c689797, added `ko_deaths=` between `food=` and `mirrors=`
  in the guest's `cell(guest):` status line). Observed: the test's pattern never matched, `created` read -1 -> -1; the
  test was not run between 2026-11-05 and 2026-11-15.
* cell_editor_timed, test design. Observed (`coop.pool mate` now prints `from_host=` and `gone_by=`): after the guest's
  `tp 25 0` a mate that had come on the host's -x side was ~40 m from the guest and the game's own cull (CellPhysicsStep
  0xE7C080 -> ShouldCullCell, DestroyCell return address 0xE7C3E6) destroyed it within 0.5 s (labs 20261115-232616 #1,
  #3; a mate on +x lived, #2). In all 20 recorded runs since 2026-11-08 the mate died exactly when it was on the far
  side. Not traced: which change since 2026-11-01 (left-side mates lived then) made the guest's active range smaller
  there; single-player rule anyway (docs/re/cell.md#cell-mate-cull).
* cell_sap, test precondition since 0.7.13 (BUG-0792). Not load: the code of 2026-11-02 (470627e5) passed at the same
  load (lab 20261115-232114-470627e5, copied to fix/w51-cell). Observed: with `cell.spawn_slots` every guest stays in its slot next to the
  host, the host's game populates one area and its count falls from ~950 (DestroyCell callers: the cull 0xE7C3E6 and
  NPC swallows 0xE7DA2E, nothing refills); `SPORE_FIX_OFF=cell.spawn_slots` passes (lab 20261115-235845) and the probe
  shows the two extra guests ~180 m away then (their own areas: 1316-1420 cells). A first-parent bisect on the count
  ended on a docs-only commit (65b4d691: 1105 against 1277 of its parent): before BUG-0792 the count depends on where
  the extras land, so it was no use.
* cell_kill_once: not reproduced, 6 of 6 OK at load 5-15 (labs 20261115-234347 with the settle fix, 20261115-234748
  with `SPORE_START_LOCKWAIT=0`); its two failures were at load 20-38. Left as is.

## Fix
* `tools/tests/startwait.py` `settle()`: also waits until no game's input lock stands (`coop.uistate` input_lock=0),
  within the same bound (4 s cold, 3 s warm: never slower than before 0.7.15). Twin `SPORE_START_LOCKWAIT=0`.
  labstart_selftest: a 2 s lock waited for, a lock over the bound ends at the bound, the twin.
* cell_unknown_model: `created=` by name on the `cell(guest):` line, and a check that the counters are there.
* cell_editor_timed: the guest jumps 25 m from the host on the mate's side (`from_host=`), the mate stays ~10 m away.
  `CELL_EDITOR_TIMED_FIXED_TP=1`: the old jump. `coop.pool mate` (tests only) prints `from_host=(dx dy)` and
  `gone_by=<the DestroyCell return address of the mate> <s> ago` (h_Destroy notes it).
* cell_sap: the guests go to their own areas before step 1 counts the host's cells (step 2 sent them there anyway);
  the count, the column and every check are the same.
No change in the mod's behaviour (only a debug command's output), no change on the wire.

## Verification
* cell_swallow_gone: FAIL 20261115-232137 (`SPORE_START_LOCKWAIT=0`), 20261115-231325 (rel before the fix); PASS
  20261115-232233 x2; v0.7.14.1 OK 20261115-231325-v07141, `SPORE_START_FIXED=1` OK 20261115-232022.
* cell_unknown_model: FAIL 20261115-231325-v07141 (v0.7.14.1, the old test); PASS 20261115-231325 (fix/w51-cell, the
  fixed test).
* cell_editor_timed: FAIL 20261115-232616 #1 and #3 (the old `tp 25 0`, `gone_by=0x00E7C3E6`); PASS 20261115-233205
  x3 (the mate on -x in #2).
* cell_sap: FAIL 20261115-231325 (the old order, 591 cells); PASS 20261116-000455 x2 (1311 / 1356 cells, sweep_peak
  1176 / 1311); `SPORE_FIX_OFF=cell.spawn_slots` with the old order OK 20261115-235845.
* The final runs are under History.

## History
- 2026-11-15: opened (feat/w51-cellsplit), evidence above.
- 2026-11-16: fix/w51-cell: v0.7.14.1 beside rel/0.7.15; swallow_gone a 0.7.15 regression (A9 settle inside the
  hatch), the other three old test failures; fixed (unreleased). Final: the four tests x2 on lanes 6 / 7, 8 of 8 OK,
  lab 20261116-001214 (load 11); make test clean.
