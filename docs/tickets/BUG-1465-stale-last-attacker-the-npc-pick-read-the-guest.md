---
id: BUG-1465
title: stale_last_attacker: the NPC pick read the guest's mirrors once right after the start, before it had spawned them (0 mirrored)
status: open
kind: test
stage: Creature
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in:
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
Open, waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run stale_last_attacker --lane N --retry 0 --no-build` PASS. no-fail-run: a test fix; FAIL before 20261123-023643.

## History
- 2026-11-23: opened (fix/w65-tests).
