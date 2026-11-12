---
id: BUG-0847
title: World share silently drops save files it cannot read, the guest accepts an incomplete world
status: fixed
kind: desync
stage: All
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 (world.cpp collect rewrite, no ticket id)
role: host
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in: unreleased
branch: fix/w48-saves
commits: 4175db39
tests: win_unit
signature: 
docs: docs/06-testing.md#testing-saves-w48
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
slotfs::collect (moved from world.cpp so win_tests runs it) reads every file of the slot with CreateFileW (all share
modes) and exactly its directory size; a file that cannot be opened, changes size while it is read, a listing error,
or more than 2048 files / 128 MB fails the share with the reason ("the save file X cannot be opened (error 32)").
share_now returns that error: the share fails and coop.cpp retries it (BUG-0231). The guest also refuses an offer
whose files or bytes do not all arrive. Switch `coop.fix world.share_complete off` = 0.7.12 (unreadable files left out).

## Verification
win_unit `test_share_collect`: a slot file held with share mode 0 (Wine: error 32).
* FAIL: out/lab/20261112-013127 (`SPORE_FIX_OFF=world.share_complete`, lane 2): collect succeeded without the file.
* PASS: out/lab/20261112-012955 (lane 2), out/lab/20261112-024053 (lane 3): refused, 0 files, the reason names sub/x.pkp.
Observed in the lab: the mechanism (a held file was dropped silently). Not measured: how often players' scanners hold a
save file at share time.

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-12: fixed on fix/w48-saves (world.share_complete), FAIL 20261112-013127, PASS 20261112-012955.
