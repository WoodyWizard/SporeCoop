---
id: BUG-0362
title: lab: two lab runs started in the same second share one run directory (summary.md overwritten)
status: fixed
kind: tooling
stage: All
reported: 2026-11-01
source: found on fix/w29-puppet-move
role: both
versions: 0.7.3.2
fixed_in: 0.7.6
branch: fix/w42-infra
commits: a04e284f
tests: tools/tests/lab_selftest.py same_second (make test)
signature:
docs: docs/06-testing.md
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
`tools/lab.py` `new_run_dir()`: the run id is still the start time to the second (and the branch), but the directory is taken with `os.mkdir` (fails when it exists), and a run that finds it taken uses the next suffix (`<time>-2`, `-3`, ...; with a branch `<time>-2-<branch>`). Two processes racing for one name never both get it. `out/lab/flaky.json` (shared by all runs of a checkout) is read, updated and replaced under an flock (`flaky.json.lock`), written through a temporary file. Switch for the self-test: `SPORE_LAB_SHARED_RUNDIR=1` keeps the old shared directory.

## Verification
tools/tests/lab_selftest.py `same_second` (make test): two stubbed lab processes started together with the clock fixed to one second. FAIL with the lab of rel/0.7.6 (`--tree`: one directory `20260102-030405`, one run's summary and results), FAIL with `SPORE_LAB_SHARED_RUNDIR=1`, PASS on fix/w42-infra (`20260102-030405` and `20260102-030405-2`, each with its own results.json / summary.md, flaky.json counts both). no-fail-run: lab tooling, shown by the self-test (no game).

## History
- 2026-11-01: opened.
- 2026-11-04: fixed on fix/w42-infra (a04e284f): exclusive run directory with a suffix, flaky.json under a lock.
- 2026-11-04: released in 0.7.6.
