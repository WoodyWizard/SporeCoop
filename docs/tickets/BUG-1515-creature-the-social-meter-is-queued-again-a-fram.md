---
id: BUG-1515
title: Creature: the social meter is queued again a frame after a social win (hidden at the finish) (CR-13)
status: open
kind: bug
stage: Creature
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w82-creature
commits: e44b4ff0, 9a4c9bc3
tests: tools/tests/coop_creature_social_meter.py (creature_social_meter, twin creature_social_meter_hold_off)
signature:
docs: docs/re/creature.md#creature-meter-hold
related: 
switch: social.meter_hold
---

## Report
External static audit of 0.7.15.5, item CR-13 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-cr-tr.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-C1 (fix/w82-creature). A behaviour change gets a switch and a twin.

## Root cause
Inferred from the code (social.cpp, as the audit's verify row says): the guest's K_RESULT handler queues the
win (meter op 2: SetBar full, SocialMeter_Finish, the face, the win message) and sets `g_engaged = -1`; op 2 does not
clear `g_meter_npc`, so the next frame `meter_tick` saw `g_engaged < 0 && g_meter_npc >= 0` and queued the close (op
3, SetVisible 0): the finish of the guest's meter was hidden one frame after it began. The game's own session hides
its meter at the session end 0xD9F7A0, after the win's reaction (docs/re/creature.md#creature-guest-meter), not in the
next frame. Parts (1), (3), (4) of the audit row are REAL-THEORETICAL and not changed.

## Fix
social.cpp: op 2 records the time of the win (`g_meter_won_at`); `meter_tick` holds the close of a won meter for
`kWinHold` = 1.5 s (a new engagement still opens at once: op 0 clears the time, as op 3 does). Tests: the guest samples
the game's own visible byte of the meter window 0.5 s after the win (`coop.social meter`: `visible_after_win=`).
Switch `social.meter_hold` (off: closed the next frame, 0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: creature_social_meter, creature_social_meter_hold_off).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run creature_social_meter --lane N --retry 0 --no-build` (`visible_after_win=1`, key frame
  meter_finish: the full arc with the face on the guest's screen).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run creature_social_meter_hold_off --lane N --retry 0 --no-build` (`visible_after_win=0`).
* Regression: social_meter_retry, social_leave_meter.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-C1), branch fix/w82-creature.
- 2026-11-23: fix and tests on fix/w82-creature (e44b4ff0); waiting for the night lab run.
