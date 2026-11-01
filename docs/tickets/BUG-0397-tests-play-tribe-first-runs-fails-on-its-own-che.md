---
id: BUG-0397
title: Tests: play_tribe (first runs) fails on its own checks: the host bot's box select misses, the guest's orders leave members idle, citizens p90 1.9 m
status: open
kind: test
stage: Tribe
reported: 2026-11-01
source: lab runs on fix/w32-tribe-sync (out/lab/20261101-115510)
role: both
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
