---
id: BUG-0249
title: Tools: the lab checks the update test for crashes on the wrong lane, and counts a crash of the owner's own Spore against a lane-1 test
status: fixed
kind: tooling
stage: All
reported: 2026-10-28
source: GitHub #183 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: 0.7.3.1
branch: fix/w5-tools
commits: e647496
tests: make test (lab_selftest.py free_lane_*, owner_crash_lane1, test_crash_lane1)
signature:
docs: docs/06-testing.md#testing-lab-free-lane
related:
---

## Report
From GitHub issue #183: https://github.com/WoodyWizard/SporeCoop/issues/183 (the full analysis, links and the scenario are there).

**Summary**
Since BUG-0036 the lab fails a test when a new crash or hang report appears in its lane's log folder, or when a game of the lane stops answering after the test. Two cases look at the wrong games:
- The `update` test (exclusive since BUG-0211) plays on `SPORE_FREE_LANE`, while the lab watches `ex_lane`. A crash of the game that the update test starts is never counted.
- Lane 1's log folder is the real install, which the owner also plays from. A crash or hang report of the owner's own Spore during a lane-1 test fails that test.

Severity: minor (lab verdicts). The first lets a crash in the setup / in-game update path pass the lab; the second is a false failure.

**Where**
- Report files and liveness by lane: https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260facd

## Root cause
Confirmed by reading: run_one snapshots and pings only the lane it gave the test; `update` plays on SPORE_FREE_LANE. Lane 1's log folder is the owner's install and report names cannot tell instance 1 of a test from the owner's game.

## Fix
suite flag `uses_free_lane` (update); update_check.py prints `LAB-WATCH lane=N since=T` after taking the lane lock and wiping the lane; run_one then counts crash / hang reports there newer than `since` (2 s margin for the kernel's coarse file clock) and a game of that lane that dies; the result line shows `lane 5 (+6)`. SporeKit crash reports get an Environment line `run: instance <N>, profile set|own, pid <pid>` (a flag only, no name or path); on lane 1 a report of instance 1 with profile own is ignored, any other or one without the line counts.

## Verification
lab_selftest.py (make test). FAIL against b423f56's lab (--tree): free_lane_crash and free_lane_hang pass unnoticed, owner_crash_lane1 fails the test. PASS on fix/w5-tools: all 7 cases ok. no-fail-run: not run with a real game (the update test is exclusive and owned by the orchestrator; kit_crash / diag were not run for the new run: line).

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-tools (e647496); the new run: line in sporekit/diag.cpp needs a game restart (no hot reload of sporekit.dll).
- 2026-10-29: released in 0.7.3.1.
