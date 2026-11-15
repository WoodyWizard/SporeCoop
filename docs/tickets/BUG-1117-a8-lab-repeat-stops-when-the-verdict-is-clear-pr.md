---
id: BUG-1117
title: A8: lab --repeat stops when the verdict is clear, prints the rate, fail-fast mode
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-labverdict
commits: 0481ee6e, c22c393a
tests: tools/tests/labverdict_selftest.py (make test), civ_money_short
signature:
docs: docs/06-testing.md#testing-lab-verdicts
related: BUG-1116
---

## Report
Plan 0.7.15 A8 (audit TST-8). `--repeat N` ran N copies whatever they showed; 29 % of all lab time went into
(branch, test) pairs run 5+ times; the agent computed the rate by hand from the summary.

## Root cause
lab.py queued N named copies and had no stopping rule and no rate line.

## Fix
`lab.py run X --repeat N [--until clear|fail|all]` (labverdict.repeat_stop, wired in lab.py's report()):
* `clear` (default): the copies not started yet are dropped once PASS and FAIL were both seen (mixed), or the first 3
  decided runs all FAILED (90 % Wilson lower bound 0.53), or the first 3 runs were all INCONCLUSIVE; a test that only
  passes runs all N (a one-in-N bug needs them).
* `fail`: fail fast, stop at the first FAIL. `all`: the old behaviour (a rate measurement).
* The rate: `repeat X: FAIL k of n (r %, 90 % interval lo-hi %), INCONCLUSIVE i; m of N runs, stopped early (why)` in
  the console, summary.md "Repeats" and results.json `repeat`.
* switch: none (lab tooling; `--until all` is the old behaviour).

## Verification
* FAIL side: lab 20261115-012144 (civ_money_short --repeat 6 with civ.build_money off): FAIL 3 of 3 (90 % interval
  53-100 %), stopped after 3 of 6, "3 copies not started" (3.1 min instead of ~6).
* PASS side: lab 20261115-012524 (--repeat 4, fix on): FAIL 0 of 4 (0-40 %), all 4 ran; lab 20261115-012904
  (--repeat 6 --until fail, fix on): 0 of 6, all 6 ran (no FAIL, nothing to stop).
* Unit: labverdict_selftest.py `repeat` (the rule, Wilson 2/7 = 10-59 %) and `lab_run` (an alternating test with
  --repeat 8 stops after 2; --until fail after 1; --until all runs 4 of 4). On rel/0.7.15 (`--tree`) the cases FAIL.

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-labverdict (unreleased).
