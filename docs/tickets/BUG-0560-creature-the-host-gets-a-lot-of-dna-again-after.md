---
id: BUG-0560
title: Cell: the host gets a lot of DNA again after going into the cell editor
status: fixed
kind: bug
stage: Cell
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: host
versions: 0.7.5
fixed_in: 0.7.7
branch: fix/w43-world-state
commits: 82b49a38, 2dd52449
tests: cell_editor_budget (coop_cell_editor_budget.py)
signature:
docs:
related:
---

## Report
The host: after going into the editor it got a lot of DNA once again (game mode Editor in the report).
Reports: SporeCoop-report-2026-11-04 173319 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s283: the host is in the Cell editor (cell parts, "Name Your Species"), not the Creature one; the guest swims in Cell with 26 DNA, the host's editor shows 45.
Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (host log 17:32:33, s 238): "cell: DNA at the editor request: food=353 plant=74 spent=0
  dna_for_food=25 budget=25"; the host's editor showed 45 at s 283 (screenshot), the guest's Cell 26.
  Inferred: the 20 more come from a second budget path (the guest's DNA / the party's shared DNA added to the
  editor's on top of DNAForFood), not found in the logs.

* Observed (Ghidra + lab, docs/re/cell.md#cell-editor-budget): the Cell request posts the editor's budget (message
  0x90A03FDF) right after Submit; the editor's listener exists only after it entered. With two players the request
  waits for the vote (h_Submit), the post went out at once to no editor, and the editor opened with the config's start
  budget (100). Leaving it books spent = budget - remaining: after a free edit the host's spent was -100 (lab
  20261104-210158), which pays again on each later visit. The earlier "second budget path" guess is disproved.

## Fix
coop.cpp submit_now posts the budget again (DNAForFood(food) - spent, computed then) right after the held request's
Submit (switch `cell.editor_budget`). And wsync row cell_spent: the guest's Cell spent DNA (S+0x2C) is the host's, so
the guest's Cell DNA counts what the host's editor spent (switch `wsync.cell_spent`).

## Verification
Not reproduced in the lab (coop_cell_editor_* passed on 0.7.6 code before; not rerun). Package W1 (DNA).

Lab: cell_editor_budget FAIL with `SPORE_FIX_OFF=cell.editor_budget,wsync.cell_spent` out/lab/20261104-210158 (the
host's spent 0 -> -100 after a free edit), PASS out/lab/20261104-210201 (spent stays 0, the guest has the host's).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W1), not reproduced.
- 2026-11-04 (fix/w43-world-state, W1): fixed; see Fix and Verification.
- 2026-11-04 (fix/w43-detector2, D0 detector): the detector compares the Cell food / spent DNA (`Cell:state:dna`, `dna_spent`); not run on Cell in D0.
- 2026-11-05: released in 0.7.7.
