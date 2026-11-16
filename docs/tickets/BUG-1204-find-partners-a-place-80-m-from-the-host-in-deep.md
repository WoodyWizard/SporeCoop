---
id: BUG-1204
title: find_partners: a place 80 m from the host in deep water starts the guest game's sea monster; its cinematic pauses the guest's game and every later place fails
status: open
kind: test
stage: Creature
reported: 2026-11-16
source: final check 20261116-025154
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
Final check 20261116-025154: find_partners FAIL "a place 80 m from the host where the host is off the guest's screen,
in front of it": from the second direction on the host's tag stays at the same screen place (678, 300). Reproduced on
lane 10 on rel/0.7.15 (20261116-082820) and on v0.7.14.1 (fix/w51-t2-v07141/out/lab/20261116-084702). History: 1 of 7
rel/main runs failed (20261113-094538 fix/w49-warm, the same line).

## Root cause
Observed (guest game logs of all three FAIL runs): `drama: sea_monster event starts (guest)` ~3 s after the first
`coop.abilities goto` (the first place, 80 m from the host, is in deep water); from then on the guest's trace says
`busy=cinematic,input_lock,paused`: the sea monster's cinematic pauses the guest's game for the rest of the test,
its camera never turns again, so no later place shows the host off screen. The sea monster is the guest's own game's
event on purpose (drama.cpp: the guest's deep-water death). Inferred: whether the first place is deep water depends
on the host's start position (the fixture's, a few metres apart from run to run): an old flake of the test.

## Fix
coop_find_partners.py (Creature): the guest's game holds the sea monster event while the test places it
(`coop.drama hold 40`, kind 6, the test knob of BUG-1188), released at the end (`coop.drama hold 0`). The checks are
unchanged.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
