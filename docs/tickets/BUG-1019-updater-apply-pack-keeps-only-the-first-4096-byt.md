---
id: BUG-1019
title: Updater: apply_pack keeps only the first 4096 bytes of an existing sporecoop.pending list
status: fixed
kind: bug
stage: Installer
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-13
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-ui
commits: 8bc5ebf9, 66f43dd7
tests: win_unit, tests/win_tests.cpp
signature:
docs: docs/06-testing.md#testing-p2-ui
related:
switch: update.pending_whole
---

## Report
Audit 2026-11-12 for 0.7.14, finding KIT-13. Not seen at runtime, found by code reading. When an update defers files
(locked by the running game) and an older sporecoop.pending list is already there, only its first 4096 bytes are
kept; a longer list is cut mid-line and the cut line is glued to the first new entry, so that file is never swapped
(and its fresh copy stays next to it).

## Root cause
Observed in code (main efd2e0ba), confidence high: updater/winupdate.cpp:479 reads the existing list into
`char b[4096]` with one ReadFile and keeps `got` bytes; the rest is lost when the list is written back (:486). The
loader side (updater/pending.h:46) reads the whole list up to 1 MB since BUG-0150; apply_pack was missed then.
Reach (inferred): each deferred file adds one relative path line (~30-60 bytes, the same few files each update), so
4 KB needs dozens of in-game updates without a restart in between. Practically unreachable for a player; Windows only.
Severity: Low.

## Fix idea
Read the whole file (GetFileSizeEx, limit 1 MB as pending.h), keep a trailing newline before appending, and drop
duplicate lines. Switch: `update.pending_whole off`.

## How to test
Extend tools/tests/update_check.py (or coop_reload_update.py): pre-write a 5000-byte sporecoop.pending with known
lines, run an update that defers a file, assert every old line and the new one are in the list. FAIL on main (the
list is cut at 4096), PASS with the fix.

## Fix
updater/winupdate.cpp apply_pack: the existing sporecoop.pending is read whole (GetFileSizeEx, up to 1 MB as
pending.h), ended by a newline, and a deferred file already listed is not added again. Portable switch
`update.pending_whole` (fix_table.cpp; win_tests --old-pending).

## Verification
tests/win_tests.cpp test_pending_long (win_unit): a 4778-byte list of 100 lines, coop.dll held by the "game":
every old line kept whole, coop.dll listed once.
* FAIL (SPORE_FIX_OFF=update.pending_whole): out/lab/20261113-032424 (win_tests.cpp:288, :289)
* PASS: out/lab/20261113-032115, out/lab/20261113-032833

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-ui (0.7.14 P2).
