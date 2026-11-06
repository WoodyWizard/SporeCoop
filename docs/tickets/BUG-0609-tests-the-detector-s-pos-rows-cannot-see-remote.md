---
id: BUG-0609
title: Tests: the detector's pos rows cannot see remote bodies that trail (15 m for creatures, 6 m for others, no time base)
status: open
kind: test
stage: All
reported: 2026-11-06
source: fix/w45-remote-bodies (M1)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0425
---

## Report
M1 (BUG-0425) asked for the class's detector rows to be gone on every stage. The detector (worlddiff.diff) has a
`<stage>:pos:<kind>` row only for matched objects more than 15 m apart (creatures, TOL_MOVING) or 6 m (others), and
it compares two dumps read one after the other (no common time): a copy that trails its host body by 0.2 s (0.6-2 m
at walking speed, the class) never makes a row. In the 0.7.8 final runs (rel/0.7.8 out/lab 20261105-*, play_*) there is
no `pos` row at all; the class shows only in the monitor's `proxy_stuck` lines (4 in those runs) and in
coop_body_trail.py (tools/tests/body_trail.py), which compares both games' own positions at the same moment.

## Root cause
Not a bug of the game or the mod: the detector's design. Idea: a `pos` row per kind from `coop.bodyrec` rows
(absolute time) instead of the dump, with a speed-aware limit (lag x speed).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened.
