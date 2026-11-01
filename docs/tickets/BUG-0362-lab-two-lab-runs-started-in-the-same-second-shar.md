---
id: BUG-0362
title: lab: two lab runs started in the same second share one run directory (summary.md overwritten)
status: open
kind: tooling
stage: All
reported: 2026-11-01
source: found on fix/w29-puppet-move
role: both
versions: 0.7.3.2
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/10-dev-pipeline.md
related: BUG-0360
---

## Report
Two `tools/lab.py run ... --branch fix/w29-puppet-move` started within the same second (one with
`SPORE_FIX_OFF=...` on lane 2, one on lanes 3/4) both used `out/lab/20261101-011005-fix-w29-puppet-move`:
the test logs were kept apart only because the names differed (`puppet_walk#1/#2` vs `puppet_walk`),
`summary.md` and `results.json` are the second run's only, and `flaky.json` mixed them.

How to reproduce: start two lab runs of the same branch in one second (`cmd1 & cmd2`).

## Root cause
`cmd_run` (tools/lab.py) names the run `datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + branch`
and does not check whether the directory exists. Observed 2026-11-01.

## Fix
Not fixed (found on fix/w29-puppet-move, outside its task). Suggested: add a suffix (-2, -3) when the directory exists, or create it with exist_ok=False and retry.

## Verification
Open.

## History
- 2026-11-01: opened.
