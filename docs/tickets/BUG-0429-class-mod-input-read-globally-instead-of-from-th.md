---
id: BUG-0429
title: class: mod input read globally instead of from the game window (umbrella)
status: open
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
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
Fixed per case: BUG-0090, 0145, 0149. Not covered: F9 toggles the co-op window from any program (coop_ui.cpp:962 GetAsyncKeyState, no focus check) (obs); Sporepedia search, editor naming, save-name dialogs.
General fix: all mod input from the game window's messages while focused. Outcome check: key/click in another window changes nothing.
Source: docs/generalisation-plan.md G13.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
