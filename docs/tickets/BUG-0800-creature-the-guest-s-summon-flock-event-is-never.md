---
id: BUG-0800
title: Creature: the guest's Summon Flock event is never forgotten: the host's own later flock follows the guest's creature
status: open
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md#creature-drama-events
related: BUG-0736 BUG-0418 BUG-0110
---

## Report
No player report. After a guest used Summon Flock once, the host's own flock follows the guest's creature; while a guest's flock is active the host's or a second guest's Summon Flock does nothing (the guest's own game already refused its event, the ability is spent). Suspected in part (depends on one kind-8 event object).

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): drama.cpp summon_for sets g_flock_event / g_flock_puppet and nothing clears them; h_SummonUpdate swaps cGameNounManager +0x54 to g_flock_puppet whenever ev == g_flock_event and the puppet is still a live puppet. Inferred: the Drama Manager builds its events once from the prop group and reuses them (docs/re/creature.md#creature-drama-events), so the host's own summon runs the same event object. The swap is also outside the swap ledger (no restore after a fault, BUG-0110 class). Confidence: medium.

## Fix
Not fixed yet.

## Fix idea
Clear g_flock_event / g_flock_puppet when the event ends (its active byte +0xC goes 0) or when h_Start runs it without our swap; put the swap into game::swap_open so a fault restores the avatar.

## How to test
Guest uses Summon Flock (coop_party_defence.py --flock path), wait until the event ends, host `coop.drama start 8`. Assert the host's flock members follow the host's avatar (distance to the host vs the guest's puppet over 20 s). Main: they go to the guest.

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
