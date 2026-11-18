---
id: BUG-9029
title: Windows installer test sometimes times out closing after its job
status: fixed
kind: bug
stage: All
reported: 2026-11-04
source: m2-conquest-win-tests-isolated.log
role: Windows regression
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: build/win_tests.exe
signature: test_setup_close ended assertion fails after protected install completes
docs: docs/known-issues.md
related: BUG-0139
---

The isolated Windows suite refused closing during installation as intended,
then timed out waiting for process exit. The installed-file/version checks
passed. Prior run m2-conquest-win-tests.log passed this test. Preserve the
failure separately from the save-slot issue; investigate completion/close
timing before modifying installer behavior.

Root cause: the test posted its final WM_CLOSE when the new files were visible,
before the worker's WM_DONE necessarily reached the UI. The installer correctly
refused that close while busy, and no later close was sent. The test now waits
for the Close control to be enabled, which refresh() does after WM_DONE clears
g_busy. It also explicitly checks that closing during the active job failed.
No installer behavior changed.

FAIL: m2-conquest-win-tests-isolated.log (prior test).
PASS: m2-setup-close-fixed.log (focused), m2-win-completion-fixed.log (full run's
installer check; that run still fails a separate save-slot assertion, BUG-9028).
