---
id: BUG-1201
title: avatar_stale_target: no click reaches the partner's puppet since BUG-0989 puts the joining guest 5 m beside the host (outside the host's view on the fixture): 0 rounds by a real click
status: open
kind: test
stage: Creature
reported: 2026-11-16
source: lab
role: 
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
Final check of 0.7.15 (lab 20261116-025154, lane 3) and both reruns on fix/w51-t5 (lab 20261116-082817
avatar_stale_target#1, #2): FAIL `0 of these rounds had the target from a real click (>= 2)`; every click said `the
puppet is not on the screen`; the other checks passed (the rounds ran by the game's SetTarget).

## Root cause
Observed: `coop.targets` on the host never had the partner's puppet inside the screen. In every 0.7.15 run the host's
partner line says d=5 and the guest's log `this creature started on the host's (0.0 m apart): put 5 m beside it
(BUG-0989)`; the guest stands at (-171.6 -466.5 -88.9) on the creature fixture, outside the host's view. Before
BUG-0989 (0.7.14) it stayed on the host's spot (d=0-1 in the 0.7.12 / 0.7.13 runs), in the middle of the host's view.

## Fix
tools/tests/coop_avatar_stale_target.py: right after the start the guest's creature goes back onto the host's spot
(`coop.npc gopos`), where the test's clicks were made for (`SPORE_STALE_AS_JOINED=1`: as it joined). switch: none.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
