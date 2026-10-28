---
id: BUG-0073
title: a lab test that FAILs and then passes on retry counts as passed
status: fixed
kind: tooling
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#51
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-lab
commits: 824813c, 6f2d12e, f8b9733, bb7154e
tests: tools/tests/lab_selftest.py (make test)
signature:
docs: docs/10-dev-pipeline.md 10.3/10.4, docs/06-testing.md
related: #51
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/51 (full text, discussion).

`lab.py` reruns every failed test once on a clean lane. If the rerun passes, the test is reported `ok (flaky)` and the run is "all passed", and that happens for genuine `RESULT: FAIL` assertions too, not only for harness trouble. A bug that shows up in half of the runs therefore passes the smoke gate for merges. A run that is meant to show a fix's test FAILING on `main` can come back as PASSED.

### Where
- Retry and result: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L190-L196
- Overall verdict: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L229
- Docstring that limits the retry to runs without a RESULT line: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L12-L16

### How to reproduce
A copy of the repo with `tools/spore.sh` replaced by a stub, and a fake test that fails on its first run only, through `lab.cmd_run` (`lab.preflight` patched to `lambda: True`):

```python
cnt = os.path.join(R, 'out', 'cnt')
suite.add('fails_once', 'python3 -c "import os;p=\'%s\';n=os.path.exists(p);open(p,\'a\').close();'
          'print(\'  FAIL the guest sees the host\' if not n else \'  ok\');print(\'RESULT:\', \'OK\' if n else \'FAIL\');'
          'exit(0 if n else 1)"' % cnt, ('smoke',))
a = argparse.Namespace(branch=None, tests='smoke', repeat=1, skip='', lanes=1, no_build=True, cold=False, retry=1)
lab.cmd_run(a)
```

Output:

```
  OK   fails_once      lane 1     0 s   (flaky)
PASSED: 1/1 ...
```

and `summary.md` says `**all passed**`.

### Expected
Either a `RESULT: FAIL` is a failure of the run (the retry is only for harness trouble, as the docstring says), or a flaky result makes the run's verdict something other than PASSED (e.g. `PASSED with N flaky`, exit code non-zero for the merge gate).

## Root cause
**What happens**

```python
if not r['ok'] and a.retry and not r.get('env'):  # once more on a clean lane: passing = flaky
    r2 = run_one(t, lane, tree, logdir, False, '.retry')
    r2['flaky'] = r2['ok']
    ...
    return r2, False
...
ok = all(r['ok'] for r in results)
```

The condition does not look at `r['harness']`, so a test that printed `RESULT: FAIL` is retried as well. The retry's `ok` replaces the first result, and `ok` for the whole run ignores `flaky`. The module docstring says: "A test that dies without a RESULT line (harness trouble: start, timeout) is retried once cold and marked flaky when it passes then". docs/10-dev-pipeline.md line 61 says the broader thing ("After a failure ... runs once more: passing then = flaky"), so the code and its two descriptions disagree too.

### Confidence
CONFIRMED by running.

## Fix
Only harness trouble (no RESULT line, a traceback, a timeout, and no lab failure) is retried; a RESULT: FAIL, a crash or a fault is a failure of the run. Flaky (harness) results are counted in the summary line. docs/10-dev-pipeline.md 10.3 updated.

Suggested in the review:

```python
if not r['ok'] and a.retry and r['harness'] and not r.get('env'):
```

and keep the existing `--retry` meaning for harness trouble. If retrying real failures is wanted for long bots, make it opt-in (`--retry-fails`), and count `flaky` results as not ok in `ok = all(...)` for the exit code. Update docs/10-dev-pipeline.md 10.3 to match.

## Verification
A self-test of the tool (no game): cases fails_once (FAIL now, ok (flaky) on main), harness_once (still ok (flaky)). `python3 tools/tests/lab_selftest.py` (in `make test`): all cases PASS on fix/w4-lab; `--tree <main checkout>` (main 7874654): this case FAILs (every case except clean / harness_once and the main-checkout game_log lines fails there). Smoke on lane 2 with the new lab: out/lab/20261028-030019 (13/14, the one FAIL was the self-test's own fake fault line seen by the outer lab, fixed in f8b9733).

Planned in the review:

Lab self-test with the `fails_once` fake test: today PASSED (flaky); with the fix the run FAILS. A fake test that dies without a RESULT line on its first run and passes on the second stays `ok (flaky)`.

### Related
CLAUDE.md section 3 ("A test you never saw fail proves nothing"), section 4 ("`main` always works: only merged branches that passed `lab.py run smoke`").

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #51.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-lab (wave 4), test: lab_selftest.py.
- 2026-10-28: released in 0.7.3.0.
