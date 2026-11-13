---
id: BUG-1070
title: Tests: same_view_tribe's host game gone 7 s into Tribe (console broken pipe, no crash report, fixture check failed)
status: open
kind: test
stage: Tribe
reported: 2026-11-13
source: fix/w49-mirror final check, lab 20261113-052132 lane 7
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
fix/w49-mirror final check, lane 7 (out/lab/20261113-052132): same_view_tribe started cold, the guest joined, the host's
game log ends 6.7 s into Tribe ("Bob appeared in the world"), no fault line, no crash report in the lane; the test's
fixture check found nothing ("the player tribe has members, another tribe exists" both missing) and its next console
call broke (BrokenPipeError). The same test passed on lane 6 in the same final check (20261113-051820-2) and on both lanes in the reruns (20261113-054834, 20261113-054834-2).
Seen once. Reproduce: `lab.py run same_view_tribe --lane 7 --repeat 5`. Not analysed (the game's process was gone; a
Wine-side exit cannot be told from a test-side kill with what the lab kept).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
