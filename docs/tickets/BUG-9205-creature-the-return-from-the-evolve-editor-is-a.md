---
id: BUG-9205
title: Creature: the return from the evolve editor is a long black screen with no loading screen
status: open
kind: bug
stage: Creature
reported: 2026-11-14
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9202 BUG-9181
---

## Report
Owner play test 2026-11-14: when a guest finished evolving it was stuck on a black screen for a while. The owner asked whether it is loading and whether it can get the loading screen.

## Root cause
Observed (guest log, the QA copy capped at 10 fps): `stage: Editor -> Creature` at 18:54:35.9, then `busy=cinematic,input_lock,paused`, `cinematic,input_lock` until control came back at 18:54:57.6, with 200-530 ms stalls while the new body builds. It is not a world load: it is the game's own fade from black and the egg-hatch scene after the editor, stretched by the 10 fps cap and the stalls. The BUG-9202 cover starts only on an arriving world (reload) or the menu with the host playing (join), so it never starts here. Fix idea: start the cover on the editor-to-stage step while the frame is dark, end it on the brightness readback alone (keep the hatch scene visible, BUG-9181).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-14: opened.
