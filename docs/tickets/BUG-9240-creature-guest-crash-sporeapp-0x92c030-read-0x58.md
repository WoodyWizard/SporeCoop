---
id: BUG-9240
title: Creature: guest crash (SporeApp+0x92C030, read 0x58) when a world share arrives while the host is in the creature editor (pregame editor opened with the load)
status: open
kind: crash
stage: creature
reported: 2026-11-16
source: QA 2026-11-16 (TITAN, BUG-9239 leak test)
role: guest
versions: 0.7.14.1-lineages60-dev
fixed_in:
branch:
commits:
tests:
signature: SporeApp.exe+0x92C030 read 0x58, stack CreatureGame_ReturnStateMachine / CreatureStun, mode Editor
docs:
related:
---

## Report
QA 2026-11-16 (TITAN QA copy, host instance 9 Dorger.spo, guest instance 8): the host had come back from a memory restart (BUG-9239) into the Creature editor (Dorger.spo often opens in a cinematic or the editor). The test forced a world share (`coop.share 1`) to the guest. The guest loaded Dorger.spo from the galaxy menu and, in the same frame, opened the pregame editor ("Host is making a new world: make your creature now", BUG-9207) because the host was in an editor; 0.9 s later it crashed: access violation at SporeApp.exe+0x92C030, read of 0x58, game mode Editor, stack CreatureStun+0x188 < CreatureGame_ReturnStateMachine+0xCB1 ... cGameTimeManager::Pause < CreatureGame_ReturnStateMachine+0x17AA < PreGameModeTransition. Crash report crash-20261116-010239-i8.txt (QA copy logs). The memory tracker (coop.memcensus track on) was on.
To reproduce (inferred): host in the Creature editor in a Creature world, a guest at the galaxy menu receives the host's world (a join or coop.share).

## Root cause
Inferred, not analysed yet: the pregame editor (BUG-9207, meant for a host making a new world) is requested in the same frame as the world load; the editor's return state machine then runs on a creature game that is not set up (a null at +0x58). The pregame rule should not take a host in the Creature editor of an existing world for "making a new world".

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
