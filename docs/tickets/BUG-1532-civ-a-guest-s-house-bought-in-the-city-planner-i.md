---
id: BUG-1532
title: Civ: a guest's house bought in the city planner is taken for one taken away again and never built (civ.planner_closed, civ_planner_build FAILs on rel/0.7.16.1)
status: open
kind: bug
stage: Civ
reported: 2026-11-23
source: lab
role: guest
versions: rel/0.7.16.1
fixed_in:
branch:
commits:
tests: civ_planner_build (tools/tests/coop_civ_planner_build.py)
signature:
docs:
related: BUG-1417, BUG-0189, BUG-1457
---

## Report
Found on fix/w67-citizens (BUG-1457) while running Civ regressions on rel/0.7.16.1 (25979b7d + the BUG-1457 change), lane 4/5:
civ_planner_build FAILs 4 of 4 (out/lab/20261123-095727, 20261123-100251 x2, and 20261123-100735 with
SPORE_FIX_OFF=civ.pointer_view, so not BUG-1457's change): the guest drags the house tile onto a free plot, its planner
takes the price ($3000 -> $1400 on both games, the shared money falls), but "after the planner the guest's game has a new
house in city 0 ([])" and the host never builds it. The guest's log: `civ: planner: a building 018EB106 the planner
added was taken away again: not sent (BUG-1417)`. With `SPORE_LAB_FIXOFF="civ_planner_build=civ.planner_closed"` it
PASSes (out/lab/20261123-100903). It passed on rel/0.7.16 before BUG-1417 (lab 20261122-194210, 20261123-023643).
A player: a house bought in the planner costs the money and is not built (on both games).

## Root cause
Not analysed. Observed: BUG-1417's `civlogic::planner_kept` judged the kept house as taken away again at the planner's
close. Inferred: the check at the close does not find the same block / noun in the city's building list (the house
of the drag is another object or another block by then, or the list is read before the planner commits it).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
