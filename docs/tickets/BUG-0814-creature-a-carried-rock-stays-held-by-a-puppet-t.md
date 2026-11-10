---
id: BUG-0814
title: Creature: a carried rock stays held by a puppet that was destroyed (frozen copy, possible read of the freed socket) (suspected)
status: open
kind: crash
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md#creature-hold-slots
related: BUG-0458 BUG-0426
---

## Report
No player report. When a partner who carries a stick or rock dies, hides, enters an editor or leaves, the copy in its puppet's hand is never released: at least a rock frozen in the air; at worst a crash when the object follows the freed creature's socket. Suspected.

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): player_rules.cpp carry_rx K_HOLD (782-798) puts the copy into the puppet with Creature_HoldObject (socket attach, obj+0x71 = 1, physics +0x1F0 = 3, docs/re/creature.md#creature-hold-slots). Nothing releases it before coop.cpp despawn() / forget_puppets(); g_carried[player] is not cleared on a leave or puppet change (no PLAYER_STATE in player_rules), and the next K_PUT calls o_Release(new_puppet, rock) for a rock the new puppet does not hold. Not known: whether DestroyInstance of a holder releases its hold slots. Confidence: medium on the stuck rock, low on the crash.

## Fix
Not fixed yet.

## Fix idea
Release the copy (Creature_ReleaseObject on the old puppet) before every puppet despawn / forget, and clear g_carried per player (PLAYER_STATE); RE first what DestroyInstance does with hold slots.

## How to test
Guest picks up a stick (`coop.rules carry` shows hold_rx/done on the host), then `coop.team die` on the guest. On the host the rock's +0x71 must be 0 and its physics normal within 1 s; no fault. Main: held flag stays 1 at the old spot.

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
