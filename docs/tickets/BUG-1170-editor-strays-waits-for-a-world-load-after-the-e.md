---
id: BUG-1170
title: editor_strays waits for a world load after the evolution that the in-place evolution (BUG-0508) no longer makes when the guest plays its own creature
status: open
kind: test
stage: Creature
reported: 2026-11-15
source: lab 20261115-164240 (feat/w51-core2)
role: 
versions: 
fixed_in:
branch:
commits:
tests: editor_strays
signature:
docs:
related: BUG-0508, BUG-0212, BUG-0689, BUG-1141
---

## Report
editor_strays FAILED in lab 20261115-164240 (feat/w51-core2, lane 10), both rounds: "the evolution cinematic is over,
the guest loaded the host's world after it" (wait_settled: a new host share and a guest load). No fault, no stray, the
herd checks after it passed.

## Root cause
Observed (host and guest game logs of the run): the guest plays its own creature (`body: own avatar species 065CBE70 ->
290225CD ... from this world's record (worlds.ini)`, the own-creature choice of BUG-0689 / 0.7.13); after the host's
evolution the host decides "evolved in place: Bob keeps its world" (BUG-0508, session_glue.cpp evolve_inplace_ok) and
its 30 s fallback ("still plays <host species>: sending the world") does not fire, because the guest plays its own
species, not the host's. So no world is shared and nothing is loaded: the behaviour BUG-0508 wants. The last PASS runs
of the test (20261108-071725, fix/w46-g4-tests) shared the world only through that fallback (the guest still played the
host's species then). Inferred: the test's precondition (a load after every evolution) is older than BUG-0508 and the
own creature; not caused by C2 (the decision and the fallback are unchanged code; the world identity does not take
part).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
