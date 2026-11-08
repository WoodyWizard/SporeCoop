---
id: BUG-0743
title: tests: lab_selftest interrupt_exclusive / free_lane_locked / interrupt_retry fail when the lab runs with SIGINT ignored (a lab started in the background)
status: fixed
kind: test
stage: Tests
reported: 2026-11-08
source: rel/0.7.11 out/lab/20261108-035548-2 unit#1
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-g4-tests
commits:
tests: unit (make test: lab_selftest interrupt_cases)
signature:
docs: docs/06-testing.md#testing-g4-tests
related: BUG-0250
---

## Report
The lab's `unit` test (make test, tools/tests/lab_selftest.py) failed now and then while other lanes were busy: rel/0.7.11
out/lab/20261108-035548-2 unit#1 "FAIL interrupt_exclusive: the free lane is cleaned when nobody holds its lock",
"FAIL free_lane_locked ...", "FAIL interrupt_retry: no .retry test starts after Ctrl+C (['noresult.retry.log'])";
interrupt_sigterm passed. Reproduce: run tools/tests/lab_selftest.py from a process that ignores SIGINT
(`python3 -c "import signal,os; signal.signal(signal.SIGINT, signal.SIG_IGN); os.execvp(...)"`).

## Root cause
Observed (scratch probes of interrupt_cases, out/g4/ in the fix/w46-g4-tests worktree): the three cases fail every time
when the self-test runs with SIGINT ignored, and pass every time in a terminal. A process started in the background by a
non-interactive shell (`... &`, nohup, an agent's or the lab's queue shell) has SIGINT ignored; Python then installs no
KeyboardInterrupt handler, and the lab child of interrupt_cases inherits it: the test's Ctrl+C did nothing, the test ran
its 30 s, failed for want of a RESULT line and was retried (`.retry`), the child was killed after 60 s and the free lane
never cleaned. SIGTERM has the lab's own handler, so interrupt_sigterm passed. Not a lab bug: a lab really started like
that cannot get a Ctrl+C at all. Disproved on the way: a fixed 8 s wait being too short under load, and a Popen.wait
lock (a scratch change was made and removed again).

## Fix
Test only (tools/tests/lab_selftest.py): the interrupted lab child sets SIGINT to Python's default handler first, as
in a terminal; the signal is sent once the test's own process runs (its marker in /proc), not after a fixed 8 s.

## Verification
FAIL (before, SIGINT ignored): out/g4/ (scratch) probe of 5e15c390's lab_selftest: interrupt_exclusive, free_lane_locked,
interrupt_retry fail, the same three as the report (also with only the wait change). PASS with the fix under SIGINT
ignored (FAILS []) and in a terminal. Lab: the `unit` runs of this branch (out/lab, queue unit1) in the History.
no-fail-run: the FAIL is a scratch probe, not a lab run directory (the lab's own unit run of the old code is the
report's 20261108-035548-2).

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-g4-tests): found (SIGINT ignored in background labs) and fixed in the self-test.
