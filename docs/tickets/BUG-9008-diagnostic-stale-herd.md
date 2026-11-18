---
id: BUG-9008
title: A sync diagnostic dereferences a freed cached native herd
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-03
source: Duo fork Windows lab
role: guest
versions: 0.7.3.6-duo8 development
fixed_in: 0.7.3.6-duo8
branch: duo-species
commits:
tests: tools/tests/duo_tribe_survival.py
signature: coop.i5.1.dll+CE6DA in game::noun_id, read 00000020, called by support describe and npc::list
docs: docs/re/creature.md#creature-cached-herd-diagnostic
related: BUG-9007
---

DuoQASurvivor5 crashed at 19:42:55 after the host's genuine population death.
The sync checker described mismatching wildlife. npc::list called noun_id on
a g_local_herds entry whose vtable had become null. object_alive only reads a
byte inside the object and does not establish that the allocation is live.
The cached-herd spawn lookup made the same lifetime assumption.

Crash evidence remains in the isolated lab's crash-20261103-194255-i5 files.

The diagnostic now identifies live herds from the native noun list. Proxy
creation also resolves the cached herd through that list before reuse. On
DuoQARecoverySequential4/5 the population-loss movement test passed, followed
by a two-minute soak with 107 rounds of NPC and Tribal listings. Both native
processes stayed alive with zero faults. See out/duo8/diagnostic-soak.txt.
