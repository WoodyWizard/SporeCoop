---
id: BUG-9266
title: Cell: a click on History's check that waits (own progress, the switch vote) leaves the screen white until the stage moves on
status: open
kind: bug
stage: Cell
reported: 2026-11-20
source: own progress play test 2026-11-20
role: host, guest
versions: 0.7.15.2
fixed_in:
branch: feat/own-progress
commits:
tests: own_progress_cell
signature:
docs:
related: BUG-1269, BUG-9265, BUG-0315
---

## Report
Own progress play test (lane B, 2026-11-20): a player whose Cell bar is full clicks History's check before the
partner's bar is full; the click is held (the host's) or refused (a guest's) and that player's screen stays plain white
(the Cell-end fade) until the stage moves on: 26 s and more in the test, as long as the partner needs. Same picture as
BUG-1269 (the host's white screen during the switch vote) and the guest's refused click of BUG-0315.

## Root cause
Not analysed. Observed: no cinematic plays at the hook (`cinematic 0` in the log line of the held switch); the white is
already up when CellStage_ToCreature 0xE62440 is called, so it comes from History's check before the event 0x1F.
Inferred: the check starts the Cell-end fade and the stage switch it expects is what ends it.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-20: opened.
