---
id: BUG-0353
title: lab: two lab runs started in the same second share one out/lab/<id> directory and mix their logs
status: open
kind: tooling
stage: All
reported: 2026-10-31
source: fix/w22-reports lab runs 2026-10-31
role: 
versions: all
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Two `tools/lab.py run cell_own_load` runs started on lanes 5 and 6 in the same second (13:01:15)
both wrote to out/lab/20261031-130115: one summary.md, one results.json and the test's log with the
lines of both runs interleaved.

## Root cause
Observed: the run id is the start time to the second, the directory is not taken exclusively.

## Fix
Not done. Proposal: create the run directory with `os.makedirs(exist_ok=False)` and add a suffix
(`-2`, ...) when it exists.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-31: opened (fix/w22-reports).
