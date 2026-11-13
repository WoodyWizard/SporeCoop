---
id: BUG-1040
title: Space: the 250 colony cap of a roster only ends the planets of one star
status: open
kind: bug
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-23
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
Code audit for 0.7.14, finding SPC-23. Not seen at runtime, found by code reading.
The roster scan stops a star's planets at 250 colonies but goes on with the other stars, so an empire with more than 250 colonies sends up to one more colony per remaining star (up to about 1270). Harmless today (the roster goes in more parts), only the cap is not one.

## Root cause
Observed in code (main efd2e0ba), confidence high: space_map.cpp:188-219, the `break` at :217-218 leaves only the inner loop over the star's planets.

## Fix idea
Stop both loops (a flag or a lambda with return). No switch needed.

## How to test
No twoinst test needed: a unit test of the scan is not possible without the game; check by reading, or a debug command that fakes 300 colonies. no-fail-run is acceptable (cosmetic cap).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
