---
id: BUG-1435
title: The host re-sends its world about every 50 s: a guest passing through the galaxy menu to load the world it was just sent counts as left to the main menu after 5 s
status: open
kind: bug
stage: Creature
reported: 2026-11-22
source: discord
role: all
versions: 0.7.15.5
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
The host re-sent its world 8 times, about every 53 s (the players saw 'it constantly reloads the world'). Full analysis: out/research/dc-report-20261122-world-switch.md in the main checkout (not in git; six SporeCoop-report zips, coop 0.7.15.5, Steam).

## Root cause
Confirmed on the session clock in 7 rounds: during the ~40 s transfer the guest keeps playing the stage, so joined_phase becomes true; then the guest goes through the galaxy menu to load the world (5.2-5.4 s), and the host's 5.0 s left-to-menu window (session_glue.cpp:444 and :461, since 2026-10-23) fires, so the host sends the world again. Not fixed in rel/0.7.16.

## Fix
Proposed: set joined_phase only after the guest loaded the last world it was sent (world id or load serial, ~30 s timeout). Test: exactly one send; must FAIL on rel/0.7.16.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-22: opened.
