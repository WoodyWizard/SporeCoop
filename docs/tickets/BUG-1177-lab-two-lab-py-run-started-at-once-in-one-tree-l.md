---
id: BUG-1177
title: lab: two lab.py run started at once in one tree (lanes 9 and 10) both reported out/lab/20261115-193631; only one results.json (lane 9) is left there
status: open
kind: tooling
stage: Tools
reported: 2026-11-15
source: fix/w51-evolve
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0353, BUG-0362
---

## Report
2026-11-15 19:36:31, tree fix/w51-evolve: `(SPORE_LAB_FIXOFF=... lab.py run evolve_world --lane 9 ...) & (lab.py run
evolve_world --lane 10 ...)` from one shell, the same second. Both printed `FAILED: 0/1 ... -> out/lab/20261115-193631/summary.md`
(lane 10: "FAIL evolve_world lane 10 222 s"; lane 9: 4.6 min); the directory holds one evolve_world.log, the two game
logs and a results.json with lane 9 only: the lane 10 run's results and logs are lost. new_run_dir (BUG-0353 /
BUG-0362) takes the id with os.mkdir and a -2 suffix, and an earlier pair started 1 s apart (20261115-193249,
20261115-193249-2) did get two directories.

## Root cause
Not looked into (not part of fix/w51-evolve). Reproduce: two runs of one test in one tree started in the same
second on two lanes. Workaround: start parallel runs a few seconds apart (docs/06-testing.md#testing-evolve-own-creature).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
