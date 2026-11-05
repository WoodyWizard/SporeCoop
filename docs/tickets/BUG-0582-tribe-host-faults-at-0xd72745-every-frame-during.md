---
id: BUG-0582
title: Tribe: host faults at 0xD72745 every frame during its own creature attack event
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-04
source: lab (fix/w43-realign), tribe_bounce_world
role: host
versions: 
fixed_in: 0.7.7
branch: fix/w43-crashes
commits: 97571768
tests: tribe_attack_spot (coop_tribe_attack_spot.py), tribe_bounce_world
signature: 0xC0000005 at 0x00D72745 (data 0x44), calls 00C945E1 00D74407 00C0B82C 00D9A93F
docs: docs/re/tribe.md#tribe-attack-spot
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
Lab: tribe_attack_spot FAIL with `SPORE_FIX_OFF=tribe.attack_spot` out/lab/20261105-000150 (host 431 caught
faults at TribeAttack_TakeSpot+0x25; also 20261104-235754, 357), PASS out/lab/20261104-235955 (2 attackers ended by
the guard, 0 faults).

## History
- 2026-11-04: opened.
- 2026-11-05: cause found (null spot record), fix tribe.attack_spot, test tribe_attack_spot (fix/w43-crashes).
- 2026-11-05: released in 0.7.7.
