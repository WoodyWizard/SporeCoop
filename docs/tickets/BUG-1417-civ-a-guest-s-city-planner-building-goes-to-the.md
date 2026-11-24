---
id: BUG-1417
title: Civ: a guest's city planner building goes to the host at AddBuilding, before the planner kept it (free building or refund, CIV-7)
status: fixed
kind: bug
stage: Civ
reported: 2026-11-22
source: audit
role: guest
versions: 0.7.16 and earlier (since BUG-0189)
fixed_in: 0.7.16.1
branch: fix/w63-civ
commits: a3c27944
tests: civ_planner_miss, civ_planner_miss_off, civ_planner_build
signature:
docs: docs/re/civ.md#civ-build-refund
related: BUG-0189, BUG-0807, BUG-0902
---

## Report
External static audit of 0.7.15.5, item CIV-7 (verified in out/research/ext-audit-verify-civ-spc.md); BUG-0902 had
noted it untracked: "The Civ stage's guest send (CMD_BUILD, BUG-0189) is the same pattern in Civ ... Left for a ticket
of its own if seen". Not seen in Civ yet.

## Root cause
* Code: civ.cpp `h_AddBuilding` sent CMD_BUILD with the building's GetCost as "paid" at the city planner's
  AddBuilding (return address CityPlanner_AddBuilding_Ret).
* Observed in Space with the same planner code (BUG-0902, lab 20261110-201036): a drop that does not end on a free plot
  adds the building and takes it away again, no money taken. Inferred for Civ: the host then builds it unpaid
  (`build_in_city(..., false)`) or, when it cannot, `refund_build` credits the claimed price to the shared money
  (up to 100000): a free building or printed money.

## Fix
civ.cpp: the guest's planner building goes into a queue (`g_planner_out`: city, block, noun, price); once the planner
closed (or after 10 min) it is sent with its placed position only when it is still in the city's building list as
the same block and noun (`civlogic::planner_kept`, tests/civ_tests.cpp); one taken away again is dropped (log "was
taken away again: not sent (BUG-1417)"). For 5 s after such a send the guest's building reconciliation of that city
waits (`planner_send_holds`, as the planner hold of BUG-0493): the host's snapshot from before the send does not have
the building yet. Counters `planner_out: adds= waiting= queued= sent= dropped=` in `coop.civ tables`. The message is
unchanged (no wire change). Switch `coop.fix civ.planner_closed off` (sent at AddBuilding, 0.7.16).

## Verification
open, waiting for the lab run. `coop_civ_planner_build.py --miss` (suite `civ_planner_miss`, civ_npc): the guest drops
the house tile on a taken plot of its planner (real input) and closes it; precondition: the planner's AddBuilding ran
(adds went up, else INCONCLUSIVE); PASS: no new house on the guest or the host (20 s), the shared money unchanged.
Twin `civ_planner_miss_off` (SPORE_FIX_OFF=civ.planner_closed): the host builds the house, must FAIL. Regression:
`civ_planner_build` (a kept house still reaches the host, now at the planner's close).

- 2026-11-23 (0.7.16.1 final check): civ_planner_miss and civ_planner_build PASS in 20261123-152921 (aad722ac).

## History
- 2026-11-23: closed on the 0.7.16.1 final check lab evidence.
- 2026-11-22: opened.
- 2026-11-22: fix on fix/w63-civ (switch civ.planner_closed, test civ_planner_miss and its twin); lab run pending.

- 2026-11-22: renumbered from BUG-9302 at the 0.7.16.1 merge (the id came from the local fork branches pr-*); commits before the merge name BUG-9302.
- 2026-11-23: BUG-1532 (fix/w67-planner): civ_planner_build failed with this fix: the test's drop missed the offered plots
  and the guest's planner sold the house back, whose refund the guest's income block dropped (0.7.16 hid it: the host
  built the house sent at AddBuilding). Corrected inside civ.planner_closed: the sell refund of an unsent planner
  building passes; the test drops on an offered plot. civ_planner_miss PASS x3, civ_planner_miss_off FAIL x2.
- 2026-11-23: released in 0.7.16.1.
