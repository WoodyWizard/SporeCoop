---
id: BUG-9002
title: Extra NPC villages appear on the guest during a longer Tribe session
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-03
source: Duo fork Windows lab
role: guest
versions: 0.7.3.6-duo3
fixed_in: 0.7.3.6-duo4
branch: duo-species
commits:
tests: tools/tests/duo_npc_villages.py, tools/tests/duo_npc_repair.py
signature:
docs: docs/re/tribe.md#tribe-duo-guest-village
related: BUG-9001
---

## Report

Two Steam instances, disposable Clyth save, after several minutes and a reconnect.
Both had the registered home and guest villages. The guest additionally had species
06785BC2, 066B8278 and 067C684A in villages with 9, 7 and 7 members. The host
reported none of those villages and continued sending snapshots for six founder citizens.
Compare `coop tribe villages` in both games. The time and caller of the extra spawns
are unconfirmed. The founding-food acceptance test compares only its two named stores.

The archived trace had two local tribes at 70 seconds and five at 110 seconds while
the same player-tribe pointer remained live. On the unpacked Steam build there are
verified direct calls to SpawnNpcTribe at 0xCC66F8 and 0xB942F2. The Duo trace logs
the actual return address of subsequent guest calls to distinguish the two paths.

In the first cold run with tracing, `duo_npc_villages.py` checked both native rosters
for four minutes without a difference. A guest-only food change was repaired in that
run. This does not reproduce the original reload/reconnect sequence.

## Fix and verification

On Duo 3 a deliberately isolated extra village set off two additional native calls
from 0xCC66FD. Manual host save transfer restored the guest. On Duo 4 the guest stops
its own native formation routine while following the host's registered villages. A
persistent extra village pauses guest orders and requests at most one automatic host
checkpoint in that host world/session. On the same Windows fixture, orders paused six
seconds after the test spawn and a new slot was loaded four seconds later. Both native
rosters and founding species agreed again. The automatic repair test passed on rejoin.
