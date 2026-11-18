---
id: BUG-1248
title: Cell: the guest's tutorial cutscene of the first part pickup frames empty water
status: open
kind: bug
stage: Cell
reported: 2026-11-18
source: owner: players' video, second analysis
role: guest
versions: <0.7.15
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
A players' video: the tutorial vignette of the first cell part fires; the guest gets the cutscene bars but no cell is in the frame (empty water). Suggested cause: the game's local tutorial cutscene aims at an object or a place the guest's game does not have in focus (the mirror of the host's pool not at the trigger).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
