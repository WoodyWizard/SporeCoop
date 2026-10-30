---
id: BUG-0321
title: tribe_npc_dash fails 2 of 3 runs on main (p90 1.5-2.2 m over the 1.2 m limit)
status: open
kind: test
stage: Tribe
reported: 2026-10-30
source: lab 20261030-144611-tmp-w13-main-ref, 20261030-144027-fix-w13-tribe-civ
role: guest
versions: main 5d7f20e
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0305
---

## Report
`tribe_npc_dash` (coop_tribe_npc_dash.py, BUG-0305's test, merged with fix/w12-tribe-npc) on
lane 6, GOG 2024:
* main 5d7f20e (temporary worktree tmp/w13-main-ref): lab 20261030-144611-tmp-w13-main-ref, 1 of 3
  passed; FAIL "90 % of 106 samples within 1.95 m" and "... 114 samples within 1.51 m" (limit 1.2 m).
* fix/w13-tribe-civ: lab 20261030-141833 (1 FAIL, 1.81 m) and 20261030-144027 (3 FAIL: 2.21, 1.92,
  1.46 m), while lanes 6 and 7 both ran tests.
The w12 branch's own runs of the test also passed and failed alternately (its summaries). No
fault, the games kept drawing. Not caused by fix/w13-tribe-civ (main fails the same way).

## Root cause
Not analysed. Inferred: the 1.2 m p90 limit is too tight for a loaded machine (the test runs the
guest at 15 fps; two lanes busy), or npc.trail does not hold in every run.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
