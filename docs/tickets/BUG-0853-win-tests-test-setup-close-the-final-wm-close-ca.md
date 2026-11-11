---
id: BUG-0853
title: win_tests test_setup_close: the final WM_CLOSE can come before the job's WM_DONE, so the setup never closes
status: open
kind: test
stage: All
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9029
role: 
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in:
branch:
commits:
tests:
signature: test_setup_close CHECK(ended) fails after the install completed
docs:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
