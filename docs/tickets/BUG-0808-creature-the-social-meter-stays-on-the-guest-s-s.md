---
id: BUG-0808
title: Creature: the social meter stays on the guest's screen after Leave or a disconnect in the middle of a social game
status: open
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: 
related: BUG-0179 BUG-0525
---

## Report
No player report. A guest that leaves (or loses the host) while its social meter is open keeps the meter (face and bar) on screen in its single-player game.

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): social::on_session_end (social.cpp:1196-1206) sets g_engaged = -1 and g_meter_npc = -1 together; the meter is hidden only by meter_tick's task op 3 (605-619), queued when g_engaged < 0 && g_meter_npc >= 0, and guest_tick no longer runs once the session is over. Not checked: whether the game's own code hides it later. Confidence: medium.

## Fix
Not fixed yet.

## Fix idea
In on_session_end, when g_meter_npc >= 0, hide the meter (SetVisible 0) outside the frame before clearing the ids.

## How to test
Guest engages a proxy (`debug_social <id> 0`), meter dump `visible=1`; disconnect the host. Assert visible=0 after 1 s (fix) vs 1 (main).

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
