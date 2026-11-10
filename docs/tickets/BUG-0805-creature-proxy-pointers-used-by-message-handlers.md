---
id: BUG-0805
title: Creature: proxy pointers used by message handlers without a liveness check (MSG_DAMAGE attacker, social prompt / result)
status: open
kind: crash
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
related: BUG-0426 BUG-0453
---

## Report
No player report. In the frame after the guest's game removed a proxy by itself, a host hit or a social message for it can hand a destroyed creature to the game.

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): npc.cpp:2627-2637 (MSG_DAMAGE on the guest) passes attacker = px.c to apply_damage, which reads political_id(attacker) and gives attacker+kCombatOff to TakeDamage (relayed to the pack as stimulus sender, the BUG-0735 path). social.cpp:1129-1131 (K_PROMPT) and 1178-1185 (K_RESULT) call CreaturePlayAnim / set_impressed on npc::proxy(id), which returns px.c without is_destroyed (npc.cpp:2997). guest_tick clears a destroyed proxy only in its own loop, after the message dispatch. Inferred consequence: a dead pack target or stimulus sender (BUG-0453 class) or an animation on a torn-down creature. Confidence: medium (one-frame window).

## Fix
Not fixed yet.

## Fix idea
npc::proxy() and the MSG_DAMAGE lookup return null when !object_alive(c) || is_destroyed(c).

## How to test
Guest: remove a proxy with the game's own destroy (destroy_untracked, a proxy variant of `coop.puppet gamekill`) and inject a MSG_DAMAGE / social prompt for it in the same frame; assert no TakeDamage with that attacker (dmglog) and no fault. Main: the call goes through.

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
