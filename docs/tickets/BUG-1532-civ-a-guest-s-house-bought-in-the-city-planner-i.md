---
id: BUG-1532
title: Civ: a guest's house bought in the city planner is taken for one taken away again and never built (civ.planner_closed, civ_planner_build FAILs on rel/0.7.16.1)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-23
source: lab
role: guest
versions: rel/0.7.16.1
fixed_in: 0.7.16.1
branch: fix/w67-planner
commits: 32bbbe5f, 79d354ad
tests: civ_planner_build, civ_planner_miss, civ_planner_miss_off (tools/tests/coop_civ_planner_build.py)
signature:
docs: docs/re/civ.md#civ-planner-sell-back
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
A correction of BUG-1417 inside its switch `civ.planner_closed` (civ.cpp): the guest's `h_AddMoney` lets the planner's
sell refund through (return address `SporeCoop::CommunityEditor_Money_AddRet` 0xD07D0D, `civlogic::planner_refund_passes`)
when the planner's hand is a building this planner added and has not sent (a `g_planner_out` entry, marked `refunded`);
the shared money's next guest delta brings it to the host. A sold building of the city (the host has it) and the
guest's other income stay blocked. With the switch off (0.7.16: sent at AddBuilding, the host builds it) the refund stays
blocked as before. Log "sold back: its refund N goes through (BUG-1532)", counter `refunds=` in the `planner_out:` line
of `coop.civ tables`. `coop.civ planner` prints the hand (`hand <obj> hover_slot= sell=`).
Test: `civ_planner_build` held the house over plot 1, which the planner does not offer; it now moves over the free
plots while holding the tile until the planner's hand is on a plot without "Sell For" and lets go there (new check
"the guest's planner offered a free plot"), so it checks the guest's own placement reaching the host at the close.
Unit test: tests/civ_tests.cpp (planner_refund_passes).

## Verification
* FAIL without the fix: out/lab/20261123-103335 (`civ_planner_miss` built from 32bbbe5f, the code of rel/0.7.16.1:
  "the shared money did not change (host 3000.0 -> 1400.0, guest 3000.0 -> 1400.0)": the sold-back house's refund was
  blocked; the run's header names 79d354ad, the tree's HEAD, with the older sources checked out). The original
  `civ_planner_build` FAILs: out/lab/20261123-101722, 20261123-101916 (branch tree, old test, temporary logging).
* PASS with the fix (lanes 2/3, SPORE_DETECTOR=20): civ_planner_build x3 (20261123-103219, 20261123-103459 #1 #2;
  the drop on plot 2 after plot 1 showed sell=1), civ_planner_miss x3 (20261123-103219, 20261123-103459 #1 #2; money
  3000 -> 3000 on both games, "sold back, its price came back"), twin civ_planner_miss_off x2 FAIL as it must
  (20261123-103459: the host builds the house, money 3000 -> 1400).
* Regression: civ_owners_session, civ_owners_reload PASS (20261123-103739), civ_save_strip PASS (20261123-103944, lane 2 + free lane 3).

## History
- 2026-11-23: opened.
- 2026-11-23: root cause and fix on fix/w67-planner (inside civ.planner_closed: the sell refund of an unsent planner
  building passes the guest's income block; civ_planner_build drops on an offered plot); lab verified.
