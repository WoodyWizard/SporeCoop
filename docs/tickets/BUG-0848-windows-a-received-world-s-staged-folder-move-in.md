---
id: BUG-0848
title: Windows: a received world's staged folder move into the save slot fails on a brief file lock (error 5)
status: open
kind: bug
stage: All
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9028
role: guest
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in:
branch:
commits:
tests:
signature: cannot move the world into ... (error 5)
docs:
related: BUG-0254, BUG-0035
---

## Report
On Windows a guest sometimes cannot store the host's world: "could not save the world of <host>: cannot move the world
into ...\Games\GameN (error 5)", and the world is not loaded. The fork saw ERROR_ACCESS_DENIED intermittently in many
slot writes (also the first one), with the games closed; a real handle opened without FILE_SHARE_DELETE reproduces it.

## Root cause
* Observed (code): slotfs.cpp:374 commits the staged temp folder with one MoveFileW(tmp, slot); any failure aborts
  the write (WriteFail::Move) and rolls back.
* Inferred: a short-lived handle (antivirus, indexer, thumbnail) on a file inside the staged folder blocks the rename.
  Confidence medium (fork's evidence; our lab runs Wine, which does not show it).

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages PR #196, BUG-9028, switch world.staged_move); NOT FINAL: taken from the fork, verify on our code and against our rules before using.

* slotfs.cpp: new move_staged(source, destination): loop MoveFileW; on ERROR_ACCESS_DENIED or ERROR_SHARING_VIOLATION
  retry until a deadline of GetTickCount64() + 500 ms, sleeping min(25 ms, remaining); any other error returns at
  once; the last Win32 error is preserved (SetLastError) for the message. It never copies into, replaces or deletes the
  destination. write_locked uses it for the final commit only (the other MoveFileW calls stay).
* Switch g_move_retry_fix registered as `world.staged_move` (COOP_FIX, BUG-9028); exported in slotfs.h for tests.
* Their test: win_tests.exe --staged-move-only: a real source-file lock released after 150 ms makes the old code fail
  (--old-staged-move) and the new one succeed; a permanent lock and an occupied destination still fail without data
  loss; the full save-slot suite passes.

## How to test
win_tests.exe case: hold a file inside the staged folder with CreateFileW without FILE_SHARE_DELETE, release it
from another thread after 150 ms, call slotfs::write. FAIL on main (Move failure), PASS with the retry; plus a
permanent lock (still fails, nothing lost) and an existing destination (refused). Runs under Wine in the win_unit lab
test; check that Wine reproduces the sharing violation for the directory rename (if not, the test documents it).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
