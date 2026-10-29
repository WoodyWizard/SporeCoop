---
id: BUG-0249
title: Tools: the lab checks the update test for crashes on the wrong lane, and counts a crash of the owner's own Spore against a lane-1 test
status: open
kind: tooling
stage: All
reported: 2026-10-28
source: GitHub #183 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
