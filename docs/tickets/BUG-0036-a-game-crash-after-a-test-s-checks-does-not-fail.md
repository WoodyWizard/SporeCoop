---
id: BUG-0036
title: a game crash after a test's checks does not fail the test in the lab
status: fixed
kind: tooling
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#14
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-lab
commits: 824813c, 6f2d12e, f8b9733, bb7154e
tests: tools/tests/lab_selftest.py (make test)
signature:
docs: docs/10-dev-pipeline.md 10.3/10.4, docs/06-testing.md
related: #14
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/14 (full text, discussion).

CLAUDE.md section 3 says "A crash after the checks is still a crash: a test fails when a game died or wrote a crash report during or after it (the lab checks this)". The lab does not check this. A test that prints `RESULT: OK` and then crashes a game (during teardown, `coop.leave`, the next frames) is reported as OK, so the smoke run that gates merges to `main` can pass a crashing build.

### Where
- `tools/lab.py` `run_one`, the pass/fail decision: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L99-L124
- `tools/tests/twoinst.py` `Checks.faults()`, which reads the monitor events only once: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/twoinst.py#L345-L358
- The rule: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/CLAUDE.md#L99-L100

### How to reproduce
No game needed. A copy of the repo with `tools/spore.sh` replaced by a stub (`#!/bin/bash` + `echo stub`) and this driver (run with the copy's root as argument):

```python
import os, sys, argparse
R = sys.argv[1]
sys.path.insert(0, os.path.join(R, 'tools'))
import lab, suite
lab.preflight = lambda: True
logs = os.path.join(lab.lane_game(1), 'SporebinEP1', 'mods', 'logs')
suite.T[:] = []
suite.add('crash_after', 'python3 -c "print(\'RESULT: OK\');import os;os.makedirs(\'%s\',exist_ok=True);'
          'open(\'%s/crash-20261027.txt\',\'w\').write(\'crash\')"' % (logs, logs), ('smoke',))
a = argparse.Namespace(branch=None, tests='smoke', repeat=1, skip='', lanes=1, no_build=True, cold=False, retry=1)
sys.exit(lab.cmd_run(a))
```

Output:

```
  OK   crash_after     lane 1     0 s
PASSED: 1/1 in 0.1 min -> out/lab/<run>/summary.md
```

A new crash report in the lane's log folder, and the run says "all passed".

### Expected
The test fails (or at least is marked FAIL in the summary) when, between its start and a few seconds after its end, a game of its lane wrote a crash or hang report, or a game that was running at the start no longer answers its console.

## Root cause
**What happens**

`run_one` decides only from the exit code and the log text:

```python
has_result = 'RESULT:' in text or t['cmd'].startswith('make')
ok = rc == 0 and 'RESULT: FAIL' not in text and 'Traceback' not in text and has_result
```

It never looks at the lane's `SporebinEP1/mods/logs` for new `crash-*.txt` / `report-*.txt` files (the files SporeKit writes, see `tools/tests/diag_check.py`), and never checks that both games still answer their console at the end. `save_game_logs` copies the game logs but nobody reads them.

Inside the test, `twoinst.Monitor` notices a dead console, but `Checks.faults()` reads `Monitor.events` at the moment it is called, just before `result()`. Anything after that (teardown, the last commands, the games crashing a second later) is not seen by anyone. The next test on the lane then starts cold and the crash is gone from the record.

### Confidence
CONFIRMED by running (fake test above) and by reading.

## Fix
`lab.run_one` records the lane's `crash-*.txt`, `crash-*.dmp`, `report-*.txt` before the test; after it pings both consoles, waits 5 s (`lab.AFTER`) when a game answered, pings again; a new report or a game that stopped answering fails the test (`FAIL lab: ...` in the summary, reports copied into the run dir). `suite.add(..., reports=True)` exempts tests that crash on purpose: diag, kit_crash, kit_freeze, exit_thread.

Suggested in the review:

In `run_one`:
1. Before the test, record the set of `crash-*`, `report-*` files in `lane_game(lane)/SporebinEP1/mods/logs`.
2. After the test exits, wait ~5 s, then ping both consoles of the lane (`2*lane-1`, `2*lane`) if they answered before the test; take the new crash/report files.
3. `ok = ok and not new_reports and not died`; put the file names into `fails` so the summary shows them, and copy the reports into the run directory.

## Verification
A self-test of the tool (no game): cases crash_after, died_after. `python3 tools/tests/lab_selftest.py` (in `make test`): all cases PASS on fix/w4-lab; `--tree <main checkout>` (main 7874654): this case FAILs (every case except clean / harness_once and the main-checkout game_log lines fails there). Smoke on lane 2 with the new lab: out/lab/20261028-030019 (13/14, the one FAIL was the self-test's own fake fault line seen by the outer lab, fixed in f8b9733).

Planned in the review:

A self-test of `lab.py` with fake tests and a stub `spore.sh` (as above): `crash_after` must be FAIL with the fix and is OK today. A second fake test that writes no report must stay OK.

### Related
CLAUDE.md section 3 ("A crash after the checks is still a crash", "the lab checks this"). The separate problem that several suite tests ignore faults is filed on its own.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #14.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-lab (wave 4), test: lab_selftest.py.
- 2026-10-28: released in 0.7.3.0.
