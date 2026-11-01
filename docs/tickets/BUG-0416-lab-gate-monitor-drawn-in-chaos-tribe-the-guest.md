---
id: BUG-0416
title: Lab gate: monitor drawn in chaos_tribe, the guest's copy of the host's pointer at rest 164 m from the host's own
status: open
kind: bug
stage: Tribe
reported: 2026-11-01
source: lab 20261101-140615 chaos_tribe, lane 7 (fix/w33-proxy-ground)
role: guest
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
out/lab/20261101-140615-fix-w33-proxy-ground chaos_tribe FAILED on `drawn: guest draws host 164.2 m from where host's
own game has it (limit 3.0 m, Tribe) on 2 lines from s 90`. The guest's partner lines at s 90 / 100 say `drawn_v=0.0`
(at rest) with drawn=(-168.1 -426.5 215.6) and (35.6 -424.2 274.7), the host's own pos= (332.2 -178.3 338.3) and
(125.9 -468.8 145.0): the chaos bot moves the host's view / pointer far between the marks (minimap clicks), rtt up to
193 ms on those lines. 3 + 4 later chaos_tribe runs on lane 7 did not show it.

## Root cause
Not analysed (outside fix/w33-proxy-ground's task). Inferred: the pointer copy is at rest on the guest while the host's
pointer has already jumped (the rule's `drawn_v <= 0.5` takes a stale copy that does not move yet as resting), the
BUG-0393 class of a jumping RTS pointer.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
