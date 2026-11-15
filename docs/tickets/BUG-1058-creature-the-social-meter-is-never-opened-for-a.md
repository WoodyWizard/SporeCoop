---
id: BUG-1058
title: Creature: the social meter is never opened for a social game whose first open found no meter window
status: fixed
kind: bug
stage: Creature
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-23
role: guest
versions: 
fixed_in: 0.7.14
branch: fix/w49-small
commits: f4aca716, 3e2de219
tests: social_meter_retry
signature:
docs: docs/06-testing.md#testing-t1-small
related: BUG-0808
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-23 (social.cpp). Not seen at runtime, found by code reading.
A guest's social game with an animal (the host runs it) shows the game's meter (face, bar) on the guest's screen. When
the meter window is not there at the first open (meter_ui null), the open is dropped but the module already counts
the meter as open for that animal, so it is never opened again for that game: the guest plays the social game without
the meter.

## Root cause
Reproduced with a test hook (the first meter task finds no meter window): meter_tick marked the meter open for the
engaged Kook when it queued the open, the task dropped the open, and the guest played the whole social game without
the meter (opened=0, visible_at_win=0). Whether meter_ui is null at an engagement on a player's PC is not seen.

## Fix idea
Set g_meter_npc only in the task after a real open (the task already does it), or reset it when the task finds no
meter. Switch idea `coop.fix social.meter_retry off`.

## How to test
creature fixture: a test command that makes meter_ui return null for one task (or opens the social game during a
frame the creature HUD is not up yet), then the social game; assert the game's meter object is visible on the guest
(meter_dump visible flag) within 1 s. Main: never visible: FAIL.

## Fix
Only the task's real open marks the meter open; a missed open is queued again 0.25 s later. Switch
`coop.fix social.meter_retry off` (0.7.13). Test command `coop.social meter_null <n>`; `coop.social meter` shows
`missed=`.

## Verification
Test `social_meter_retry` (coop_creature_social_meter.py --meter-null 1).
FAIL (SPORE_FIX_OFF=social.meter_retry): out/lab/20261113-031541 (opened=0, visible_at_win=0). PASS: out/lab/20261113-031904,
out/lab/20261113-033100-2 (open for the first Kook after the miss, up at the win).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-small (package T1).
- 2026-11-14: released in 0.7.14.
