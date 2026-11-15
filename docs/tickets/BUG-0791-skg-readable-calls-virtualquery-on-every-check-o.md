---
id: BUG-0791
title: skg::readable calls VirtualQuery on every check: on native Windows it can eat the main thread with many objects (suspected)
status: wontfix
kind: bug
stage: All
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-main 589ca98d (docs/re/build-dc04.md large adventure frame cost)
role: 
versions: 
fixed_in:
branch: fix/w48-small
commits: 3a3c5d3b
tests: perf_readable, perf_readable_civ_npc
signature:
docs: docs/06-testing.md#testing-p1-g1
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
Measured first, as the plan asked; not changed: it costs nothing measurable here. `skg::readable` now counts its calls (an interlocked increment) and can make N more VirtualQuery per call for a cost check (`coop.perf readable [extra n]`: calls per frame since the last ask, fps, and the cost of one call from a loop of 2000). Test `perf_readable` (extended, per fixture): both games of a session, 20 s as they are, then 20 s with 20 more VirtualQuery per call.

| fixture | game | readable() calls a frame | fps | one call (loop) | share of the frame |
|---|---|---|---|---|---|
| creature | host | 39 | 56.4 | 1.22 us | 0.27 % |
| creature | guest | 36 | 58.8 | 1.10 us | 0.23 % |
| civ_npc | host | 136 | 42.8 | 1.52 us | 0.89 % |
| civ_npc | guest | 194 | 42.6 | 1.29 us | 1.06 % |

With 20 more VirtualQuery per call (21 x the cost) the frame rate moved within its own noise (Creature 56.4 -> 55.8 / 58.8 -> 56.9; Civ 42.8 -> 49.0, i.e. faster). Under Wine one VirtualQuery is about 1.1-1.5 us; even the Civ guest's 194 calls take about 0.25 ms of a 23 ms frame. The fork's case (97 % of the main thread in NtQueryVirtualMemory) was a 438-actor adventure on native Windows, where we have no lane: there a call may cost more, but our per-frame counts are small (tens to ~200). A scoped cache (the fork's ReadOnlyMemoryScope) is not worth its risk (a stale region answer after a free) for ~1 % at most. perf_readable fails at 5 % of the frame, so a code change that multiplies the calls is caught.

## Verification
no-fail-run: a measurement, nothing fixed. Runs: out/lab/20261112-124945 (perf_readable, creature), out/lab/20261112-125124 (perf_readable_civ_npc). Not measured: native Windows (no Windows lane), a 4-player session, Tribe (perf_readable_tribe exists, not run).

## History
- 2026-11-10: opened.
- 2026-11-12 (fix/w48-small): measured (0.2-1.1 % of the frame under Wine, 36-194 calls a frame): wontfix; counter, coop.perf readable and perf_readable kept.
