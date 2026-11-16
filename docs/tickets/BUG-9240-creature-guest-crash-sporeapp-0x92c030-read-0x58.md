---
id: BUG-9240
title: Creature: guest crash (SporeApp+0x92C030, read 0x58) when a world share arrives while the host is in the creature editor (pregame editor opened with the load)
status: fixed
kind: crash
stage: creature
reported: 2026-11-16
source: QA 2026-11-16 (TITAN, BUG-9239 leak test)
role: guest
versions: 0.7.14.1-lineages60-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: a2dc0416
tests: manual Windows QA: TITAN host + guest, the host in the Creature editor, forced shares (coop.share 1)
signature: SporeApp.exe+0x92C030 read 0x58, stack CreatureGame_ReturnStateMachine / CreatureStun, mode Editor
docs:
related:
---

## Report
QA 2026-11-16 (TITAN QA copy, host instance 9 Dorger.spo, guest instance 8): the host had come back from a memory restart (BUG-9239) into the Creature editor (Dorger.spo often opens in a cinematic or the editor). The test forced a world share (`coop.share 1`) to the guest. The guest loaded Dorger.spo from the galaxy menu and, in the same frame, opened the pregame editor ("Host is making a new world: make your creature now", BUG-9207) because the host was in an editor; 0.9 s later it crashed: access violation at SporeApp.exe+0x92C030, read of 0x58, game mode Editor, stack CreatureStun+0x188 < CreatureGame_ReturnStateMachine+0xCB1 ... cGameTimeManager::Pause < CreatureGame_ReturnStateMachine+0x17AA < PreGameModeTransition. Crash report crash-20261116-010239-i8.txt (QA copy logs). The memory tracker (coop.memcensus track on) was on.
To reproduce (inferred): host in the Creature editor in a Creature world, a guest at the galaxy menu receives the host's world (a join or coop.share).

## Root cause
Observed in the guest's log (01:02:38.460 / .592): "world: loading from the galaxy menu 4.7 s after the request" and "pregame editor: the creature editor with the base creature while the host makes a new world" in the same frame. world.cpp's menu load clears its request (g_via_menu) and posts the load outside the frame; world::load_queued() is false from then on, so coop.cpp's pregame check (BUG-9207: host in an editor, "from the menu" because the host's first state seen here was the editor and its world was the guest's last one) saw no load coming and posted the editor request outside the frame too: the load and the editor started together and the editor's return ran on a creature game that was not set up (null at +0x58).

## Fix
- world::load_starting(): a load posted from the galaxy menu counts as coming until the game is in it (15 s at most, while the mode is still the menu or the load). The pregame editor (both the 9207 and the 9208 paths) waits while it is true, and the queued request checks again outside the frame (a load began meanwhile: not opened, logged "the host's world is loading (BUG-9240)"). Switch world.load_starting.

## Verification
PASS (TITAN, the fix on, the 01:02 setting forced the other way round: host Dorger.spo in the Creature editor by `coop.newcreature`, three forced shares `coop.share 1` to the guest): three loads from the galaxy menu, no pregame editor, no crash.
no-fail-run: not reproduced with the fix off either (two runs, six tries: the guest saw the host in Creature first, or it never got the world while the host was in the editor). The crash needs the test's forced share while the host is in the editor: in play the host does not share from an editor (world save blocker), so players should not meet it; the guard closes the race anyway.

## History
- 2026-11-16: opened.
- 2026-11-16: fixed (world.load_starting); not reproducible without the forced share.
