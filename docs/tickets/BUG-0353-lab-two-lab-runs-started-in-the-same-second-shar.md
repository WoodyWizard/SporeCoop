---
id: BUG-0353
title: lab: two lab runs started in the same second share one out/lab/<id> directory and mix their logs
status: fixed
kind: tooling
stage: All
reported: 2026-10-31
source: fix/w22-reports lab runs 2026-10-31
role: 
versions: all
fixed_in: 0.7.6
branch: fix/w42-infra
commits: a04e284f
tests: tools/tests/lab_selftest.py same_second (make test)
signature:
docs: docs/06-testing.md
related:
---

## Report
Two `tools/lab.py run cell_own_load` runs started on lanes 5 and 6 in the same second (13:01:15)
both wrote to out/lab/20261031-130115: one summary.md, one results.json and the test's log with the
lines of both runs interleaved.

## Root cause
Observed: the run id is the start time to the second, the directory is not taken exclusively.

## Fix
`tools/lab.py` `new_run_dir()`: the run id is still the start time to the second (and the branch), but the directory is taken with `os.mkdir` (fails when it exists), and a run that finds it taken uses the next suffix (`<time>-2`, `-3`, ...; with a branch `<time>-2-<branch>`). Two processes racing for one name never both get it. `out/lab/flaky.json` (shared by all runs of a checkout) is read, updated and replaced under an flock (`flaky.json.lock`), written through a temporary file. Switch for the self-test: `SPORE_LAB_SHARED_RUNDIR=1` keeps the old shared directory.

## Verification
tools/tests/lab_selftest.py `same_second` (make test): two stubbed lab processes started together with the clock fixed to one second. FAIL with the lab of rel/0.7.6 (`--tree`: one directory `20260102-030405`, one run's summary and results), FAIL with `SPORE_LAB_SHARED_RUNDIR=1`, PASS on fix/w42-infra (`20260102-030405` and `20260102-030405-2`, each with its own results.json / summary.md, flaky.json counts both). no-fail-run: lab tooling, shown by the self-test (no game).

## History
- 2026-10-31: opened (fix/w22-reports).
- 2026-11-04: fixed on fix/w42-infra (a04e284f): exclusive run directory with a suffix, flaky.json under a lock.
- 2026-11-04: released in 0.7.6.
