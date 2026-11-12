---
id: BUG-0853
title: win_tests test_setup_close: the final WM_CLOSE can come before the job's WM_DONE, so the setup never closes
status: fixed
kind: test
stage: All
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9029
role: 
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in: unreleased
branch: fix/w48-trust
commits: fcae15a5
tests: tests/win_tests.cpp (test_setup_close)
signature: test_setup_close CHECK(ended) fails after the install completed
docs: docs/06-testing.md#testing-trust-install
related: BUG-0139
---

## Report
The Windows unit test test_setup_close sometimes times out waiting for the setup process to exit although the
install finished and the version checks pass.

## Root cause
* Observed (code): tests/win_tests.cpp:797-806 posts WM_CLOSE once the new files are all of version 0.11.0; the setup
  still has g_busy set until its worker's WM_DONE is handled, refuses that close (BUG-0139), and no later close is
  sent.
* Confidence high (same code as the fork's; the fork found the race). Test-only, no product change.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages PR #196, BUG-9029); NOT FINAL: taken from the fork, verify on our code and against our rules before using.

The test waits for the Close button (control id 13) to be enabled again (refresh() enables it after WM_DONE clears
g_busy) before posting the final WM_CLOSE, and explicitly checks that the close during the active job was refused.
No installer behaviour changed.

## How to test
Run win_unit repeatedly (e.g. 20 times) with a load on the CPU: FAILs now and then on main, never with the fix. A
deterministic variant: delay the worker's WM_DONE in test mode by 1 s (SPORECOOP_SETUP_TEST_DELAY) so the old test
always fails.

## Fix
Test only, the installer's behaviour is unchanged. The setup can hold its job's WM_DONE in tests
(`SPORECOOP_SETUP_TEST_DONE_DELAY=<ms>`, installer/setup.cpp job_thread); test_setup_close sets it to 2000 ms, so the
race the fork found happens on every run. The test now checks that the close during the job was refused
(`CHECK(!closed_early)`) and waits for the Close button (id 13) to be enabled again (refresh() after WM_DONE) before
its final WM_CLOSE. Switch: win_tests `--old-closewait` (the old wait for the files only; part of `--old`).

## Verification
Under Wine in a private prefix (`SPORE_WIN_UNIT_PREFIX=build/wineprefix python3 tools/tests/win_unit.py`):
- PASS: ALL OK, "Close enabled again after 8.3 s" (out/w48-trust/win_unit_pass.txt).
- FAIL with the old wait: `win_unit.py --old-closewait`: exactly one FAIL, `test_setup_close CHECK(ended)` (the
  setup refused the only close and never ended; out/w48-trust/win_unit_old-closewait.txt).

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-12 (fix/w48-trust): fixed (test made deterministic, the race reproduced every run with the old wait).
