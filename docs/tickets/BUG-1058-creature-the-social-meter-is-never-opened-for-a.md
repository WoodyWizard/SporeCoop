---
id: BUG-1058
title: Creature: the social meter is never opened for a social game whose first open found no meter window
status: open
kind: bug
stage: Creature
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-23
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0808
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-23 (social.cpp). Not seen at runtime, found by code reading.
A guest's social game with an animal (the host runs it) shows the game's meter (face, bar) on the guest's screen. When
the meter window is not there at the first open (meter_ui null), the open is dropped but the module already counts
the meter as open for that animal, so it is never opened again for that game: the guest plays the social game without
the meter.

## Root cause
Observed in code (main efd2e0ba); whether meter_ui is null at an engagement is inferred (confidence low-medium):
mods/coop/social.cpp:622 meter_tick sets `g_meter_npc = g_engaged` right after queueing the open (op 0); the task
meter_run (social.cpp:546) `break`s when `!m` (social.cpp:552) and the open never happens; meter_tick's next pass sees
g_engaged == g_meter_npc and does nothing.

## Fix idea
Set g_meter_npc only in the task after a real open (the task already does it), or reset it when the task finds no
meter. Switch idea `coop.fix social.meter_retry off`.

## How to test
creature fixture: a test command that makes meter_ui return null for one task (or opens the social game during a
frame the creature HUD is not up yet), then the social game; assert the game's meter object is visible on the guest
(meter_dump visible flag) within 1 s. Main: never visible: FAIL.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
