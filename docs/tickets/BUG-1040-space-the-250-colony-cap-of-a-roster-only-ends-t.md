---
id: BUG-1040
title: Space: the 250 colony cap of a roster only ends the planets of one star
status: fixed
kind: bug
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-23
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-space2
commits: daac928c
tests: 
signature:
docs: docs/re/space.md#space-restore-window
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
space_map.cpp: the roster scan checks the 250-colony cap before every star too, so it ends the whole scan. No switch
(a cap only; the ticket's fix idea).

## Verification
no-fail-run: an empire of more than 250 colonies is not reachable in a test (a debug faking 300 colonies would test
the fake); checked by reading; the roster tests (space_map, space_allies) run the scan.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-space2 (the cap ends both loops).
