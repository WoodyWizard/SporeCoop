---
id: BUG-0397
title: Tests: play_tribe (first runs) fails on its own checks: the host bot's box select misses, the guest's orders leave members idle, citizens p90 1.9 m
status: fixed
kind: test
stage: Tribe
reported: 2026-11-01
source: lab runs on fix/w32-tribe-sync (out/lab/20261101-115510)
role: both
versions: 
fixed_in: 0.7.8
branch: fix/w44-tests
commits: 27c23222 2e1860c0
tests: play_tribe
signature:
docs: docs/06-testing.md#testing-harness-g2
related: BUG-0505, BUG-0531
switch: none
---

## Report
`play_tribe` (tools/tests/play_tribe.py, nightly, 10 min of bot play with real clicks) had never run in the
lab before. Two runs on lane 7 (out/lab/20261101-115510, branch fix/w32-tribe-sync on main bc3de20), both with
no sync-check MISMATCH and no fault:
* play_tribe#1 (seed 977114307) FAIL: "the same members, owners and chiefs on both" (a test bug: the
  `chiefs=` text of `coop.tribe status` goes on with the host's own counters `spread=0 placed=1`, compared
  as a set of words: fixed on this branch, only `owner:#member` pairs are compared now); "host: gave orders
  through real clicks (3)" (host bot `no_selection=30`: its box drags selected nothing); "guest: the
  members act on the orders" (idle 20 of 25, e.g. `guest walk ground [2]: no movement (kind=1 n=1 action=4)`);
  "gathering and hunting bring food (1 rises)" (follows from the idle orders).
* play_tribe#2 (seed of that run) FAIL only on "sync t*: 90% within 1.5 m" (citizens p90 1.92 m, max 2.53 m;
  animals p90 0.78 m); every order check passed (19 / 19 orders acted on).
Replay: `SPORE_LANE=7 python3 tools/tests/play_tribe.py --minutes 10.0 --seed 977114307`.

## Root cause
Open. The chiefs compare is a test bug (fixed). Not analysed: why the host bot's box select missed all
game long in #1 (the minimap calibration? `no_selection` 30 of 110 actions) and why the guest's walk orders
left its members idle there (in #2 the same kinds of orders worked); the citizens' 1.5 m p90 is the mirror
walking (BUG-0365, fix/w30-tribe-walk, reworked on another branch now), not looked at here.

## Fix
G2 (fix/w44-tests, 2026-11-05). Kind: **the bot's own activity** ("gave orders through real clicks" counts what the bot
did, not what the game did).
* Observed (the 37 play_tribe runs kept in the worktrees' out/lab, 0.7.3-0.7.8): the floor of 10 orders failed for
  the guest in 4 runs (feat/w33-tests-session 20261101-134020: 9, fix/w40-world-objects 20261103-143918: 9, rel/0.7.7
  20261104-220200: 8, rel/0.7.8 20261105-111032: 9) and for the host in 5 (3, 9, 8, 7, 0), every time with no order
  lost or rejected. In those runs the steps went to camera moves, partner clicks, waits (wait=26 of ~80 guest steps in
  20261105-111032), gifts (give=7, gave=1), heals with no food mat on screen (heal_nothing=18 in 20261104-220200), box
  drags that selected nothing (host no_selection=44 in 20261105-115848) and gathers with no tree in the open, which
  ended the step without a note (gather=10, 4 gather orders, 20261105-111032).
* Fix (test only, no switch): each bot keeps a pace of one order per 30 s of play (`ORDER_EVERY`). Behind it, the step
  plays an order kind (the plan's kind, else a gather; a hunt when the kind's last try found nothing to click or its
  box missed), and when every own member is busy it orders one of them again. The random draws stay as they were, so
  a seed replays the rest of the play. A gather with no tree in the open is counted (`gather_nothing`). The floor of
  10 and the checks on the orders (acted on, lost, rejected) are unchanged.
* "the members act on the orders" (once in 0.7.8, fix/w44-tribe-guest 20261105-115848, 13 ok / 11 idle): the game
  (members stalled in the gather node, BUG-0531, updated there), not changed.
* The ticket's first-runs items: the chiefs compare was fixed in 2026-11-01; the citizens' p90 is BUG-0365 / BUG-0481
  (fixed there).

## Verification
* FAIL (the floor, the old bots): rel/0.7.8 out/lab/20261105-111032 (guest 9), rel/0.7.7 20261104-220200 (guest 8),
  fix/w40-world-objects 20261103-143918 (guest 9), fix/w41-tribe-orders 20261104-030728 #3 (host 7).
* PASS: out/lab/20261105-135000 play_tribe x3 on lane 9 (host 20 / 13 / 22 orders, guest 21 / 17 / 16; the pace chose
  11 / 47 / 40 steps; members acted on 20/20, 12/13, 22/22 and 21/21, 15/17, 16/16 orders; no order lost or rejected).

## History
- 2026-11-01: opened.

Update (2026-11-05, rel/0.7.8 integration, out/lab/20261105-111032): play_tribe failed only on "guest: gave orders
through real clicks (9)" (the check wants 10 in 10 min). The same check failed in 12 of 37 play_tribe runs kept in
the worktrees' out/lab (0.7.4-0.7.8, e.g. rel/0.7.7 20261104-220200 with 8): the bot's own order rate, not a game
fault. For G2 (0.7.8 plan): make the guest bot give a fixed minimum of orders, or scale the floor with its actions.
- 2026-11-05 (fix/w44-tests, G2): the order floor failures were the bots' pace; the bots keep one order per 30 s.
- 2026-11-05: released in 0.7.8.
