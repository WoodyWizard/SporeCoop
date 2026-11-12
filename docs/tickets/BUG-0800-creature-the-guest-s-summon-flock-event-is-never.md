---
id: BUG-0800
title: Creature: the guest's Summon Flock event is never forgotten: the host's own later flock follows the guest's creature
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in:
branch: fix/w48-creature
commits: 2b0c843d
tests: creature_flock_forget
signature:
docs: docs/re/creature.md#creature-c2-audit-0713
related: BUG-0736 BUG-0418 BUG-0110
---

## Report
No player report. After a guest used Summon Flock once, the host's own flock follows the guest's creature; while a guest's flock is active the host's or a second guest's Summon Flock does nothing (the guest's own game already refused its event, the ability is spent). Suspected in part (depends on one kind-8 event object).

## Root cause
The code path is real, its effect is not visible. Observed at runtime: the Drama Manager starts the same summon event object for the guest's relayed flock and for the host's own later one (lab 20261112-072224, -073101: the same event pointer), and with the mod's switch off the swap ran on the host's own event (its `updates` grew). But SummonEvent_Update 0xD512A0 (Ghidra) only gives the members their hearts while the event runs; the "follow the avatar" stimulus and the let-go of far members come in its end branch, and by then the flock has left the pack and the game's NPC list in every run, with the fix on or off (lab 20261112-074517 / -074532). The host's own flock came to the host's creature in every run (073101 off: within 6-40 m; 072224 off: 4 of 5 within 25 m).

## Fix
Kept as hardening (no visible bug): the event is forgotten when the game starts it for its own avatar; the swap is in the swap ledger. Switch `coop.fix creature.flock_forget off`.

## Fix idea
Clear g_flock_event / g_flock_puppet when the event ends (its active byte +0xC goes 0) or when h_Start runs it without our swap; put the swap into game::swap_open so a fault restores the avatar.

## How to test
Guest uses Summon Flock (coop_party_defence.py --flock path), wait until the event ends, host `coop.drama start 8`. Assert the host's flock members follow the host's avatar (distance to the host vs the guest's puppet over 20 s). Main: they go to the guest.

## Verification
Test `creature_flock_forget` (coop_creature_flock_forget.py), the regression: the guest's flock, its end, the host's own flock comes to the host. Runs with `SPORE_FIX_OFF=creature.flock_forget`: out/lab/20261112-072224, -073101, -074517; with the fix: -072244, -073116, -074532. No run showed a difference a player sees.
* The test in its final form (the regression check, after the summon members were seen to leave the pack at the event's end: earlier versions asked them to stay and failed both ways, -074517 / -074532): PASS out/lab/20261112-080250 (lane 7, the fix on; the host's own flock 3 of 3 within 25 m of the host).

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-creature, C2 of 0.7.13): not reproduced as a visible bug (the flock is gone before the end branch of the update); hardening behind creature.flock_forget.
