---
id: BUG-0279
title: Cell: only one of two new players can eat (Discord report)
status: open
kind: bug
stage: cell
reported: 2026-10-29
source: Discord report 2026-10-29
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0278
---

## Report
The same Discord report as BUG-0278 (2026-10-29, two players who had just bought the game; version,
build (GOG / Steam) and roles not given): "even at stage 1 only 1 of us could eat". Which player
(host or guest) and which food (plants, meat, the shared food bar not moving) is not known. The lab's
Cell tests (cell_coop, cell_meat, cell_net) pass on GOG 2024 with fixtures, not with a game the stage
wizard made right before joining.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
