---
id: BUG-0808
title: Creature: the social meter stays on the guest's screen after Leave or a disconnect in the middle of a social game
status: fixed
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-creature
commits: 86798932
tests: social_leave_meter
signature:
docs: docs/re/creature.md#creature-c2-audit-0713, docs/06-testing.md#testing-c2-creature-0713
related: BUG-0179 BUG-0525
---

## Report
No player report. A guest that leaves (or loses the host) while its social meter is open keeps the meter (face and bar) on screen in its single-player game.

## Root cause
Observed at runtime (confirms the code reading). social::on_session_end cleared g_engaged and g_meter_npc together; the meter is hidden only by meter_tick (task op 3), which runs while the session lives. A guest that left with `coop.leave` while its Kook game was open kept the game's meter object visible (its layout's visible byte 1) in its own game (lab 20261112-072224).

## Fix
on_session_end queues the meter close (meter_task op 3, outside the frame) when a meter is open. Switch `coop.fix social.meter_end off`: the old end.

## Fix idea
In on_session_end, when g_meter_npc >= 0, hide the meter (SetVisible 0) outside the frame before clearing the ids.

## How to test
Guest engages a proxy (`debug_social <id> 0`), meter dump `visible=1`; disconnect the host. Assert visible=0 after 1 s (fix) vs 1 (main).

## Verification
Test `social_leave_meter` (coop_creature_social_leave.py --meter): real engagement of a Kook on the guest, the meter up, `coop.leave`; the game's meter object (`coop.social meter` visible=) on the guest 3 s later.
* FAIL: out/lab/20261112-072224 (`SPORE_FIX_OFF=social.meter_end`, lane 6): visible=1 after the leave.
* PASS: out/lab/20261112-072244 (lane 7): visible=0.
* PASS: out/lab/20261112-080216 (lane 6, the switch on; only nest.mirror_epoch off in that run): the second PASS.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-creature, C2 of 0.7.13): fixed (social.meter_end); FAIL 20261112-072224, PASS 20261112-072244, 20261112-080216.
