---
id: BUG-1465
title: stale_last_attacker: the NPC pick read the guest's mirrors once right after the start, before it had spawned them (0 mirrored)
status: fixed
kind: test
stage: Creature
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in: 0.7.16.1
branch: fix/w65-tests
commits: def13b5f
tests: tools/tests/coop_stale_last_attacker.py (stale_last_attacker)
signature:
docs:
related: BUG-1348, BUG-1444
---

## Report
Validation run of 0.7.16 (lab 20261123-023643, mod code 5376d388): stale_last_attacker "FAIL host NPCs of other species mirrored on the guest (0)".

## Root cause
Test: the pick read the guest's mirrors once right after start(); guest `Loading -> Creature` at 06:37:11.227 with "npcs mirrored: 0", 21 mirrors at 06:37:12.06. The same class as BUG-1348 (creature_kills, fixed there only) and BUG-1444. Not the mod.

## Fix
coop_stale_last_attacker.py: the pick is read every 0.5 s for up to 30 s until there is a candidate; the check is unchanged.

## Verification
FAIL before the fix: 20261123-023643 (stale_last_attacker, mod code 5376d388, the test of before def13b5f). PASS with it:
20261123-104630 (stale_last_attacker, rel/0.7.16.1 30be883d, which holds def13b5f). no-fail-run: a test fix (the pick read every 0.5 s up to 30 s); def13b5f
is in v0.7.16 (released with it).

- 2026-11-23 (0.7.16.1 final check): stale_last_attacker PASS in 20261123-152921 (aad722ac).

## History
- 2026-11-23: closed on the 0.7.16.1 final check lab evidence.
- 2026-11-23: opened (fix/w65-tests).
- 2026-11-23: released in 0.7.16.1.
