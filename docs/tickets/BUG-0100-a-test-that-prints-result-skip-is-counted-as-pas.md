---
id: BUG-0100
title: a test that prints RESULT: SKIP is counted as passed by the lab
status: fixed
kind: tooling
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#78
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-lab
commits: 824813c, 6f2d12e, f8b9733, bb7154e
tests: tools/tests/lab_selftest.py (make test)
signature:
docs: docs/10-dev-pipeline.md 10.3/10.4, docs/06-testing.md
related: #78
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/78 (full text, discussion).

`update_check.py` prints `RESULT: SKIP` and exits 0 when no lane outside the run is free. `lab.py` treats that exactly like `RESULT: OK`, so the summary says "all passed" and the test shows as `ok` although it never ran. Only lanes 2..8 are candidates for the free lane, so a run whose lanes plus `--skip` lanes cover them (for example `--lanes 6 --skip 7,8` while other agents use lanes 7 and 8) skips it silently. The setup and in-game update path can then stay untested while the summaries say it passed.

### Where
- The verdict: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L115-L116
- The free lane choice: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L144-L146
- The skip: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/update_check.py#L107-L114

### How to reproduce
Lab self-test with a stub `tools/spore.sh` (as in the other lab issues) and a fake test:

```python
suite.add('skipper', 'python3 -c "print(\'RESULT: SKIP (no free lane)\')"', ('smoke',))
```

It is reported `OK` and the run `PASSED`. Confirmed by reading the code; the same pass path is exercised by the fake `faults` and `crash_after` tests that were run for the related issues.

### Expected
A skipped test is listed as `SKIP` in the console output and in `summary.md`, and is not counted as a pass (for example the summary says "N passed, M skipped", and `results.json` has `skipped: true`).

## Root cause
**What happens**

```python
has_result = 'RESULT:' in text or t['cmd'].startswith('make')
ok = rc == 0 and 'RESULT: FAIL' not in text and 'Traceback' not in text and has_result
```

Any `RESULT:` line that is not `RESULT: FAIL` counts as a pass. `update_check.py`:

```python
if free == '' and '--lane' not in sys.argv:
    print('RESULT: SKIP (no lane outside this lab run is free: this test wipes the mod from its lane)')
    return 0
```

The summary table has only `ok` / `FAIL`, so the skip is invisible unless someone opens the log.

### Confidence
CONFIRMED by reading.

## Fix
`RESULT: SKIP` gives `skipped: true` in results.json, `SKIP` in the console, `skip` in summary.md, and is counted apart ("PASSED: n/m, k skipped"); an exclusive test without a lane 1-5 is recorded the same way.

Suggested in the review:

In `run_one`: `skipped = 'RESULT: SKIP' in text`, return it in the result dict, print `SKIP` instead of `OK `, write `skip` in the summary table, and exclude skipped tests from the "all passed" line (or make the verdict `PASSED (N skipped)` so a reader sees it). Optionally warn at the start of the run when `SPORE_FREE_LANE` is empty and `update` is selected.

## Verification
A self-test of the tool (no game): case skipper. `python3 tools/tests/lab_selftest.py` (in `make test`): all cases PASS on fix/w4-lab; `--tree <main checkout>` (main 7874654): this case FAILs (every case except clean / harness_once and the main-checkout game_log lines fails there). Smoke on lane 2 with the new lab: out/lab/20261028-030019 (13/14, the one FAIL was the self-test's own fake fault line seen by the outer lab, fixed in f8b9733).

Planned in the review:

The `skipper` fake test above: today `OK` / `PASSED`; with the fix `SKIP` in the output and in `summary.md`.

### Related
CLAUDE.md section 3 ("Every feature gets an automated test"; "A test you never saw fail proves nothing").

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #78.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-lab (wave 4), test: lab_selftest.py.
- 2026-10-28: released in 0.7.3.0.
