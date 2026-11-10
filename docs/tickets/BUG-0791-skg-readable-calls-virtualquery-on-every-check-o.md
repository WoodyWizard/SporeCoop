---
id: BUG-0791
title: skg::readable calls VirtualQuery on every check: on native Windows it can eat the main thread with many objects (suspected)
status: open
kind: bug
stage: All
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-main 589ca98d (docs/re/build-dc04.md large adventure frame cost)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
No player report. Possible low frame rate on native Windows when the mod checks many game pointers per frame
(large NPC counts, 4 players), suspected only.

## Root cause
Inferred from the fork (not verified for our code; confidence low-medium): `sdk/include/sporekit_game.hpp:79`
`skg::readable` calls VirtualQuery on every call (a NtQueryVirtualMemory syscall; on Windows far more expensive than under
Wine). The GA fork sampled main-thread instruction pointers in a 438-actor adventure on native Windows: 97/100 host and
99/100 guest samples were inside NtQueryVirtualMemory, called from skg::readable; the game ran at 3.9-5.9 FPS and at
21.5 FPS after their fix. We have not counted how many readable() calls our own per-frame code makes (npc.cpp,
worldobj.cpp, cellsync.cpp use it widely).

## Fix idea
From the fork (jwhans0n/SporeCoopGA 589ca98d, docs/re/build-dc04.md "Large adventure frame cost on native Windows"); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
They added a `ReadOnlyMemoryScope`: an RAII scope that, while active, caches the memory regions VirtualQuery returned
(bounded cache, more than 64 regions handled) so nested / repeated readable() checks in one synchronous read-only pass
(enumeration, snapshot collection) query each region once. A scope never spans writes, object creation / destruction, a
queued task or a frame boundary; unscoped checks still query every time. Test: a Win32 fixture
(`tests/readable_memory_tests.cpp`) with 438 repeated/nested checks, protection changes between passes, cross-region
reads, guard pages, freed memory and overflowing ranges. First step for us: measure (count readable() calls per frame in
a 4-player Creature / Civ session, profile on a Windows lane).

## How to test
Windows lane, 4-player Creature with many NPCs: count readable() calls per frame and the main thread's share in
NtQueryVirtualMemory (sampling). FAIL: a large share (several percent or more); PASS after a scoped cache: share small,
FPS up, the readable fixture (guard pages, freed memory) still correct.

## Fix
## Verification

## History
- 2026-11-10: opened.
