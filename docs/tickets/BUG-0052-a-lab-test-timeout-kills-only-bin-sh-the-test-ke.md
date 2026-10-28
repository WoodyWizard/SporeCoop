---
id: BUG-0052
title: a lab test timeout kills only /bin/sh, the test keeps driving the lane
status: fixed
kind: tooling
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#30
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-lab
commits: 824813c, 6f2d12e, f8b9733, bb7154e
tests: tools/tests/lab_selftest.py (make test)
signature:
docs: docs/10-dev-pipeline.md 10.3/10.4, docs/06-testing.md
related: #30
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/30 (full text, discussion).

When a test hits the lab's 30 min timeout, `lab.py` kills the `/bin/sh` that started it, but the Python test process lives on. The lab then retries the test, or starts the next one, on the same lane while the old process is still sending console commands, killing and starting games, deploying and restoring fixtures. That gives cascading false failures, and a false pass is possible too: the old process can bring the games into a state the new test then checks.

### Where
https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L104-L113

### How to reproduce
No game needed:

```python
import subprocess, time
for cmd in ['SPORE_NET=x python3 -c "import time; time.sleep(31)"',
            'python3 -c "import time; time.sleep(32)"']:
    try:
        subprocess.run(cmd, shell=True, timeout=1.5)
    except subprocess.TimeoutExpired:
        pass
    time.sleep(0.5)
    print(subprocess.run('ps -eo pid,args | grep "[t]ime.sleep(3"', shell=True,
                         capture_output=True, text=True).stdout)
```

Both sleepers are still running after the timeout (seen with `/bin/sh -> dash`). Careful when cleaning up with `pkill -f`: a script whose own command line contains the pattern kills itself (the CLAUDE.md pitfall).

### Expected
On timeout the whole process tree of the test is gone before the lab touches the lane again.

## Root cause
**What happens**

```python
rc = subprocess.run(t['cmd'], shell=True, cwd=tree, env=env, stdout=f, stderr=subprocess.STDOUT,
                    timeout=TIMEOUT).returncode
except subprocess.TimeoutExpired:
    rc = -9
```

On timeout `subprocess.run` sends SIGKILL to its direct child, the shell. `/bin/sh` is dash on the lab machine's distribution and, for these command lines, does not `exec` the command, so the test (`python3 tools/tests/...`) is a grandchild and is not killed. It keeps its own copy of the log file descriptor and keeps writing into the old log. The same applies to commands with an environment prefix (`SPORE_NET=mobile python3 ...`) and to `make test` children.

After the timeout, `has_result` is false, so `attempt()` retries the test on the same lane (`.retry`), or the worker takes the next test.

### Confidence
CONFIRMED by running (the subprocess behaviour); the lab consequence CONFIRMED by reading.

## Fix
Tests start with `start_new_session=True`; a timeout `killpg`s the whole group (games of the lane included; the lab kills the lane afterwards anyway). Ctrl+C on the lab kills the running tests' groups and takes no new test.

Suggested in the review:

Start the test in its own session and kill the group:

```python
p = subprocess.Popen(t['cmd'], shell=True, cwd=tree, env=env, stdout=f, stderr=subprocess.STDOUT,
                     start_new_session=True)
try:
    rc = p.wait(timeout=TIMEOUT)
except subprocess.TimeoutExpired:
    os.killpg(p.pid, signal.SIGKILL)
    p.wait()
    rc = -9
    f.write('\nLAB: TIMEOUT\n')
```

The games are started with `nohup ... &` by `spore.sh run` and would also be in that group; that is fine, since the lab runs `spore.sh kill` on the lane afterwards anyway.

## Verification
A self-test of the tool (no game): case timeout (a command list bash does not exec, timeout 2 s, no process with its marker left; main left one). `python3 tools/tests/lab_selftest.py` (in `make test`): all cases PASS on fix/w4-lab; `--tree <main checkout>` (main 7874654): this case FAILs (every case except clean / harness_once and the main-checkout game_log lines fails there). Smoke on lane 2 with the new lab: out/lab/20261028-030019 (13/14, the one FAIL was the self-test's own fake fault line seen by the outer lab, fixed in f8b9733).

Planned in the review:

Lab self-test with a stub `spore.sh` and `TIMEOUT` patched to 2 s: a fake test `python3 -c "import time; time.sleep(30)"`. After `run_one` returns, no process with that command line may be left. Today one is left; with the fix none.

### Related
CLAUDE.md section 3 (lanes are independent; pitfalls about leftover Wine processes). docs/10-dev-pipeline.md 10.3 ("A test that dies without a RESULT line (harness trouble: start, timeout) is retried once cold").

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #30.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-lab (wave 4), test: lab_selftest.py.
- 2026-10-28: released in 0.7.3.0.
