---
id: BUG-0847
title: World share silently drops save files it cannot read, the guest accepts an incomplete world
status: open
kind: desync
stage: All
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 (world.cpp collect rewrite, no ticket id)
role: host
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in:
branch:
commits:
tests:
signature: 
docs:
related: BUG-0105, BUG-0254
---

## Report
When the host shares its world and one file of the slot cannot be opened or read (Windows: a scanner or indexer
holds it, or the save is still being written, see the 0x06318E78 ticket), the mod leaves that file out without a word.
The checksum covers only the files that were read, so the guest writes and loads an incomplete save (inferred: a load
that fails or a world with missing parts).

## Root cause
* Observed (code): world.cpp:121 read_file returns false on open failure; collect (world.cpp:135) then `continue`s
  and sends the rest. ftell errors and short reads against the directory size are not detected. The CRC is computed
  over the collected files only.
* Observed: the host has no size or file-count limit when it collects; the guest rejects an offer over 128 MB or
  2048 files (world.cpp:67, 1630), so such a share fails only on the guest.
* Confidence: high for the code fact, unknown how often it happens on players' PCs.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages PR #196, world.cpp collect() rewrite); NOT FINAL: taken from the fork, verify on our code and against our rules before using.

* collect() returns bool with an error text and fails the whole share on: a FindFirstFile error other than
  ERROR_FILE_NOT_FOUND, a FindNextFile end other than ERROR_NO_MORE_FILES, an unsafe relative path (safe_rel) or a
  reparse point, a file larger than the remaining kMaxWorldBytes or more than kMaxWorldFiles, a file that cannot be
  opened, or a read whose length differs from the directory's nFileSizeLow (fread size, then fgetc == EOF, no ferror:
  "save changed or could not be read").
* share_now / queue_slot return that error (the share is retried by the caller), and the sum is checked against
  kMaxWorldBytes while the CRC is computed.
* Guest side (finish): besides the CRC, file count and total size must equal the offer's.
* No switch in the fork.

## How to test
win_tests.exe case (Windows unit tests under Wine): make a slot, hold one of its files open without share access,
call the share collection: it must fail with a reason, not return the other files. FAIL on main (files minus one,
success), PASS with the fix. Plus a lab test: hold a file of the host's slot during a guest join and check that the
guest does not load a world (or gets it once the file is free).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
