---
id: BUG-1019
title: Updater: apply_pack keeps only the first 4096 bytes of an existing sporecoop.pending list
status: open
kind: bug
stage: Installer
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-13
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
