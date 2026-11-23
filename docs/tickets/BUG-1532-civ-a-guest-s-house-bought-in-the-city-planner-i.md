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
Lab lane 2 with temporary logging (20261123-101722, 20261123-101916, 20261123-102328, 20261123-102512,
20261123-102713), Ghidra: docs/re/civ.md#civ-planner-sell-back.
* Observed: the planner makes the house at the tile's mouse-down (AddBuilding, return 0xD1419C) and takes its price in
  the same frame. The test dropped it on plot 1, which the planner does not offer (in civ_npc the City Hall's links open
  only plots 2, 3 and 7; `coop.civ slots` calls all 11 "free"): the house showed "Sell For $1,600" under the cursor
  all along (hover slot -1), and the drop sold it back: CommunityEditor_Sell -> cCity::DestroyBuilding (out of the
  city list), then the refund AddMoney(civ, 1600) from CommunityEditor_Money (return 0xD07D0D). So BUG-1417's
  `planner_kept` was right: the house was taken away again.
* Observed: the guest's Civ income block (`h_AddMoney`, guest_income_blocked, `income_blocked=1`) dropped that refund:
  the sold-back house cost 1600 on both games.
* Why it passed before BUG-1417: the CMD_BUILD went at AddBuilding (the mouse-down), so the host built (paid) the house
  the guest's planner sold back, and the reconcile brought the host's copy to the guest after the planner: the test
  checked that copy. With `civ.planner_closed` nothing goes for a building taken away again, so the lost refund showed.
  The same happens to a player: a palette building dropped off the glowing plots (or back onto the palette) is sold
  back by the game and its price never comes back (BUG-1417's own `civ_planner_miss` asserts the unchanged money).
* The test's plot choice is the second half: `civ_planner_build` never placed a house on the guest's planner.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
