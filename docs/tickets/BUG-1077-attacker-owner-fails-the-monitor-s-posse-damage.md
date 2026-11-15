---
id: BUG-1077
title: attacker_owner fails the monitor's posse_damage gate in 3 of 6 recent runs (the host hit by its own pack)
status: fixed
kind: test
stage: Creature
reported: 2026-11-13
source: lab
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-drama
commits: 4c33c54d
tests: attacker_owner
signature: monitor posse_damage "hit by pack owner=npc posse=1 amount=0.1 forwarded" for a UFO hit
docs: docs/06-testing.md#testing-w49-drama
related: BUG-0790, BUG-0417
---

## Report
attacker_owner (BUG-0790's test: the host's Drama Manager makes its UFO, the UFO hits the guest's puppet) fails
the invariant monitor's posse_damage gate now and then, cold and warm alike: `posse_damage: host: player 1 hit by
its own party (pack, posse=1) for 0.1 hp forwarded`. Recorded: lab 20261112-073116 (lane 7, cold),
20261112-094823 (lane 6, cold), 20261113-094538 (lane 7, cold start after a refused warm reuse, fix/w49-warm);
passed in 20261112-080216 and 20261112-175447. Found while moving tests to warm resets (fix/w49-warm): not a warm
reset effect, the test stays as it was (cold).

## Root cause
Observed: a real bug of the mod, in the monitor's log line (not the gate, not the game). The failing runs' host line
is `damage: 1 hit by pack owner=npc posse=1 amount=0.1 forwarded`, the passing runs' `1 hit by npc owner=npc posse=0
amount=0.1 forwarded`, both for the test's own `coop.hp ufohit 1 0.1` (the UFO's combatant hits the guest's
puppet). Since BUG-0790 the attacker handed to the damage filter is the combatant's owner object (here the UFO). The
filter itself takes only a listed creature for its party check (npc.cpp `att_creature`), so the hit was forwarded
as it should; but `log_hit` (the monitor's `damage:` line) read the creature flags (+0xB58 & 0x100, the pack bit)
and the herd of every attacker as if it were a creature. In a UFO object +0xB58 is other data (or past its end):
the bit was set in 3 of 6 runs, so the line said "pack, posse=1" and the posse_damage gate failed. The gate is right
(a posse=1 hit that lands is the BUG-0374 class); its input was wrong.

## Fix
npc.cpp `log_hit`: an attacker whose noun (vt 0x20) is not a creature (0x18EB45E) or citizen (0x18EB4B7) is written
as `hit by object owner=- posse=0`, its fields are not read (only with creature.attacker_owner on: with it off the
attacker pointer is the old wrong one). Switch `coop.fix npc.hit_log_object off`: read as a creature (0.7.13).
The test (coop_attacker_owner.py) now also checks the monitor's line of the UFO's hit: `object`, posse=0.

## Verification
attacker_owner, lane 7, cold: FAIL with `SPORE_LAB_FIXOFF="attacker_owner=npc.hit_log_object"` 20261113-124819
(`1 hit by pack owner=npc posse=1 amount=0.1 forwarded`, MONITOR-VIOLATION posse_damage); PASS with the fix
20261113-124910 and 20261113-124954 (`1 hit by object owner=- posse=0 amount=0.1 forwarded`).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-drama (the log line, not the gate); test extended.
- 2026-11-14: released in 0.7.14.
