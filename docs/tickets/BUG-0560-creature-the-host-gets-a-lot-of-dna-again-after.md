---
id: BUG-0560
title: Cell: the host gets a lot of DNA again after going into the cell editor
status: open
kind: bug
stage: Cell
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: host
versions: 0.7.5
fixed_in:
branch:
commits:
tests:
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

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced in the lab (coop_cell_editor_* passed on 0.7.6 code before; not rerun). Package W1 (DNA).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W1), not reproduced.
