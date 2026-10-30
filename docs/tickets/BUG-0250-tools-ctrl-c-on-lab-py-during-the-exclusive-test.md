---
id: BUG-0250
title: Tools: Ctrl+C on lab.py during the exclusive tests (ui, update) leaves the test driving its lane and the games running
status: fixed
kind: tooling
stage: All
reported: 2026-10-28
source: GitHub #184 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: 0.7.3.1
branch: fix/w5-tools
commits: e647496
tests: make test (lab_selftest.py interrupt_exclusive, interrupt_sigterm, free_lane_locked, interrupt_retry, nohup)
signature:
docs: docs/06-testing.md#testing-lab-free-lane
related:
---

## Report
From GitHub issue #184: https://github.com/WoodyWizard/SporeCoop/issues/184 (the full analysis, links and the scenario are there).

**Summary**
Since BUG-0052, `lab.py` starts every test in its own session so that a timeout can kill the whole process group. As a side effect, the terminal's Ctrl+C no longer reaches the test. Only the parallel phase catches `KeyboardInterrupt` and kills the running test groups. If Ctrl+C comes during the exclusive tests (`ui` in smoke, `update`), the lab exits and the test keeps driving its lane. The final `spore kill` of the run's lanes is skipped on any interrupt. Severity: low (developer tooling). An orphaned `update` test goes on wiping and installing the mod on its lane unseen, and a later run on that lane races with it.

**Where**
- The test in its own session (BUG-0052): https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260facd5001ec395ec859/tools/lab.py#L139-L156
-

## Root cause
Confirmed (stubbed lab): in the exclusive phase Ctrl+C raised inside run_cmd's wait, whose finally removed the test from _procs, nothing killed its group and the final spore kill was skipped; SIGTERM had no handler.

## Fix
lab.py: one `shutdown()` (idempotent, SIGINT ignored while it runs): stop flag, kill every test group of ours, spore kill of the run's lanes, the free lane only when its BUG-0211 lock can be taken non-blocking; run_cmd kills its group on KeyboardInterrupt and a test started after the stop flag at once; attempt() starts no env retry or .retry after it; SIGTERM raises KeyboardInterrupt in cmd_run (restored afterwards); no SIGHUP handler; the final per-lane kill is in a finally.

## Verification
lab_selftest.py, the lab in a child process interrupted after 8 s. FAIL against b423f56's lab: interrupt_exclusive and interrupt_sigterm leave the sleeping test running (its pid found in /proc) and kill nothing after the signal; free_lane_locked has no free-lane handling. PASS on fix/w5-tools: all 6 cases ok (nothing left, lanes 5 and free lane 6 killed, lane 6 left alone while another process holds its lock, no .retry, SIGHUP ignored keeps the lab running). interrupt_retry also passes on b423f56. no-fail-run: the manual check with a real `ui` test was not done (exclusive).

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-tools (e647496).
- 2026-10-29: released in 0.7.3.1.
