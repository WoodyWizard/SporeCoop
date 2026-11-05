---
id: BUG-0582
title: Tribe: host faults at 0xD72745 every frame during its own creature attack event
status: open
kind: crash
stage: Tribe
reported: 2026-11-04
source: lab (fix/w43-realign), tribe_bounce_world
role: host
versions: 
fixed_in:
branch:
commits:
tests: tribe_bounce_world (coop_tribe_bounce_world.py)
signature: 0xC0000005 at 0x00D72745 (data 0x44), calls 00C945E1 00D74407 00C0B82C 00D9A93F
docs:
related: BUG-0564
---

## Report
Found by the lab (no player report). tribe_bounce_world on fix/w43-realign, lanes 6 / 7: the HOST's game faulted at
0x00D72745 (data 0x44, a null + 0x44) 258-1082 times (every frame) from about 15 s after its own Drama Manager event
`creature_attack` started (drama.cpp log "creature_attack event starts (host or alone)"); its game-over caption did
not come afterwards. Runs: out/lab/20261104-204645 (279 faults), 20261104-205650 (258), 20261104-210008 (1082, fix
wobj.fruit off). Stack: 00C945E1 00D74407 00C0B82C 00D9A93F 00D9A94E 00B384A5 (cCreatureAnimal::Update ->
BehaviorTreeTick -> a behaviour node). Reproduce: `SPORE_LANE=N python3 tools/tests/coop_tribe_bounce_world.py`
until the host's log has the creature_attack line (random, about 1 run in 3).

## Root cause
Not analysed. Observed: host only, after its own creature_attack event, in runs with the guest's game connected.
Inferred: the attacking animal's behaviour node reads a target (a member or a tribe) the game freed or never set.
Not seen in the 0.7.6 replay runs (1 run); the host code of fix/w43-realign only reads objects there.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
